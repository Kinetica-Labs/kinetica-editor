#pragma once
// A plugin's panel drawn from its description (docs/contracts/editor-plugin.md, «Словарь элементов панели»): the plugin
// sends data, never widgets - section, row, label, value (a live word the plugin sets), button (a command), chart, and
// the fields number, slider, toggle, choice, vector (FieldForm draws them), text. The fields hold the panel's values; a
// button calls its command with those of them the command has (a panel is a form that stays). The plugin changes them
// while it runs (plugin_panel_set, or show() in a hook of the run): its words, a measured number.
//
// A chart is lines (TrackChart.h): each line is one of the panel's values. Over the run's time (the default), it takes a
// point whenever the plugin shows that value from a frame of the run - at the frame's time; the history is kept here, the
// last minute of it; the time going back is a new run and a new history. With `x` (a training: a generation, an
// iteration) the horizontal axis is another value of the panel: a line takes a point whenever one show() or
// plugin_panel_set carries both the `x` and the line's value, from any thread, and the chart keeps every point from the
// first to the last; `x` not advancing is a new training and a new history. The series are shared (sharedCharts()): the bottom
// plots draw the same ones. Qt lives here only: a plugin does not know it.
//
// A picture (svg) is an SVG the plugin sends as the value of its name: SvgView draws it as wide as the panel with the
// aspect kept, at most `height` px high; an empty or invalid one is a quiet placeholder. It follows the panel's refresh.
// The mouse looks closer: the wheel zooms about the point under it, a drag (the wheel held, as in the 3D view, or the
// left button) moves the picture, a double click shows it
// whole again - the zoom kept while the plugin sends new pictures.
#include "PlotSeries.h"
#include "TrackChart.h"

#include <QByteArray>
#include <QJsonObject>
#include <QSvgRenderer>
#include <QWidget>

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

class FieldForm;
class QBoxLayout;
class QLabel;
class QLineEdit;
class QTimer;

class SvgView : public QWidget {
    Q_OBJECT
public:
    explicit SvgView(int maxHeight, QWidget* parent = nullptr);
    void setSvg(const QByteArray& data); // the same data again costs nothing
    bool valid() const { return renderer_.isValid(); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;
    QSize sizeHint() const override { return {240, heightForWidth(240)}; }
    double zoom() const { return zoom_; }
    QPointF pan() const { return pan_; }

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    void fit();
    QSvgRenderer renderer_;
    QByteArray data_;
    int maxHeight_;
    double zoom_ = 1.0;   // 1: the whole picture
    QPointF pan_;         // where the picture's corner is moved to, px: the screen point = pan_ + zoom_ * the picture's
    QPointF dragFrom_;    // the mouse where a drag began (none: not dragging)
    bool dragging_ = false;
};

class PluginPanel : public QWidget {
public:
    // `press`: a button's command and the panel's values (PluginHost picks the command's own of them).
    PluginPanel(const QJsonObject& description, std::function<void(const QString& command, const QJsonObject& values)> press, QWidget* parent = nullptr);
    QString title() const { return title_; }
    QString place() const { return place_; } // "right": a tab of the inspector; "bottom": a tab of the bottom panel
    QJsonObject values() const { return values_; }
    // Fields and words: what the plugin says, or the last session's. `time`: the frame of the run they were shown
    // from (NaN: none) - the charts' lines among them take a point there.
    void setValues(const QJsonObject& values, double time = std::nan(""));
    // Its charts for the bottom plots: the same lines over the same series, owned jointly.
    std::vector<SharedChart> sharedCharts() const;

private:
    void build(const QJsonObject& node, QBoxLayout* into);
    FieldForm* form(QBoxLayout* into); // the fields in a row of the layout share one form
    void refresh();                    // every field, value and chart from values_ and the lines' histories
    void hover(TrackChart* chart, double t, QPoint at, int track);

    QString title_, place_;
    QJsonObject values_;
    std::function<void(const QString&, const QJsonObject&)> press_;
    std::vector<FieldForm*> forms_;
    std::vector<std::pair<QString, QLabel*>> words_;    // value(name)
    std::vector<std::pair<QString, QLineEdit*>> texts_; // text(name)
    std::vector<std::pair<QString, SvgView*>> svgs_;    // svg(name)
    FieldForm* open_ = nullptr;                         // the form fields go into until another kind of element
    struct Chart {
        TrackChart* view = nullptr;
        QString title, x, xWords;                                          // x: the value that is the axis (empty: the run's time)
        std::vector<std::pair<QString, std::shared_ptr<PlotSeries>>> lines; // a line by its value's name
        double lastX = std::nan("");
    };
    std::vector<Chart> charts_;
    QTimer* soon_ = nullptr;                            // a run shows sixty times a second: the panel follows at 25
    double now_ = 0;                                    // the latest frame shown from
};
