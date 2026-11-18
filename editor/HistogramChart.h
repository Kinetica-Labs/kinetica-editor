#pragma once
// A distribution of one physical quantity: equal-width bins, count axis from zero, explicit units.
// The owned result remains valid when the source history is cleared or a different body is watched.
#include "PlotStatistics.h"

#include <QString>
#include <QWidget>

class HistogramChart : public QWidget {
public:
    explicit HistogramChart(QWidget* parent = nullptr);
    void setResult(const PlotStatistics& result, const QString& name, const QString& unit);
    const PlotStatistics& result() const { return result_; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent*) override;

private:
    QRectF plotArea() const;
    PlotStatistics result_;
    QString name_, unit_;
};
