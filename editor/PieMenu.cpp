// The pie menu (PieMenu.h): eight sectors of 45 degrees, sector 0 at the top, clockwise.
#include "PieMenu.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace {
constexpr double kDeadZone = 22, kRadius = 118, kLabelRadius = 84;
constexpr double kPi = 3.14159265358979323846;
}

PieMenu::PieMenu(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
}

int PieMenu::sectorOf(QPointF centre, QPointF mouse, int count) {
    const QPointF d = mouse - centre;
    if (std::hypot(d.x(), d.y()) < kDeadZone || count <= 0) return -1;
    const double clockwiseFromTop = std::atan2(d.x(), -d.y()) * 180 / kPi; // screen y grows down
    const double step = 360.0 / count;
    return (int(std::lround(clockwiseFromTop / step)) % count + count) % count;
}

void PieMenu::open(QPointF centre, const QStringList& labels, const QList<QPixmap>& icons, int suggested) {
    centre_ = centre;
    labels_ = labels;
    icons_ = icons;
    suggested_ = suggested;
    lit_ = -1;
    setGeometry(parentWidget()->rect());
    show();
    raise();
    update();
}

void PieMenu::track(QPointF mouse) {
    const int s = sectorOf(centre_, mouse, int(labels_.size()));
    if (s != lit_) lit_ = s, update();
}

void PieMenu::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(18, 20, 25, 150));
    p.drawEllipse(centre_, kRadius, kRadius);
    const int n = int(labels_.size());
    const double step = n > 0 ? 360.0 / n : 360.0;
    for (int k = 0; k < n; ++k) {
        const double a = (90 - step * k) * kPi / 180; // sector k's direction, y up
        const QPointF at = centre_ + QPointF(std::cos(a), -std::sin(a)) * kLabelRadius;
        if (k == lit_) { // the lit sector: a wedge of light
            QPainterPath wedge;
            wedge.moveTo(centre_);
            wedge.arcTo(QRectF(centre_ - QPointF(kRadius, kRadius), QSizeF(2 * kRadius, 2 * kRadius)), 90 - step * k - step / 2, step);
            wedge.closeSubpath();
            p.setBrush(QColor(255, 200, 70, 90));
            p.drawPath(wedge);
        }
        const bool icon = k < icons_.size() && !icons_[k].isNull();
        const QString text = (k == suggested_ ? "★ " : "") + labels_[k];
        QFont f = font();
        f.setBold(k == lit_ || k == suggested_);
        p.setFont(f);
        const QRectF box(at - QPointF(icon ? 62 : 52, icon ? 16 : 11), QSizeF(icon ? 124 : 104, icon ? 32 : 22));
        p.setBrush(k == lit_ ? QColor(255, 205, 80) : QColor(40, 44, 52, 230));
        p.drawRoundedRect(box, 6, 6);
        if (icon) p.drawPixmap(QRectF(box.left() + 4, box.top() + 4, 24, 24), icons_[k], icons_[k].rect());
        p.setPen(k == lit_ ? QColor(20, 20, 20) : k == suggested_ ? QColor(255, 222, 120) : QColor(225, 230, 238));
        p.drawText(icon ? box.adjusted(30, 0, -4, 0) : box, icon ? (Qt::AlignVCenter | Qt::AlignLeft) : Qt::AlignCenter, text);
        p.setPen(Qt::NoPen);
    }
    p.setBrush(QColor(255, 255, 255, 60));
    p.drawEllipse(centre_, kDeadZone, kDeadZone);
}
