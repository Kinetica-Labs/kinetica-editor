// Histogram drawing uses only Qt's painter, as the time charts do; no Qt Charts or GPU computation.
#include "HistogramChart.h"
#include "PlotChannels.h"

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>

HistogramChart::HistogramChart(QWidget* parent) : QWidget(parent) {
    setObjectName("histogramChart");
    setMinimumHeight(160);
    setMouseTracking(true);
}

void HistogramChart::setResult(const PlotStatistics& result, const QString& name, const QString& unit) {
    result_ = result;
    name_ = name;
    unit_ = unit;
    update();
}

QRectF HistogramChart::plotArea() const { return QRectF(70, 28, std::max(1, width() - 100), std::max(1, height() - 78)); }

void HistogramChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#1c1e23"));
    p.setPen(QColor("#d5dbe5"));
    p.drawText(QRect(12, 3, width() - 24, 22), Qt::AlignLeft | Qt::AlignVCenter, name_);
    if (!result_.count) {
        p.drawText(rect(), Qt::AlignCenter, "Нет измерений: выберите величину и нажмите ▶");
        return;
    }
    const QRectF area = plotArea();
    const size_t peak = *std::max_element(result_.bins.begin(), result_.bins.end());
    for (int tick = 0; tick <= 4; ++tick) {
        const double y = area.bottom() - area.height() * tick / 4;
        p.setPen(QColor("#343943"));
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        p.setPen(QColor("#aab1bd"));
        p.drawText(QRectF(2, y - 9, 62, 18), Qt::AlignRight | Qt::AlignVCenter, numberText(double(peak) * tick / 4));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#3987e5"));
    const double binWidth = area.width() / double(result_.bins.size());
    for (size_t i = 0; i < result_.bins.size(); ++i) {
        const double h = area.height() * double(result_.bins[i]) / double(peak);
        p.drawRect(QRectF(area.left() + double(i) * binWidth, area.bottom() - h, std::max(1.0, binWidth - 1), h));
    }
    p.setPen(QColor("#aab1bd"));
    p.drawText(QRectF(area.left(), area.bottom() + 5, area.width() / 2, 18), Qt::AlignLeft, numberText(result_.lowerEdge));
    p.drawText(QRectF(area.center().x(), area.bottom() + 5, area.width() / 2, 18), Qt::AlignRight, numberText(result_.upperEdge));
    p.drawText(QRectF(area.left(), area.bottom() + 25, area.width(), 18), Qt::AlignCenter, unit_.isEmpty() ? "Значение" : unit_);
    p.save();
    p.translate(15, area.center().y());
    p.rotate(-90);
    p.drawText(QRectF(-area.height() / 2, -10, area.height(), 20), Qt::AlignCenter, "Измерений");
    p.restore();
}

void HistogramChart::mouseMoveEvent(QMouseEvent* event) {
    const QRectF area = plotArea();
    if (!result_.count || !area.contains(event->position())) { QToolTip::hideText(); return; }
    const size_t bin = std::min(result_.bins.size() - 1, size_t((event->position().x() - area.left()) / area.width() * result_.bins.size()));
    const double step = (result_.upperEdge - result_.lowerEdge) / double(result_.bins.size());
    const QString text = QString("[%1; %2%3 %4\n%5 измерений (%6 %)")
        .arg(numberText(result_.lowerEdge + step * bin), numberText(result_.lowerEdge + step * (bin + 1)),
             bin + 1 == result_.bins.size() ? "]" : ")", unit_)
        .arg(qulonglong(result_.bins[bin])).arg(numberText(100.0 * double(result_.bins[bin]) / double(result_.count)));
    QToolTip::showText(event->globalPosition().toPoint(), text, this);
}

void HistogramChart::leaveEvent(QEvent*) { QToolTip::hideText(); }
