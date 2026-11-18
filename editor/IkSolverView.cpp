// The IK solver's card (IkSolverView.h): its words above, the residual's chart below, painted by hand.
#include "IkSolverView.h"

#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
const double kTolerance = 1e-4; // m: solveIk's point tolerance, the dashed line
}

IkSolverView::IkSolverView(QWidget* parent) : QWidget(parent) {
    setObjectName("ikSolverView");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 4, 0, 0);
    text_ = new QLabel;
    text_->setWordWrap(true);
    text_->setObjectName("roleRowLabel");
    col->addWidget(text_);
    col->addStretch(1);
    setMinimumHeight(190);
    setToolTip("Решатель обратной кинематики: каждый раунд — шаг демпфированных наименьших квадратов по точному якобиану. "
               "Невязка — промах точки и поворота (поворот в метрах: 0,3 м на радиан). Сначала шаги не длиннее 0,2 рад, "
               "потом невязка падает в разы за раунд");
}

void IkSolverView::show(const ke::IkResult& r, int freedoms) {
    residual_ = r.residual;
    const QString what = freedoms >= 6 ? "точка и поворот" : QString("только точка: у руки %1 степени свободы, поворот требует шести").arg(freedoms);
    text_->setText(QString("<b>Решатель IK</b> — демпфированные наименьшие квадраты (Buss 2004):<br>"
                           "Δθ = Jᵀ(JJᵀ + λ²I)⁻¹e, λ = 0,05; цель — %1.<br>"
                           "Раундов %2 · промах %3 мм, %4° · манипулируемость %5%6")
                       .arg(what)
                       .arg(r.iterations)
                       .arg(double(r.positionError) * 1000, 0, 'f', 3)
                       .arg(double(r.angleError) * 180 / 3.14159265358979, 0, 'f', 3)
                       .arg(double(r.manipulability), 0, 'g', 3)
                       .arg(r.reached ? "" : " · <b>не достал</b>: цель вне досягаемости или за пределами осей"));
    update();
}

QRect IkSolverView::plotRect() const {
    const int top = text_->geometry().bottom() + 10;
    return QRect(44, top, std::max(40, width() - 52), std::max(30, height() - top - 22));
}

// The residual's chart: log10 of each round's residual, the tolerance dashed, the rounds along the bottom.
void IkSolverView::paintEvent(QPaintEvent*) {
    if (residual_.empty()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect area = plotRect();
    double lo = std::log10(kTolerance) - 1, hi = std::log10(kTolerance) + 1;
    for (ke::Real e : residual_) {
        const double l = std::log10(std::max(double(e), 1e-12));
        lo = std::min(lo, l), hi = std::max(hi, l);
    }
    const int n = int(residual_.size());
    auto at = [&](int i, double v) {
        const double x = area.left() + (n > 1 ? double(i) / (n - 1) : 0.5) * area.width();
        return QPointF(x, area.bottom() - (std::log10(std::max(v, 1e-12)) - lo) / (hi - lo) * area.height());
    };
    p.setPen(QColor(90, 96, 110));
    p.drawRect(area);
    for (int d = int(std::ceil(lo)); d <= int(std::floor(hi)); ++d) { // the decades
        const QPointF y = at(0, std::pow(10.0, d));
        p.setPen(QColor(60, 64, 74));
        p.drawLine(QPointF(area.left(), y.y()), QPointF(area.right(), y.y()));
        p.setPen(QColor(150, 156, 170));
        p.drawText(QRectF(0, y.y() - 8, area.left() - 4, 16), Qt::AlignRight | Qt::AlignVCenter, QString("1e%1").arg(d));
    }
    p.setPen(QPen(QColor(120, 200, 120), 1, Qt::DashLine));
    p.drawLine(QPointF(area.left(), at(0, kTolerance).y()), QPointF(area.right(), at(0, kTolerance).y()));
    QPainterPath line;
    for (int i = 0; i < n; ++i) i == 0 ? line.moveTo(at(i, double(residual_[size_t(i)]))) : line.lineTo(at(i, double(residual_[size_t(i)])));
    p.setPen(QPen(QColor(240, 170, 60), 2));
    p.drawPath(line);
    p.setBrush(QColor(240, 170, 60));
    for (int i = 0; i < n; ++i) p.drawEllipse(at(i, double(residual_[size_t(i)])), 2.5, 2.5);
    p.setPen(QColor(150, 156, 170));
    p.drawText(QRectF(area.left(), area.bottom() + 2, area.width(), 18), Qt::AlignCenter, QString("раунды 0…%1 · невязка, м (лог)").arg(n - 1));
}
