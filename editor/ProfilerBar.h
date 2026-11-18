#pragma once
// The Laboratory's live profiler: where the time of one simulated frame goes, readable at a glance.
//
// Every stage the simulation times with ke::Probe::Timer inside the step ("physics/engine ms",
// "arm/plan ms", ...) becomes a segment of one horizontal bar, as long as its milliseconds; what no
// timer covers is a grey segment at the end, so the bar adds up to the whole step ("physics/step ms").
// The segments of one part share a colour (physics, the arm, vision), in the Okabe-Ito palette that
// colour-blind eyes tell apart too, and the legend under the bar names the parts in words. Under it, a sparkline shows the
// whole step over the last 5 seconds with the 16.7 ms line of 60 frames a second, as the Unity
// Profiler and PhysX PVD show it. Hovering a segment says its name and share; a right click offers
// «Построить график» of that stage (the plotRequested signal).
#include "core/Probe.h"

#include <QColor>
#include <QElapsedTimer>
#include <QString>
#include <QWidget>

#include <deque>
#include <string>
#include <vector>

class ProfilerBar : public QWidget {
    Q_OBJECT
public:
    explicit ProfilerBar(QWidget* parent = nullptr);

    // One stage of the last frame: the Probe channel, its name in words, its time and its colour.
    struct Stage {
        std::string channel; // "physics/engine ms"
        QString part;        // «Физика», «Рука», «Зрение», «Прочее», «Вне замеров»
        QString name;        // «движок физики»
        double ms = 0;
        QColor color;
    };

    // One simulated frame: its timers become the bar, the whole step goes into the 5 s sparkline.
    void addFrame(const ke::Probe::Snapshot& probe);
    const std::vector<Stage>& stages() const { return stages_; }
    double stepMs() const { return stepMs_; } // the whole step of the last frame ("physics/step ms")
    static constexpr const char* kStepChannel = "physics/step ms";

    QSize sizeHint() const override { return QSize(280, 122); }
    QSize minimumSizeHint() const override { return QSize(200, 116); }

signals:
    void plotRequested(QString channel); // «Построить график» of this stage

protected:
    void paintEvent(QPaintEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    QRectF barRect() const;
    QRectF sparkRect() const;
    int stageAt(const QPointF& p) const; // the segment under a point of the bar (-1: none)
    void paintBar(class QPainter& p);
    void paintLegend(class QPainter& p);
    void paintSparkline(class QPainter& p);

    std::vector<Stage> stages_;
    double stepMs_ = 0;
    std::deque<std::pair<qint64, double>> history_; // (wall-clock ms, whole step ms), the last 5 s
    QElapsedTimer clock_;
};
