#pragma once
// The magnet's pie menu - a marking menu (Kurtenbach and Buxton, "The limits of expert performance
// using hierarchic marking menus", CHI 1993): a few choices around the point the mouse was pressed,
// always in the same places, clockwise from the top, so after a few uses the hand flicks to one
// without looking and the menu need not even open. It is drawn over the 3D view, which keeps the
// mouse: the menu only shows where the mouse points. The suggested choice wears a star; the sector
// the mouse points to (past a small dead zone round the centre) is lit.
#include <QList>
#include <QPixmap>
#include <QStringList>
#include <QWidget>

class PieMenu : public QWidget {
public:
    explicit PieMenu(QWidget* parent);
    void open(QPointF centre, const QStringList& labels, const QList<QPixmap>& icons, int suggested); // icons: one each, or none
    void track(QPointF mouse);  // lights the sector the mouse points to
    int chosen() const { return lit_; } // the lit sector (-1: the mouse is in the dead zone)
    static int sectorOf(QPointF centre, QPointF mouse, int count); // -1 inside the dead zone

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QPointF centre_;
    QStringList labels_;
    QList<QPixmap> icons_;
    int suggested_ = -1, lit_ = -1;
};
