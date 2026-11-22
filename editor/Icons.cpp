// The editor's icons (see Icons.h), each painted on a 100 x 100 canvas that is scaled to the
// requested size. Light comes from the top left: faces turned to it are lighter, the others darker,
// and a gentle shadow sits under every shape so it reads as a solid thing standing on a floor.
#include "Icons.h"

#include <QCoreApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <cmath>
#include <functional>
#include <map>
#include <tuple>

namespace {

using Draw = std::function<void(QPainter&)>;

// One pixmap of the icon at a screen scale (1 = normal, 2 = high-DPI screens).
QPixmap render(int size, qreal scale, const Draw& draw) {
    QPixmap pm(int(size * scale), int(size * scale));
    pm.setDevicePixelRatio(scale);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 100.0, size / 100.0);
    draw(p);
    return pm;
}

// An icon of a family (the functions below), painted once and kept: the scene's list asks for the same dozen at every
// refresh, and painting them anew was a sixth of an API edit's time (7 of 40 samples of scene_move).
enum class Family { Scene, Device, Object, Shape, Role, Control, Collider, Link };

QIcon makeIcon(Family family, int kind, int size, const Draw& draw) {
    static std::map<std::tuple<Family, int, int>, QIcon> kept;
    static const bool cleared = (qAddPostRoutine([] { kept.clear(); }), true); // the pixmaps go before the application does
    (void)cleared;
    const auto [it, fresh] = kept.try_emplace({family, kind, size});
    if (fresh) {
        it->second.addPixmap(render(size, 1.0, draw));
        it->second.addPixmap(render(size, 2.0, draw));
    }
    return it->second;
}

QColor shade(const QColor& c, int factor) { return factor >= 100 ? c.lighter(factor) : c.darker(200 - factor); }

// A blurred contact shadow under a shape: it stands on something.
void shadow(QPainter& p, qreal cx, qreal cy, qreal rx) {
    QRadialGradient g(QPointF(cx, cy), rx);
    g.setColorAt(0, QColor(0, 0, 0, 90));
    g.setColorAt(1, QColor(0, 0, 0, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(QPointF(cx, cy), rx, rx * 0.28);
}

QPen outline(const QColor& c) { return QPen(shade(c, 55), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin); }

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------
void drawCube(QPainter& p, const QColor& c) {
    shadow(p, 50, 89, 40);
    const QPointF top[] = {{50, 12}, {86, 30}, {50, 48}, {14, 30}};
    const QPointF left[] = {{14, 30}, {50, 48}, {50, 88}, {14, 70}};
    const QPointF right[] = {{86, 30}, {50, 48}, {50, 88}, {86, 70}};
    p.setPen(outline(c));
    p.setBrush(shade(c, 135));
    p.drawPolygon(top, 4);
    p.setBrush(c);
    p.drawPolygon(left, 4);
    p.setBrush(shade(c, 70));
    p.drawPolygon(right, 4);
}

void drawSphere(QPainter& p, const QColor& c) {
    shadow(p, 52, 89, 34);
    QRadialGradient g(QPointF(38, 36), 52, QPointF(34, 32));
    g.setColorAt(0.0, shade(c, 165));
    g.setColorAt(0.45, c);
    g.setColorAt(1.0, shade(c, 45));
    p.setPen(outline(c));
    p.setBrush(g);
    p.drawEllipse(QPointF(50, 50), 36, 36);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 150));
    p.drawEllipse(QPointF(37, 34), 8, 5);
}

// A body lit from the left: light at the left third, dark at the right edge.
QLinearGradient sideLight(const QColor& c, qreal x0, qreal x1) {
    QLinearGradient g(QPointF(x0, 0), QPointF(x1, 0));
    g.setColorAt(0.0, shade(c, 110));
    g.setColorAt(0.3, shade(c, 130));
    g.setColorAt(1.0, shade(c, 55));
    return g;
}

void drawCylinder(QPainter& p, const QColor& c) {
    shadow(p, 50, 90, 36);
    QPainterPath body;
    body.moveTo(20, 26);
    body.lineTo(20, 78);
    body.arcTo(QRectF(20, 68, 60, 20), 180, 180);
    body.lineTo(80, 26);
    body.closeSubpath();
    p.setPen(outline(c));
    p.setBrush(sideLight(c, 20, 80));
    p.drawPath(body);
    p.setBrush(shade(c, 140));
    p.drawEllipse(QRectF(20, 16, 60, 20));
}

void drawCone(QPainter& p, const QColor& c) {
    shadow(p, 50, 90, 36);
    QPainterPath body;
    body.moveTo(50, 10);
    body.lineTo(18, 78);
    body.arcTo(QRectF(18, 68, 64, 20), 180, 180);
    body.closeSubpath();
    p.setPen(outline(c));
    p.setBrush(sideLight(c, 18, 82));
    p.drawPath(body);
}

void drawPlane(QPainter& p, const QColor& c) {
    const QPointF top[] = {{8, 58}, {50, 38}, {92, 58}, {50, 78}};
    const QPointF edgeL[] = {{8, 58}, {50, 78}, {50, 84}, {8, 64}};
    const QPointF edgeR[] = {{92, 58}, {50, 78}, {50, 84}, {92, 64}};
    p.setPen(outline(c));
    p.setBrush(shade(c, 60));
    p.drawPolygon(edgeR, 4);
    p.setBrush(shade(c, 85));
    p.drawPolygon(edgeL, 4);
    p.setBrush(shade(c, 125));
    p.drawPolygon(top, 4);
    p.setPen(QPen(shade(c, 90), 1.0));
    for (int k = 1; k < 4; ++k) { // a grid on the top: it is a floor
        const qreal t = k / 4.0;
        p.drawLine(QPointF(8 + 42 * t, 58 - 20 * t), QPointF(50 + 42 * t, 78 - 20 * t));
        p.drawLine(QPointF(8 + 42 * t, 58 + 20 * t), QPointF(50 + 42 * t, 38 + 20 * t));
    }
}

// A model from a file: a faceted low-poly gem, each facet in its own shade.
void drawModel(QPainter& p, const QColor& c) {
    shadow(p, 50, 90, 36);
    QPointF ring[6];
    for (int k = 0; k < 6; ++k) {
        const qreal a = (90 + 60 * k) * 3.14159265 / 180;
        ring[k] = QPointF(50 + 38 * std::cos(a), 52 - 38 * std::sin(a));
    }
    const QPointF centre(50, 46);
    const int shades[6] = {140, 120, 95, 70, 80, 110};
    p.setPen(outline(c));
    for (int k = 0; k < 6; ++k) {
        const QPointF tri[] = {centre, ring[k], ring[(k + 1) % 6]};
        p.setBrush(shade(c, shades[k]));
        p.drawPolygon(tri, 3);
    }
}

QColor shapeColor(ke::ShapeKind k) {
    switch (k) {
    case ke::ShapeKind::Box: return QColor(232, 128, 72);
    case ke::ShapeKind::Sphere: return QColor(78, 150, 235);
    case ke::ShapeKind::Cylinder: return QColor(110, 196, 98);
    case ke::ShapeKind::Cone: return QColor(236, 196, 80);
    case ke::ShapeKind::Plane: return QColor(160, 166, 176);
    case ke::ShapeKind::Mesh: return QColor(170, 120, 220);
    }
    return QColor(180, 180, 180);
}

// ---------------------------------------------------------------------------
// Components: the rigid body
// ---------------------------------------------------------------------------
void drawRigidBlock(QPainter& p) { drawCube(p, QColor(150, 158, 172)); }

// ---------------------------------------------------------------------------
// Controls: one light colour on the dark toolbar, green for "run"
// ---------------------------------------------------------------------------
const QColor kControl(222, 226, 234);

void drawArrowArc(QPainter& p, bool clockwise) {
    p.setPen(QPen(kControl, 9, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    const QRectF r(20, 26, 60, 56);
    p.drawArc(r, (clockwise ? 150 : 30) * 16, (clockwise ? -170 : 170) * 16);
    const QPointF tip = clockwise ? QPointF(76, 42) : QPointF(24, 42);
    const qreal s = clockwise ? 1 : -1;
    const QPointF head[] = {tip + QPointF(s * 8, -16), tip + QPointF(s * 10, 10), tip + QPointF(-s * 14, -2)};
    p.setPen(Qt::NoPen);
    p.setBrush(kControl);
    p.drawPolygon(head, 3);
}

// "Back to the start": a nearly full circle running clockwise, its head pointing up at the left.
void drawCircleArrow(QPainter& p) {
    p.setPen(QPen(kControl, 9, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(20, 20, 60, 60), 100 * 16, -280 * 16);
    const QPointF head[] = {{21, 30}, {10, 48}, {32, 48}};
    p.setPen(Qt::NoPen);
    p.setBrush(kControl);
    p.drawPolygon(head, 3);
}

void drawEye(QPainter& p, bool open) {
    QPainterPath almond;
    almond.moveTo(8, 50);
    almond.cubicTo(28, 22, 72, 22, 92, 50);
    almond.cubicTo(72, 78, 28, 78, 8, 50);
    p.setPen(QPen(kControl, 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(almond);
    p.setPen(Qt::NoPen);
    p.setBrush(kControl);
    p.drawEllipse(QPointF(50, 50), 13, 13);
    if (!open) {
        p.setPen(QPen(QColor(236, 96, 96), 8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(16, 84), QPointF(84, 16));
    }
}

void drawLock(QPainter& p, bool closed) {
    p.setPen(QPen(kControl, 8, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    QPainterPath shackle;
    shackle.moveTo(32, 46);
    shackle.lineTo(32, 32);
    shackle.arcTo(QRectF(32, 14, 36, 36), 180, -180);
    shackle.lineTo(68, closed ? 46 : 34);
    if (!closed) shackle.translate(12, -6);
    p.drawPath(shackle);
    p.setPen(Qt::NoPen);
    p.setBrush(kControl);
    p.drawRoundedRect(QRectF(22, 46, 56, 42), 8, 8);
    p.setBrush(QColor(60, 64, 72));
    p.drawEllipse(QPointF(50, 64), 6, 6);
}

// The edit tools: a cursor arrow, a cross of four arrows, a turning arrow, a growing square.
void drawCursor(QPainter& p) {
    const QPointF arrow[] = {{30, 12}, {30, 80}, {46, 64}, {58, 90}, {70, 84}, {58, 58}, {80, 58}};
    p.setPen(QPen(QColor(40, 44, 52), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(kControl);
    p.drawPolygon(arrow, 7);
}

void drawMoveCross(QPainter& p) {
    p.setPen(QPen(kControl, 7, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(50, 20), QPointF(50, 80));
    p.drawLine(QPointF(20, 50), QPointF(80, 50));
    p.setPen(Qt::NoPen);
    p.setBrush(kControl);
    const QPointF heads[4][3] = {{{50, 6}, {38, 22}, {62, 22}}, {{50, 94}, {38, 78}, {62, 78}},
                                 {{6, 50}, {22, 38}, {22, 62}}, {{94, 50}, {78, 38}, {78, 62}}};
    for (const auto& h : heads) p.drawPolygon(h, 3);
}

void drawScaleSquares(QPainter& p) {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(kControl, 5, Qt::DashLine, Qt::RoundCap));
    p.drawRect(QRectF(14, 14, 72, 72));
    p.setPen(Qt::NoPen);
    p.setBrush(kControl);
    p.drawRect(QRectF(14, 54, 32, 32));
    p.setPen(QPen(kControl, 7, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(52, 48), QPointF(76, 24));
    p.setPen(Qt::NoPen);
    const QPointF head[] = {{84, 16}, {80, 38}, {62, 20}};
    p.drawPolygon(head, 3);
}

// A ruler laid aslant, its marks along its upper edge, long and short in turns: the tool that measures point to point.
void drawRuler(QPainter& p) {
    p.save();
    p.translate(50, 50);
    p.rotate(-40);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(kControl, 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawRoundedRect(QRectF(-46, -15, 92, 30), 5, 5);
    p.setPen(QPen(kControl, 5, Qt::SolidLine, Qt::RoundCap));
    for (int k = -3; k <= 3; ++k) p.drawLine(QPointF(k * 11.5, -15), QPointF(k * 11.5, k % 2 ? -5 : 3));
    p.restore();
}

// A window outline with its area: filled while open, only the dividing line while closed. The menu
// button shows a side bar beside two stacked panes.
void drawLayout(QPainter& p, LayoutIcon kind, bool open) {
    const QRectF frame(10, 18, 80, 64);
    QPainterPath outline;
    outline.addRoundedRect(frame, 9, 9);
    const QPen pen(kControl, 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QRectF area;
    QLineF divider;
    switch (kind) {
    case LayoutIcon::LeftBar: area = QRectF(10, 18, 28, 64), divider = QLineF(38, 18, 38, 82); break;
    case LayoutIcon::RightBar: area = QRectF(62, 18, 28, 64), divider = QLineF(62, 18, 62, 82); break;
    case LayoutIcon::BottomPanel: area = QRectF(10, 58, 80, 24), divider = QLineF(10, 58, 90, 58); break;
    default: break;
    }
    if (open && !area.isEmpty()) {
        p.save();
        p.setClipPath(outline);
        p.fillRect(area, kControl);
        p.restore();
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(pen);
    p.drawPath(outline);
    if (kind == LayoutIcon::Customize) {
        p.drawLine(QPointF(38, 18), QPointF(38, 82));
        p.drawLine(QPointF(38, 50), QPointF(90, 50));
    } else if (!open) {
        p.drawLine(divider);
    }
}

void drawControl(QPainter& p, ControlIcon c) {
    p.setPen(Qt::NoPen);
    switch (c) {
    case ControlIcon::Play: {
        p.setBrush(QColor(88, 200, 110));
        const QPointF tri[] = {{28, 16}, {84, 50}, {28, 84}};
        p.drawPolygon(tri, 3);
        break;
    }
    case ControlIcon::Pause:
        p.setBrush(QColor(240, 190, 80));
        p.drawRoundedRect(QRectF(24, 18, 18, 64), 4, 4);
        p.drawRoundedRect(QRectF(58, 18, 18, 64), 4, 4);
        break;
    case ControlIcon::Record: // ▶ and ● together: it plays and it records
    case ControlIcon::Recording: { // the dot lit and ringed, in turns with Record: a blink
        p.setBrush(QColor(88, 200, 110));
        const QPointF tri[] = {{8, 20}, {54, 50}, {8, 80}};
        p.drawPolygon(tri, 3);
        const bool lit = c == ControlIcon::Recording;
        if (lit) {
            p.setBrush(QColor(255, 120, 110, 110));
            p.drawEllipse(QPointF(72, 50), 27, 27);
        }
        p.setBrush(lit ? QColor(255, 70, 60) : QColor(214, 58, 58));
        p.drawEllipse(QPointF(72, 50), 19, 19);
        break;
    }
    case ControlIcon::Stop:
        p.setBrush(QColor(236, 96, 96));
        p.drawRoundedRect(QRectF(22, 22, 56, 56), 7, 7);
        break;
    case ControlIcon::ToolSelect: drawCursor(p); break;
    case ControlIcon::ToolMove: drawMoveCross(p); break;
    case ControlIcon::ToolRotate: drawCircleArrow(p); break;
    case ControlIcon::ToolScale: drawScaleSquares(p); break;
    case ControlIcon::ToolRuler: drawRuler(p); break;
    case ControlIcon::Step: {
        p.setBrush(kControl);
        const QPointF tri[] = {{20, 18}, {66, 50}, {20, 82}};
        p.drawPolygon(tri, 3);
        p.drawRoundedRect(QRectF(70, 18, 12, 64), 3, 3);
        break;
    }
    case ControlIcon::Reset: drawCircleArrow(p); break;
    case ControlIcon::Undo: drawArrowArc(p, false); break;
    case ControlIcon::Redo: drawArrowArc(p, true); break;
    case ControlIcon::Visible: drawEye(p, true); break;
    case ControlIcon::Hidden: drawEye(p, false); break;
    case ControlIcon::Locked: drawLock(p, true); break;
    case ControlIcon::Unlocked: drawLock(p, false); break;
    case ControlIcon::Count: break;
    }
}


// ---------------------------------------------------------------------------
// Colliders: thin pale-green wireframes, the colour the viewport draws them in
// ---------------------------------------------------------------------------
const QColor kGuide(124, 255, 154);

QPen guidePen(qreal width = 3.2) { return QPen(kGuide, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin); }

// Every stroke twice: a dark one under a green one, so the glyph reads on light and dark buttons.
void guideStroke(QPainter& p, const std::function<void()>& draw) {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 110), 5.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    draw();
    p.setPen(guidePen());
    draw();
}

void drawWireCube(QPainter& p) {
    guideStroke(p, [&p] {
        const QPointF a[] = {{22, 34}, {58, 34}, {58, 76}, {22, 76}};
        const QPointF b[] = {{42, 20}, {78, 20}, {78, 62}, {42, 62}};
        p.drawPolygon(a, 4);
        p.drawPolygon(b, 4);
        for (int k = 0; k < 4; ++k) p.drawLine(a[k], b[k]);
    });
}

void drawWireSphere(QPainter& p) {
    guideStroke(p, [&p] {
        p.drawEllipse(QPointF(50, 50), 34, 34);
        p.drawEllipse(QPointF(50, 50), 34, 12);
        p.drawEllipse(QPointF(50, 50), 12, 34);
    });
}

void drawWireCapsule(QPainter& p) {
    guideStroke(p, [&p] {
        p.drawRoundedRect(QRectF(30, 10, 40, 80), 20, 20);
        p.drawEllipse(QPointF(50, 30), 20, 6);
        p.drawEllipse(QPointF(50, 70), 20, 6);
    });
}

// A round cylinder standing up: two rims and the two sides (a wheel's exact collider).
void drawWireCylinder(QPainter& p) {
    guideStroke(p, [&p] {
        p.drawEllipse(QPointF(50, 22), 28, 9);
        p.drawEllipse(QPointF(50, 78), 28, 9);
        p.drawLine(QPointF(22, 22), QPointF(22, 78));
        p.drawLine(QPointF(78, 22), QPointF(78, 78));
    });
}

void drawWireHull(QPainter& p) {
    guideStroke(p, [&p] {
        const QPointF outer[] = {{50, 10}, {84, 32}, {78, 72}, {46, 90}, {16, 66}, {20, 28}};
        p.drawPolygon(outer, 6);
        p.drawLine(QPointF(50, 10), QPointF(52, 52));
        p.drawLine(QPointF(52, 52), QPointF(78, 72));
        p.drawLine(QPointF(52, 52), QPointF(16, 66));
    });
}

// Авто: the collider follows the shape - a wire cube with a small spark in its corner.
void drawWireAuto(QPainter& p) {
    drawWireCube(p);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 214, 90));
    const QPointF star[] = {{84, 64}, {88, 76}, {98, 80}, {88, 84}, {84, 96}, {80, 84}, {70, 80}, {80, 76}};
    p.drawPolygon(star, 8);
}

Draw colliderDraw(ke::ColliderKind k) {
    switch (k) {
    case ke::ColliderKind::Auto: return drawWireAuto;
    case ke::ColliderKind::Box: return drawWireCube;
    case ke::ColliderKind::Sphere: return drawWireSphere;
    case ke::ColliderKind::Capsule: return drawWireCapsule;
    case ke::ColliderKind::ConvexHull: return drawWireHull;
    case ke::ColliderKind::Cylinder: return drawWireCylinder;
    }
    return [](QPainter&) {};
}

// The collider as a component: a grey block inside a green wire cage.
void drawColliderRole(QPainter& p) {
    p.save();
    p.translate(50, 50);
    p.scale(0.55, 0.55);
    p.translate(-50, -50);
    drawRigidBlock(p);
    p.restore();
    guideStroke(p, [&p] { p.drawRoundedRect(QRectF(12, 12, 76, 76), 10, 10); });
}

Draw roleDraw(RoleIcon r) {
    switch (r) {
    case RoleIcon::Rigid: return drawRigidBlock;
    case RoleIcon::Collider: return drawColliderRole;
    case RoleIcon::Count: break;
    }
    return [](QPainter&) {};
}

Draw shapeDraw(ke::ShapeKind k) {
    const QColor c = shapeColor(k);
    switch (k) {
    case ke::ShapeKind::Box: return [c](QPainter& p) { drawCube(p, c); };
    case ke::ShapeKind::Sphere: return [c](QPainter& p) { drawSphere(p, c); };
    case ke::ShapeKind::Cylinder: return [c](QPainter& p) { drawCylinder(p, c); };
    case ke::ShapeKind::Cone: return [c](QPainter& p) { drawCone(p, c); };
    case ke::ShapeKind::Plane: return [c](QPainter& p) { drawPlane(p, c); };
    case ke::ShapeKind::Mesh: return [c](QPainter& p) { drawModel(p, c); };
    }
    return [](QPainter&) {};
}

// ---------------------------------------------------------------------------
// Many objects: a group and the three patterns of an array
// ---------------------------------------------------------------------------
// A small shaded cube with its centre at (x, y), `size` wide (the big cube scaled down).
void smallCube(QPainter& p, qreal x, qreal y, qreal size, const QColor& c) {
    p.save();
    p.translate(x - size / 2, y - size / 2);
    p.scale(size / 100.0, size / 100.0);
    drawCube(p, c);
    p.restore();
}

void drawObjectIcon(QPainter& p, ObjectIcon kind) {
    const QColor c(236, 150, 88);
    switch (kind) {
    case ObjectIcon::Group: // three cubes and a dashed frame around them
        p.setPen(QPen(QColor(222, 226, 234), 3, Qt::DashLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(8, 12, 84, 78), 10, 10);
        smallCube(p, 34, 64, 40, QColor(90, 150, 234));
        smallCube(p, 66, 64, 40, QColor(110, 196, 98));
        smallCube(p, 50, 36, 40, c);
        break;
    case ObjectIcon::ArrayLine:
        for (int i = 0; i < 4; ++i) smallCube(p, 14 + 24 * i, 52, 30, c);
        break;
    case ObjectIcon::ArrayGrid:
        for (int i = 0; i < 9; ++i) smallCube(p, 22 + 28 * (i % 3), 22 + 28 * (i / 3), 26, c);
        break;
    case ObjectIcon::ArrayCircle:
        for (int i = 0; i < 8; ++i) {
            const qreal a = 6.2831853 * i / 8;
            smallCube(p, 50 + 34 * std::cos(a), 50 + 34 * std::sin(a), 22, c);
        }
        break;
    case ObjectIcon::Count: break;
    }
}

// ---------------------------------------------------------------------------
// Lights and cameras
// ---------------------------------------------------------------------------
// The sun: a warm disc and eight rays.
void drawSun(QPainter& p) {
    p.setPen(QPen(QColor(255, 196, 64), 7, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 8; ++i) {
        const qreal a = 6.2831853 * i / 8, c = std::cos(a), s = std::sin(a);
        p.drawLine(QPointF(50 + 31 * c, 50 + 31 * s), QPointF(50 + 44 * c, 50 + 44 * s));
    }
    QRadialGradient g(QPointF(43, 43), 30);
    g.setColorAt(0, QColor(255, 247, 190));
    g.setColorAt(1, QColor(250, 170, 40));
    p.setPen(outline(QColor(250, 170, 40)));
    p.setBrush(g);
    p.drawEllipse(QPointF(50, 50), 22, 22);
}

// A lamp: a glowing glass bulb on a threaded base.
void drawBulb(QPainter& p) {
    QRadialGradient glow(QPointF(50, 40), 46);
    glow.setColorAt(0, QColor(255, 230, 120, 110));
    glow.setColorAt(1, QColor(255, 230, 120, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(QPointF(50, 40), 46, 46);
    QRadialGradient glass(QPointF(42, 30), 34);
    glass.setColorAt(0, QColor(255, 255, 235));
    glass.setColorAt(1, QColor(255, 206, 80));
    QPainterPath bulb;
    bulb.moveTo(38, 66);
    bulb.cubicTo(38, 56, 22, 50, 22, 36);
    bulb.cubicTo(22, 20, 36, 10, 50, 10);
    bulb.cubicTo(64, 10, 78, 20, 78, 36);
    bulb.cubicTo(78, 50, 62, 56, 62, 66);
    bulb.closeSubpath();
    p.setPen(outline(QColor(230, 170, 60)));
    p.setBrush(glass);
    p.drawPath(bulb);
    p.setBrush(QColor(150, 158, 172));
    p.setPen(outline(QColor(150, 158, 172)));
    p.drawRoundedRect(QRectF(37, 66, 26, 20), 4, 4);
    p.drawLine(QPointF(38, 73), QPointF(62, 73));
    p.drawLine(QPointF(38, 80), QPointF(62, 80));
    p.drawEllipse(QPointF(50, 89), 5, 3);
}

// A spotlight: a dark lamp head at the top left, its cone of light falling to a bright ellipse.
void drawSpotlight(QPainter& p) {
    QPainterPath cone;
    cone.moveTo(30, 30);
    cone.lineTo(14, 84);
    cone.arcTo(QRectF(14, 74, 76, 20), 180, 180);
    cone.lineTo(46, 22);
    cone.closeSubpath();
    QLinearGradient light(QPointF(38, 26), QPointF(52, 90));
    light.setColorAt(0, QColor(255, 226, 120, 220));
    light.setColorAt(1, QColor(255, 226, 120, 60));
    p.setPen(Qt::NoPen);
    p.setBrush(light);
    p.drawPath(cone);
    p.setBrush(QColor(255, 240, 170, 200));
    p.drawEllipse(QRectF(14, 74, 76, 20));
    const QPointF head[] = {{24, 8}, {50, 2}, {54, 24}, {26, 34}};
    p.setPen(outline(QColor(120, 128, 142)));
    p.setBrush(QColor(120, 128, 142));
    p.drawPolygon(head, 4);
}

// A camera: a body, two film reels on top, a lens in front.
void drawCameraIcon(QPainter& p) {
    const QColor body(98, 108, 124), dark(62, 68, 80);
    p.setPen(outline(body));
    p.setBrush(dark);
    p.drawEllipse(QPointF(30, 26), 14, 14);
    p.drawEllipse(QPointF(58, 26), 14, 14);
    QLinearGradient g(QPointF(0, 40), QPointF(0, 82));
    g.setColorAt(0, shade(body, 130));
    g.setColorAt(1, shade(body, 75));
    p.setBrush(g);
    p.drawRoundedRect(QRectF(10, 40, 62, 42), 7, 7);
    const QPointF lens[] = {{72, 52}, {94, 40}, {94, 82}, {72, 70}};
    p.setBrush(dark);
    p.drawPolygon(lens, 4);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(120, 190, 255));
    p.drawEllipse(QPointF(22, 51), 4, 4);
}

// A solid arrow from a to b, lit across its width; `depth`: a darker copy below it, the arrow's thickness.
void drawSolidArrow(QPainter& p, QPointF a, QPointF b, qreal shaft, qreal headWidth, qreal headLength, const QColor& c, qreal depth) {
    const QPointF d = b - a;
    const qreal length = std::hypot(d.x(), d.y());
    const QPointF u = d / length, n(-u.y(), u.x()), neck = b - u * headLength;
    QPainterPath path;
    path.moveTo(a + n * shaft / 2);
    for (const QPointF& q : {neck + n * shaft / 2, neck + n * headWidth / 2, b, neck - n * headWidth / 2, neck - n * shaft / 2, a - n * shaft / 2}) path.lineTo(q);
    path.closeSubpath();
    p.setPen(outline(c));
    p.setBrush(shade(c, 60));
    p.drawPath(path.translated(depth * 0.6, depth));
    QLinearGradient g(a + n * (length * 0.12), a - n * (length * 0.12));
    g.setColorAt(0, shade(c, 150));
    g.setColorAt(0.5, c);
    g.setColorAt(1, shade(c, 75));
    p.setBrush(g);
    p.drawPath(path);
}

// A flash: a star of `spikes` points around c, between the radii r0 and r1.
QPainterPath flash(QPointF c, qreal r0, qreal r1, int spikes, qreal turn) {
    QPainterPath path;
    for (int i = 0; i < spikes * 2; ++i) {
        const qreal a = turn + M_PI * i / spikes, r = i % 2 ? r0 : (i % 4 ? r1 : r1 * 0.86);
        const QPointF q = c + QPointF(std::cos(a), std::sin(a)) * r;
        i ? path.lineTo(q) : path.moveTo(q);
    }
    path.closeSubpath();
    return path;
}

const QColor kForceColour(150, 104, 240), kFlashColour(250, 150, 40);

// «Сила»: a flash and an arrow out of it - a blow and the way it pushes.
void drawForce(QPainter& p) {
    shadow(p, 50, 90, 36);
    p.setPen(outline(kFlashColour));
    p.setBrush(kFlashColour);
    p.drawPath(flash({40, 58}, 17, 33, 8, 0.3));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(252, 210, 70));
    p.drawPath(flash({40, 58}, 10, 21, 8, 0.7));
    drawSolidArrow(p, {36, 64}, {90, 14}, 15, 34, 24, kForceColour, 4);
}

// A directional force: one thick arrow rising to the upper right.
void drawForceArrow(QPainter& p) {
    shadow(p, 52, 90, 34);
    drawSolidArrow(p, {20, 82}, {82, 18}, 20, 42, 30, kForceColour, 5);
}

// A radial force: a core with four arrows out of it.
void drawForceBall(QPainter& p) {
    shadow(p, 50, 91, 34);
    for (const QPointF& d : {QPointF(0, -1), QPointF(0, 1), QPointF(-1, 0), QPointF(1, 0)}) drawSolidArrow(p, QPointF(50, 50) + d * 14, QPointF(50, 50) + d * 44, 11, 25, 15, kForceColour, 2);
    QRadialGradient g(QPointF(44, 42), 28, QPointF(41, 40));
    g.setColorAt(0.0, shade(kFlashColour, 165));
    g.setColorAt(0.45, kFlashColour);
    g.setColorAt(1.0, shade(kFlashColour, 45));
    p.setPen(outline(kFlashColour));
    p.setBrush(g);
    p.drawEllipse(QPointF(50, 50), 19, 19);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 150));
    p.drawEllipse(QPointF(43, 42), 4.2, 2.7);
}

// An encoder: a code disc with its slots round the rim and the read head over them.
void drawEncoderIcon(QPainter& p) {
    const QColor disc(190, 196, 206), dark(58, 62, 72);
    p.setPen(outline(disc));
    p.setBrush(disc);
    p.drawEllipse(QPointF(46, 54), 36, 36);
    p.setPen(QPen(dark, 5, Qt::SolidLine, Qt::FlatCap));
    for (int k = 0; k < 16; ++k) {
        const qreal a = k * 3.14159265 / 8;
        p.drawLine(QPointF(46 + 26 * std::cos(a), 54 + 26 * std::sin(a)), QPointF(46 + 34 * std::cos(a), 54 + 34 * std::sin(a)));
    }
    p.setPen(outline(dark));
    p.setBrush(dark);
    p.drawEllipse(QPointF(46, 54), 7, 7);
    p.setBrush(QColor(240, 150, 60));
    p.drawRoundedRect(QRectF(68, 8, 24, 30), 4, 4);
}

// An IMU: a chip with its pins and the three axes it measures, x red, y green, z blue.
void drawImuIcon(QPainter& p) {
    const QColor chip(96, 104, 120);
    p.setPen(QPen(QColor(200, 200, 205), 3));
    for (int k = 0; k < 4; ++k) {
        p.drawLine(QPointF(32 + 10 * k, 30), QPointF(32 + 10 * k, 22));
        p.drawLine(QPointF(32 + 10 * k, 70), QPointF(32 + 10 * k, 78));
    }
    p.setPen(outline(chip));
    p.setBrush(chip);
    p.drawRoundedRect(QRectF(24, 30, 48, 40), 4, 4);
    const QPointF o(48, 50);
    const QColor axes[3] = {QColor(235, 80, 70), QColor(90, 210, 90), QColor(80, 140, 240)};
    const QPointF ends[3] = {{92, 50}, {48, 6}, {14, 84}};
    for (int k = 0; k < 3; ++k) {
        p.setPen(QPen(axes[k], 5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(o, ends[k]);
    }
}

// A sonar: the HC-SR04's blue board with its two silver transducers, an echo arc in front.
void drawSonarIcon(QPainter& p) {
    const QColor board(40, 110, 200), metal(205, 210, 218);
    p.setPen(outline(board));
    p.setBrush(board);
    p.drawRoundedRect(QRectF(6, 34, 88, 44), 5, 5);
    p.setPen(outline(metal));
    p.setBrush(metal);
    p.drawEllipse(QPointF(30, 56), 15, 15);
    p.drawEllipse(QPointF(70, 56), 15, 15);
    p.setBrush(QColor(70, 74, 82));
    p.drawEllipse(QPointF(30, 56), 7, 7);
    p.drawEllipse(QPointF(70, 56), 7, 7);
    p.setPen(QPen(QColor(120, 220, 140), 4, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(20, 4, 60, 40), 30 * 16, 120 * 16);
}

// A touch pad: a fingertip pressing a pad, the force's arrow into it.
void drawTouchIcon(QPainter& p) {
    const QColor pad(230, 120, 70), skin(225, 190, 160);
    p.setPen(outline(pad));
    p.setBrush(pad);
    p.drawRoundedRect(QRectF(10, 66, 80, 18), 6, 6);
    p.setPen(outline(skin));
    p.setBrush(skin);
    p.drawRoundedRect(QRectF(30, 14, 40, 54), 18, 18);
    p.setPen(QPen(QColor(120, 220, 140), 5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(84, 18), QPointF(84, 56));
    p.drawLine(QPointF(84, 56), QPointF(76, 46));
    p.drawLine(QPointF(84, 56), QPointF(92, 46));
}

// A force-torque sensor: the flange's disc, its three axes and the torque's arc.
void drawWrenchIcon(QPainter& p) {
    const QColor disc(150, 158, 172);
    p.setPen(outline(disc));
    p.setBrush(disc);
    p.drawEllipse(QPointF(50, 52), 30, 30);
    const QPointF o(50, 52);
    const QColor axes[3] = {QColor(235, 80, 70), QColor(90, 210, 90), QColor(80, 140, 240)};
    const QPointF ends[3] = {{94, 52}, {50, 8}, {18, 84}};
    for (int k = 0; k < 3; ++k) {
        p.setPen(QPen(axes[k], 5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(o, ends[k]);
    }
    p.setPen(QPen(QColor(240, 200, 70), 4, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(14, 16, 72, 72), 100 * 16, 120 * 16);
}

// A drive's effort: a motor's can and the turning arrow of its torque.
void drawEffortIcon(QPainter& p) {
    const QColor can(90, 98, 114), shaft(205, 210, 218);
    p.setPen(outline(can));
    p.setBrush(can);
    p.drawRoundedRect(QRectF(12, 30, 52, 50), 8, 8);
    p.setPen(outline(shaft));
    p.setBrush(shaft);
    p.drawRect(QRectF(64, 50, 22, 10));
    p.setPen(QPen(QColor(240, 200, 70), 5, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(58, 26, 34, 58), -60 * 16, 220 * 16);
    p.drawLine(QPointF(86, 76), QPointF(94, 66));
}

// A stereo camera: one dark bar, two lenses on it far apart - a ZED's face - and the dots of its projector.
void drawStereoIcon(QPainter& p) {
    const QColor bar(60, 66, 78), glass(80, 140, 240);
    p.setPen(outline(bar));
    p.setBrush(bar);
    p.drawRoundedRect(QRectF(4, 32, 92, 36), 16, 16);
    for (const qreal x : {24.0, 76.0}) {
        p.setPen(outline(QColor(205, 210, 218)));
        p.setBrush(QColor(205, 210, 218));
        p.drawEllipse(QPointF(x, 50), 13, 13);
        p.setPen(Qt::NoPen);
        p.setBrush(glass);
        p.drawEllipse(QPointF(x, 50), 8, 8);
    }
    p.setBrush(QColor(235, 80, 70));
    for (int k = 0; k < 3; ++k) p.drawEllipse(QPointF(42 + 8 * k, 50), 2.5, 2.5);
}

// A joint: two plates on a pin - a hinge with no motor.
void drawJointIcon(QPainter& p) {
    const QColor plate(150, 158, 172), pin(240, 150, 60);
    p.setPen(outline(plate));
    p.setBrush(plate);
    p.drawRoundedRect(QRectF(8, 40, 44, 22), 5, 5);
    p.save();
    p.translate(56, 51);
    p.rotate(-35);
    p.drawRoundedRect(QRectF(0, -11, 40, 22), 5, 5);
    p.restore();
    p.setPen(outline(pin));
    p.setBrush(pin);
    p.drawEllipse(QPointF(54, 51), 9, 9);
}

// The magnet's elements, drawn on the 64 x 64 canvas the other icons use: two plates and a pin (a turn, with an arc
// of motion), a rail with a block on it (a travel, with an arrow), a cylinder on an axle (a wheel), a seam with
// rivets (a weld), a socket with a ball (a ball joint).
void drawRotaryIcon(QPainter& p) {
    drawJointIcon(p);
    p.setPen(QPen(QColor(240, 150, 60), 3, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(30, 27, 48, 48), 40 * 16, 70 * 16);
}

void drawLinearIcon(QPainter& p) {
    const QColor rail(150, 158, 172), block(240, 150, 60);
    p.setPen(outline(rail));
    p.setBrush(rail);
    p.drawRoundedRect(QRectF(6, 36, 52, 10), 4, 4);
    p.setPen(outline(block));
    p.setBrush(block);
    p.drawRoundedRect(QRectF(14, 22, 22, 24), 4, 4);
    p.setPen(QPen(block, 3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(40, 14), QPointF(56, 14));
    p.drawLine(QPointF(50, 8), QPointF(56, 14));
    p.drawLine(QPointF(50, 20), QPointF(56, 14));
}

void drawWheelIcon(QPainter& p) {
    const QColor tyre(70, 74, 82), hub(240, 150, 60), axle(150, 158, 172);
    p.setPen(outline(axle));
    p.setBrush(axle);
    p.drawRoundedRect(QRectF(8, 29, 48, 6), 3, 3);
    p.setPen(outline(tyre));
    p.setBrush(tyre);
    p.drawEllipse(QPointF(32, 32), 22, 22);
    p.setPen(outline(hub));
    p.setBrush(hub);
    p.drawEllipse(QPointF(32, 32), 7, 7);
}

void drawWeldIcon(QPainter& p) {
    const QColor plate(150, 158, 172), seam(240, 150, 60);
    p.setPen(outline(plate));
    p.setBrush(plate);
    p.drawRoundedRect(QRectF(6, 24, 24, 16), 4, 4);
    p.drawRoundedRect(QRectF(34, 24, 24, 16), 4, 4);
    p.setPen(QPen(seam, 4, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(32, 14), QPointF(32, 50));
    p.setPen(outline(seam));
    p.setBrush(seam);
    for (int y : {20, 32, 44}) p.drawEllipse(QPointF(32, y), 3.5, 3.5);
}

void drawBallIcon(QPainter& p) {
    const QColor socket(150, 158, 172), ball(240, 150, 60);
    p.setPen(outline(socket));
    p.setBrush(socket);
    QPainterPath cup;
    cup.moveTo(10, 30);
    cup.arcTo(QRectF(10, 10, 44, 44), 180, 180);
    cup.lineTo(44, 54);
    cup.lineTo(20, 54);
    cup.closeSubpath();
    p.drawPath(cup);
    p.setPen(outline(ball));
    p.setBrush(ball);
    p.drawEllipse(QPointF(32, 30), 13, 13);
    p.setPen(QPen(ball, 5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(32, 30), QPointF(46, 10));
}

Draw linkDraw(LinkIcon k) {
    switch (k) {
    case LinkIcon::Rotary: return drawRotaryIcon;
    case LinkIcon::Linear: return drawLinearIcon;
    case LinkIcon::Wheel: return drawWheelIcon;
    case LinkIcon::Weld: return drawWeldIcon;
    default: return drawBallIcon;
    }
}

// A battery: a cell lying down, its positive cap, its charge in green.
void drawBatteryIcon(QPainter& p) {
    const QColor shell(70, 76, 88);
    p.setPen(outline(shell));
    p.setBrush(shell);
    p.drawRoundedRect(QRectF(6, 28, 80, 46), 7, 7);
    p.drawRect(QRectF(86, 40, 8, 22));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(90, 210, 110));
    p.drawRoundedRect(QRectF(13, 35, 50, 32), 4, 4);
}

// A motor driver: a red board with an H - the bridge of four switches.
void drawDriverIcon(QPainter& p) {
    const QColor board(190, 50, 50);
    p.setPen(outline(board));
    p.setBrush(board);
    p.drawRoundedRect(QRectF(8, 16, 84, 68), 6, 6);
    p.setPen(QPen(QColor(245, 240, 230), 8, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(32, 32), QPointF(32, 68));
    p.drawLine(QPointF(68, 32), QPointF(68, 68));
    p.drawLine(QPointF(32, 50), QPointF(68, 50));
}

// A controller: a game pad - its body with two grips, a cross on the left, four buttons on the right.
void drawControllerIcon(QPainter& p) {
    const QColor body(70, 80, 96);
    p.setPen(outline(body));
    p.setBrush(body);
    QPainterPath pad;
    pad.addRoundedRect(QRectF(10, 28, 80, 40), 18, 18);
    pad.addEllipse(QRectF(8, 44, 30, 36));
    pad.addEllipse(QRectF(62, 44, 30, 36));
    p.drawPath(pad.simplified());
    p.setPen(QPen(QColor(235, 238, 242), 7, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(22, 50), QPointF(38, 50));
    p.drawLine(QPointF(30, 42), QPointF(30, 58));
    p.setPen(Qt::NoPen);
    const QColor buttons[] = {QColor(90, 200, 120), QColor(230, 80, 80), QColor(80, 150, 240), QColor(240, 200, 70)};
    const QPointF at[] = {{70, 58}, {78, 50}, {62, 50}, {70, 42}};
    for (int k = 0; k < 4; ++k) p.setBrush(buttons[k]), p.drawEllipse(at[k], 4.5, 4.5);
}

// Any sensor: a sensor's board - the chip with its legs and the window it senses through. No one device stands for all
// of them (the sonar's two round eyes on this button read as a face), and no waves: they read as a radio sign.
void drawSensorIcon(QPainter& p) {
    shadow(p, 50, 91, 40);
    const QColor board(38, 128, 96), chip(52, 58, 70), window(90, 175, 255);
    QLinearGradient g(QPointF(12, 18), QPointF(88, 86));
    g.setColorAt(0, shade(board, 135));
    g.setColorAt(1, shade(board, 76));
    p.setPen(outline(board));
    p.setBrush(g);
    p.drawRoundedRect(QRectF(10, 20, 80, 64), 8, 8);
    p.setPen(QPen(QColor(205, 210, 218), 4, Qt::SolidLine, Qt::RoundCap));
    for (int k = 0; k < 4; ++k) {
        p.drawLine(QPointF(34 + 10.5 * k, 28), QPointF(34 + 10.5 * k, 33));
        p.drawLine(QPointF(34 + 10.5 * k, 71), QPointF(34 + 10.5 * k, 76));
    }
    QLinearGradient c(QPointF(26, 33), QPointF(74, 71));
    c.setColorAt(0, shade(chip, 130));
    c.setColorAt(1, shade(chip, 85));
    p.setPen(outline(chip));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(26, 33, 48, 38), 5, 5);
    QRadialGradient glass(QPointF(47, 49), 10);
    glass.setColorAt(0, shade(window, 160));
    glass.setColorAt(1, shade(window, 75));
    p.setPen(Qt::NoPen);
    p.setBrush(glass);
    p.drawEllipse(QPointF(50, 52), 8.5, 8.5);
    p.setBrush(QColor(255, 255, 255, 210));
    p.drawEllipse(QPointF(47.2, 49.2), 2, 2);
}

void drawDeviceIcon(QPainter& p, DeviceIcon kind) {
    switch (kind) {
    case DeviceIcon::Encoder: drawEncoderIcon(p); break;
    case DeviceIcon::Imu: drawImuIcon(p); break;
    case DeviceIcon::Sonar: drawSonarIcon(p); break;
    case DeviceIcon::Touch: drawTouchIcon(p); break;
    case DeviceIcon::Wrench: drawWrenchIcon(p); break;
    case DeviceIcon::Effort: drawEffortIcon(p); break;
    case DeviceIcon::Stereo: drawStereoIcon(p); break;
    case DeviceIcon::Battery: drawBatteryIcon(p); break;
    case DeviceIcon::Driver: drawDriverIcon(p); break;
    case DeviceIcon::Joint: drawJointIcon(p); break;
    case DeviceIcon::Controller: drawControllerIcon(p); break;
    case DeviceIcon::Sensor: drawSensorIcon(p); break;
    case DeviceIcon::Count: break;
    }
}

void drawSceneIcon(QPainter& p, SceneIcon kind) {
    switch (kind) {
    case SceneIcon::Sun: drawSun(p); break;
    case SceneIcon::Bulb: drawBulb(p); break;
    case SceneIcon::Spot: drawSpotlight(p); break;
    case SceneIcon::Camera: drawCameraIcon(p); break;
    case SceneIcon::Force: drawForce(p); break;
    case SceneIcon::ForceArrow: drawForceArrow(p); break;
    case SceneIcon::ForceBall: drawForceBall(p); break;
    case SceneIcon::Count: break;
    }
}

} // namespace

QIcon sceneIcon(SceneIcon kind, int size) {
    return makeIcon(Family::Scene, int(kind), size, [kind](QPainter& p) { drawSceneIcon(p, kind); });
}

QIcon deviceIcon(DeviceIcon kind, int size) {
    return makeIcon(Family::Device, int(kind), size, [kind](QPainter& p) { drawDeviceIcon(p, kind); });
}

QIcon objectIcon(ObjectIcon kind, int size) {
    return makeIcon(Family::Object, int(kind), size, [kind](QPainter& p) { drawObjectIcon(p, kind); });
}

QIcon shapeIcon(ke::ShapeKind shape, int size) { return makeIcon(Family::Shape, int(shape), size, shapeDraw(shape)); }
QIcon roleIcon(RoleIcon role, int size) { return makeIcon(Family::Role, int(role), size, roleDraw(role)); }
QIcon controlIcon(ControlIcon control, int size) {
    return makeIcon(Family::Control, int(control), size, [control](QPainter& p) { drawControl(p, control); });
}
QIcon layoutIcon(LayoutIcon kind, int size) {
    QIcon icon;
    for (const bool open : {false, true})
        for (const qreal scale : {1.0, 2.0})
            icon.addPixmap(render(size, scale, [kind, open](QPainter& p) { drawLayout(p, kind, open); }), QIcon::Normal,
                           open ? QIcon::On : QIcon::Off);
    return icon;
}
QPixmap rolePixmap(RoleIcon role, int size) { return render(size, 2.0, roleDraw(role)); }
QIcon colliderIcon(ke::ColliderKind kind, int size) { return makeIcon(Family::Collider, int(kind), size, colliderDraw(kind)); }
QIcon linkIcon(LinkIcon kind, int size) { return makeIcon(Family::Link, int(kind), size, linkDraw(kind)); }
QPixmap linkPixmap(LinkIcon kind, int size) { return render(size, 2.0, linkDraw(kind)); }

