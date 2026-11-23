#pragma once
// One chart of the plots (see PlotPanel.h): lines of one unit over a window of time - or, normalised,
// any lines as shapes between 0 and 1 («форма, не величина»).
//
// The cost of a picture does not grow with the run: the chart reads only the samples inside its time
// window (the panel's: the last minute while live, or the whole run), and draws them in one of two
// ways, each a few thousand points at most:
//   - sparse (fewer samples than about 1,5 per pixel column): a 2 px line through every sample;
//   - dense: per pixel column the lowest and highest sample, joined to the column before - a ribbon
//     one polygon wide, which looks exactly like the 2 px line where the data is smooth and like a
//     band where it jitters, and is filled once instead of stroked thousands of times over itself.
//
// The anatomy follows the dataviz rules: 2 px lines, a recessive grid, text in text colours (never
// the series colour), a legend for two lines or more, a direct label at the end of each line for up
// to four (on a large chart), one y-axis only, from zero for a quantity of one sign. The mouse draws a dashed vertical line
// with a dot on every line.
//
// What the reader can do on the chart itself - all optional, the charts come by themselves:
//   - the title ends in a quiet ▾: a click opens the list of quantities for this chart (titleClicked);
//   - a legend entry: a click hides or shows its line, a double click shows it alone;
//   - a right click: the panel's menu for this chart (menuRequested);
//   - the wheel over the plot: the time closer or farther about the mouse (zoomedX: the panel moves every chart of the
//     same axis with it - while the panel's window slides with the run, its width); Shift + the wheel: the values of this
//     chart; a drag with the left button: the window moved (and stopped: the past looked at);
//     zoomed, the plot's corner has ↺ - all of it again, live (zoomCleared). As PlotJuggler, Foxglove, Rerun do.
#include "PlotSeries.h"
#include "UiLanguage.h"

#include <QColor>
#include <QPoint>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// The lines' colours: the eight dark-surface slots of the validated categorical palette (the dataviz skill's
// reference palette, checked with its validator on this chart surface #1c1e23: every adjacent pair CVD ΔE ≥ 8.4,
// normal-vision ΔE ≥ 19.3, every slot ≥ 3:1 against the surface), in this fixed order. A ninth line takes slot 1
// again with a dashed line, a seventeenth with a dotted one: composite encoding, never a generated ninth hue.
QColor plotSlotColor(int slot);
Qt::PenStyle plotSlotStyle(int slot);

// A number for people: a decimal comma and `significant` digits (no trailing zeros); the very large
// and the very small as «1,5·10⁻⁶» - a power in superscript, never the programmer's «1.5e-06».
QString numberText(double v, int significant = 4);

// One line of a chart.
struct PlotTrack {
    std::string id;                        // the channel (never shown: the name is)
    QString name;                          // for the legend, the direct label, the tooltip
    QString unit;
    QColor color;
    Qt::PenStyle style = Qt::SolidLine;    // dashed / dotted past the palette's eighth slot
    const PlotSeries* series = nullptr;    // its history (the panel owns it)
    bool hidden = false;                   // switched off in the legend
};

// Whether a horizontal axis is time: the run's own (no words) or a plugin's clock in seconds («время с подключения, с») -
// such charts slide and stand together; any other axis is a step (generations), shown whole.
inline bool timeAxis(const QString& words) { return words.isEmpty() || words.endsWith(", с") || words.endsWith(", s"); }

// A chart a plugin's panel draws, as the bottom plots show it too: the same lines over the same series. `keep` shares
// their ownership, so the plots' copy outlives nothing it points to; `xWords`: the horizontal axis when it is a step (a
// generation, an iteration) and not the run's time - empty: the time.
struct SharedChart {
    QString title, xWords;
    std::vector<PlotTrack> tracks;
    std::vector<std::shared_ptr<const PlotSeries>> keep;
    // The span of the horizontal axis a step chart shows: from the first point to the last.
    std::pair<double, double> span() const {
        double lo = INFINITY, hi = -INFINITY;
        for (const PlotTrack& t : tracks)
            if (t.series && t.series->size() > 0) lo = std::min(lo, t.series->time(0)), hi = std::max(hi, t.series->time(t.series->size() - 1));
        return lo <= hi ? std::pair<double, double>{lo, hi > lo ? hi : lo + 1} : std::pair<double, double>{0, 1};
    }
    // What the chart shows: the step chart all of it, the time chart the last `live` seconds up to its latest point (the
    // panel's sliding window - a plugin's chart by its own clock).
    std::pair<double, double> window(double live = 60.0) const {
        const auto [lo, hi] = span();
        return timeAxis(xWords) ? std::pair<double, double>{std::max(lo, hi - live), hi} : std::pair<double, double>{lo, hi};
    }
};

// «лучший: 849 м, поколение 12» - a line's value at x of a step chart; «Ошибка: 0,02 рад в 2,40 с» when the axis is time.
QString plotHoverText(const PlotTrack& line, double x, const QString& xWords);

class TrackChart : public QWidget {
    Q_OBJECT
public:
    explicit TrackChart(QWidget* parent = nullptr);

    void setTracks(std::vector<PlotTrack> tracks);
    void setWindow(double t0, double t1); // the span of time shown
    void refresh();                       // new samples: the y-range again, a new picture
    void setTitle(const QString& title, bool menu = true); // menu: the title ends in ▾ and opens a list
    void setEmptyText(const QString& text);                // what an empty chart says
    void setNormalized(bool on);          // each line scaled to 0 … 1 over the window
    void setAxisWords(const QString& words);  // what the horizontal axis is called (empty: the time, «t, с»)
    void setTimeAxisVisible(bool on);     // lanes label the time once, under the lowest lane
    void setHoverTime(double t);          // where the mouse is, on every chart of the panel (NaN: none)
    void setUserWindow(double t0, double t1); // the reader's time window: setWindow's waits till clearZoom
    void clearUserWindow();               // the panel's time window again
    void setUserValues(double y0, double y1); // the reader's values, bottom to top
    void clearZoom();
    std::pair<double, double> shownTime() const { return {t0_, t1_}; } // the time it shows now, s
    // The window slides with the time (the panel's «едет»): its axis counts back from now - «−10 с … сейчас», as a system
    // monitor's - while the reader has not zoomed it to stand.
    void setSliding(bool on) { if (sliding_ != on) sliding_ = on, update(); }                     // the panel's window and the values' own range again
    bool zoomed() const { return userX_ || userY_; }
    // Margins shared by stacked lanes, so their time axes line up (0: the chart's own).
    int neededLeftMargin() const;
    int neededRightMargin() const;
    void setSharedMargins(int left, int right);

    const std::vector<PlotTrack>& tracks() const { return tracks_; }
    QString title() const { return title_; }
    QString axisWords() const { return axisWords_; }
    bool normalized() const { return normalized_; }
    std::pair<double, double> timeRange() const { return {t0_, t1_}; }
    // The value the chart draws for line k at time t (normalised when it is), NaN when there is none.
    double shownValueAt(size_t k, double t) const;
    double timeAtX(double x) const;
    double xAtTime(double t) const;
    int nearestTrack(const QPointF& pos) const; // the shown line nearest to the mouse (-1: none)
    QRectF titleRect() const;
    QRectF legendRect(size_t k) const;           // empty when the chart has no legend
    QRectF plotArea() const { return plotRect(); }

signals:
    void hovered(double t, QPoint globalPos, int track);
    void hoverLeft();
    void titleClicked();
    void legendClicked(int track);
    void legendDoubleClicked(int track);
    void doubleClicked(); // on the plot, not its legend or title
    void zoomedX(double t0, double t1, bool dragged); // the reader zoomed (the wheel) or moved (a drag) the time on this chart
    void zoomedY(double y0, double y1); // the reader zoomed or moved this chart's values
    void zoomCleared();                 // ... and asked for all of it again (↺)
    void menuRequested(QPoint localPos);

protected:
    void paintEvent(class QPaintEvent* e) override;
    void mouseMoveEvent(class QMouseEvent* e) override;
    void mousePressEvent(class QMouseEvent* e) override;
    void mouseDoubleClickEvent(class QMouseEvent* e) override;
    void contextMenuEvent(class QContextMenuEvent* e) override;
    void leaveEvent(class QEvent* e) override;
    void wheelEvent(class QWheelEvent* e) override;
    void mouseReleaseEvent(class QMouseEvent* e) override;

private:
    void updateRanges();
    bool wholeSteps() const;
    QRectF plotRect() const;
    int layoutHeader(std::vector<QRectF>* legendBoxes) const; // title + legend rows: their height
    double shown(size_t k, double v) const;                    // a value as drawn (normalised if on)
    QPointF toScreen(const QRectF& r, double t, double v) const;
    int legendAt(const QPointF& pos) const;
    void drawHeader(QPainter& p) const;
    void drawGrid(QPainter& p, const QRectF& r) const;
    void drawTrack(QPainter& p, const QRectF& r, size_t k) const;
    void drawLine(QPainter& p, const QRectF& r, size_t k, size_t from, size_t to) const;
    void drawRibbon(QPainter& p, const QRectF& r, size_t k, size_t from, size_t to) const;
    void drawDirectLabels(QPainter& p, const QRectF& r) const;
    void drawHoverLine(QPainter& p, const QRectF& r) const;
    QRectF resetRect() const; // ↺ in the plot's corner while zoomed
    size_t shownTracks() const;
    bool directLabels() const { return shownTracks() >= 2 && shownTracks() <= 4 && width() >= 520 && height() >= 220; }

    std::vector<PlotTrack> tracks_;
    std::vector<std::pair<double, double>> own_; // each line's lowest and highest value in the window
    QString title_, axisWords_, emptyText_ = uiText("Kinetica", "нет данных");
    bool menu_ = true;
    bool normalized_ = false;
    bool timeAxis_ = true;
    bool sliding_ = false;
    double hover_ = std::nan("");
    double t0_ = 0, t1_ = 1, y0_ = 0, y1_ = 1;
    double autoT0_ = 0, autoT1_ = 1;       // the panel's window, kept while the reader's is shown
    bool userX_ = false, userY_ = false;   // the reader zoomed the time / the values
    double userY0_ = 0, userY1_ = 1;
    bool dragging_ = false;
    QPointF dragFrom_;
    std::vector<double> yTicks_;
    int sharedLeft_ = 0, sharedRight_ = 0;
    bool allZero_ = false; // every shown line is exactly 0 over the window (said in words)
};
