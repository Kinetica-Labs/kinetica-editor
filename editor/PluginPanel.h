#pragma once
// A plugin's panel drawn from its description (docs/contracts/editor-plugin.md, «Словарь элементов панели»): the plugin
// sends data, never widgets - section, row, label, value (a live word the plugin sets), button (a command), chart, and
// the fields number, slider, toggle, choice, vector (FieldForm draws them), text. The fields hold the panel's values; a
// button calls its command with those of them the command has (a panel is a form that stays). The plugin changes them
// while it runs (plugin_panel_set, or show() in a hook of the run): its words, a measured number.
//
// A chart is lines over the run's time (TrackChart.h): each line is one of the panel's values, and takes a point
// whenever the plugin shows that value from a frame of the run - at the frame's time. The history is kept here, the
// last minute of it; the time going back is a new run and a new history. Qt lives here only: a plugin does not know it.
#include "PlotSeries.h"

#include <QJsonObject>
#include <QWidget>

#include <cmath>
#include <functional>
#include <map>
#include <vector>

class FieldForm;
class QBoxLayout;
class QLabel;
class QLineEdit;
class QTimer;
class TrackChart;

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
    FieldForm* open_ = nullptr;                         // the form fields go into until another kind of element
    std::map<QString, PlotSeries> lines_;               // a chart's line by its value's name
    std::vector<TrackChart*> charts_;
    QTimer* soon_ = nullptr;                            // a run shows sixty times a second: the panel follows at 25
    double now_ = 0;                                    // the latest frame shown from
};
