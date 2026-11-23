// Making things with the mouse (CreateTool, ViewportTools.h), pressed the way a person does it, step by
// step: a box by three clicks (a corner, the opposite corner, the height), the hint naming each step and
// the size; one scene.add(...) line in the console, one undo; a box dragged on the top of the first; a click
// on the same spot waits, Backspace goes a step back, Esc drops what was begun and then leaves; a sphere by
// a drag; a cylinder snapped by Ctrl; a lamp placed and raised; a spot and a camera placed, raised and
// aimed; a right click and a tool of the shelf leave the mode. Each check prints one line.
#include "SelfTestSupport.h"
#include "scene/Assembly.h"

#include "EditorOps.h"
#include "MainWindow.h"

#include <QDockWidget>
#include <QKeyEvent>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QJsonArray>

#include <cmath>

using namespace ke;
using namespace selftest;

namespace {

QPointF onScreen(Viewport* v, const Vector3& p) { return toPoint(v->gizmoView().project(p)); }

// «Фигура ▾» or «Свет ▾» → its k-th item, as the menu is clicked.
void choose(QMainWindow& w, int k, const char* button = "shapesButton") {
    auto* menuButton = w.findChild<QToolButton*>(button);
    if (menuButton && menuButton->menu() && k < menuButton->menu()->actions().size()) menuButton->menu()->actions()[k]->trigger();
    pump(30);
}

void key(Viewport* v, int k) {
    QKeyEvent press(QEvent::KeyPress, k, Qt::NoModifier);
    QCoreApplication::sendEvent(v, &press);
    pump(30);
}

// The mouse (no button) over the world points a .. b in steps.
void glide(Viewport* v, const Vector3& a, const Vector3& b, Qt::KeyboardModifiers m = Qt::NoModifier) {
    for (int i = 1; i <= 8; ++i) sendMouse(v, QEvent::MouseMove, onScreen(v, a + (b - a) * (float(i) / 8.0f)), Qt::NoButton, Qt::NoButton, m);
    pump(20);
}

// Where a light shines, or a camera looks: its -y, or its -z, turned by its rotation.
Vector3 looking(const SceneObject& o, const Vector3& local) { return objectRotation(o).rotate(local); }

// The left button pressed over world point a, moved over b in steps, released there, the keys held.
void dragOver(Viewport* v, const Vector3& a, const Vector3& b, Qt::KeyboardModifiers m = Qt::NoModifier) {
    sendMouse(v, QEvent::MouseButtonPress, onScreen(v, a), Qt::LeftButton, Qt::LeftButton, m);
    for (int i = 1; i <= 10; ++i) sendMouse(v, QEvent::MouseMove, onScreen(v, a + (b - a) * (float(i) / 10.0f)), Qt::NoButton, Qt::LeftButton, m);
    sendMouse(v, QEvent::MouseButtonRelease, onScreen(v, b), Qt::LeftButton, Qt::NoButton, m);
    pump(30);
}

void clickAt(Viewport* v, const QPointF& p, Qt::MouseButton button = Qt::LeftButton, Qt::KeyboardModifiers m = Qt::NoModifier) {
    sendMouse(v, QEvent::MouseButtonPress, p, button, button, m);
    sendMouse(v, QEvent::MouseButtonRelease, p, button, Qt::NoButton, m);
    pump(30);
}

// The height phase: the mouse (no button) up the vertical through `corner` to `height`, then a click there.
void raiseAndClick(Viewport* v, const Vector3& corner, float height, Qt::KeyboardModifiers m = Qt::NoModifier) {
    const Vector3 top = corner + Vector3(0.0f, height, 0.0f);
    for (int i = 1; i <= 8; ++i) sendMouse(v, QEvent::MouseMove, onScreen(v, corner + (top - corner) * (float(i) / 8.0f)), Qt::NoButton, Qt::NoButton, m);
    clickAt(v, onScreen(v, top), Qt::LeftButton, m);
}

bool near(const Vector3& a, const Vector3& b) { return length(a - b) < 1e-4f; }

// The console's lines that make a shape.
QStringList addLines(QPlainTextEdit* log) {
    QStringList out;
    for (const QString& line : log->toPlainText().split('\n'))
        if (line.contains("scene.add(")) out << line;
    return out;
}

} // namespace

int runCreateTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    Checker c;
    auto* dock = w.findChild<QDockWidget*>("consoleDock");
    auto* log = dock ? dock->findChild<QPlainTextEdit*>() : nullptr;
    if (!log) return c.check(false, "the window has its console"), c.failures;
    b.newScene();
    v->camera().lookAt(QVector3D(1.2f, 1.4f, 1.8f), QVector3D(0.0f, 0.1f, 0.0f));
    pump(150);
    log->clear();
    choose(w, 0); // Куб
    c.check(v->createMode() && v->createWhat().shape == ShapeKind::Box && v->cursor().shape() == Qt::CrossCursor &&
                v->mouseHint().contains("шаг 1/3") && v->mouseHint().contains("щелчок — готовая фигура 0.2 м") && v->mouseHint().contains("Esc") &&
                v->mouseHint().contains("Shift"),
            "«Фигура ▾» → «Куб» starts making a box: a cross cursor, the hint says a click puts a ready 0.2 m one, a drag draws it");

    // A click: a ready 0.2 m box standing there, selected, and the mode is over - a newcomer is not left in the tool.
    const size_t before = b.graph().entities.size();
    const Vector3 a(-0.3f, 0.0f, 0.1f);
    clickAt(v, onScreen(v, a));
    const Entity& box = last(b);
    const uint32_t first = box.id;
    const QStringList lines = addLines(log);
    std::printf("  a click made: %s at (%.3f, %.3f, %.3f) size (%.3f, %.3f, %.3f); the console: %s\n", box.name.c_str(), double(box.position.x),
                double(box.position.y), double(box.position.z), double(box.size.x), double(box.size.y), double(box.size.z),
                qPrintable(lines.join(" | ")));
    c.check(b.graph().entities.size() == before + 1 && box.shape == ShapeKind::Box && near(box.size, Vector3(0.2f)) &&
                length(box.position - Vector3(-0.3f, 0.1f, 0.1f)) < 2e-3f && entityIsGeometryOnly(box) && b.selectedId() == first && !v->createMode(),
            "a click makes a ready 0.2 m box standing on the floor there, selected; the mode is over");
    c.check(lines.size() == 1 && lines[0].contains("scene.add(\"box\", position=(") && lines[0].contains("size=(0.2, 0.2, 0.2))"),
            "the console shows it as one line: scene.add(\"box\", position=..., size=...)");
    b.undoAction()->trigger();
    pump(60);
    c.check(b.graph().entities.size() == before && !entity(b, first), "one undo takes the box away");
    b.redoAction()->trigger();
    pump(60);
    c.check(entity(b, first), "redo brings it back");

    // A drag with Shift: the footprint 0.4 x 0.3 m, then its height 0.2 m with a click - each step named; Shift keeps the mode.
    choose(w, 0);
    const Vector3 a2(0.6f, 0.0f, 0.0f), corner = a2 + Vector3(0.4f, 0.0f, 0.3f), top = corner + Vector3(0.0f, 0.2f, 0.0f); // clear of the later ones
    sendMouse(v, QEvent::MouseButtonPress, onScreen(v, a2), Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    for (int i = 1; i <= 10; ++i) sendMouse(v, QEvent::MouseMove, onScreen(v, a2 + (corner - a2) * (float(i) / 10.0f)), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
    const QString base = v->mouseHint();
    sendMouse(v, QEvent::MouseButtonRelease, onScreen(v, corner), Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    pump(30);
    glide(v, corner, top, Qt::ShiftModifier);
    const QString rising = v->mouseHint();
    if (!dir.isEmpty()) windowShot(w).save(dir + "/create-height.png");
    std::printf("  base: %s\n  rising: %s\n", qPrintable(base), qPrintable(rising));
    c.check(base.contains("шаг 2/3") && base.contains("0.40 × 0.30 м") && rising.contains("шаг 3/3") && rising.contains("высота 0.20 м"),
            "a drag draws it step by step: 2/3 the footprint with its size, 3/3 its height");
    clickAt(v, onScreen(v, top), Qt::LeftButton, Qt::ShiftModifier);
    const Entity& drawn = last(b);
    c.check(drawn.id != first && near(drawn.size, Vector3(0.4f, 0.2f, 0.3f)) && near(drawn.position, Vector3(0.8f, 0.1f, 0.15f)) &&
                b.selectedId() == drawn.id && v->createMode() && v->mouseHint().contains("шаг 1/3"),
            "the drawn box is 0.4 x 0.3 x 0.2 m on the floor, selected; with Shift the mode stays for one more");

    // On the top of the first box: a box dragged out there stands on its top face.
    const Vector3 onTop(-0.35f, 0.2f, 0.05f), onTopCorner = onTop + Vector3(0.1f, 0.0f, 0.1f);
    dragOver(v, onTop, onTopCorner, Qt::ShiftModifier);
    raiseAndClick(v, onTopCorner, 0.1f, Qt::ShiftModifier);
    const Entity& upper = last(b);
    const Entity* lower = entity(b, first);
    const float gap = lower ? (upper.position.y - 0.5f * upper.size.y) - (lower->position.y + 0.5f * lower->size.y) : 1.0f;
    c.check(upper.id != drawn.id && near(upper.size, Vector3(0.1f, 0.1f, 0.1f)) && std::fabs(gap) < 2e-3f,
            "a box dragged out on the top of another stands on its top face");

    // Backspace goes a step back, Esc drops the thing begun - the mode stays -, Esc again leaves.
    const size_t made = b.graph().entities.size();
    const Vector3 spot(0.5f, 0.0f, 0.5f);
    dragOver(v, spot, spot + Vector3(0.2f, 0.0f, 0.2f), Qt::ShiftModifier);
    const bool rising2 = v->mouseHint().contains("шаг 3/3");
    key(v, Qt::Key_Backspace);
    const bool back = v->mouseHint().contains("шаг 2/3");
    key(v, Qt::Key_Escape);
    const bool dropped = v->createMode() && v->mouseHint().contains("шаг 1/3");
    key(v, Qt::Key_Escape);
    c.check(rising2 && back && dropped && !v->createMode() && b.graph().entities.size() == made,
            "Backspace goes a step back; Esc drops the box begun and the mode stays; Esc with nothing begun leaves");

    // A sphere: «Сфера», a drag of 0.1 m from its centre.
    choose(w, 1);
    const Vector3 centre(0.5f, 0.0f, -0.3f);
    dragOver(v, centre, centre + Vector3(0.1f, 0.0f, 0.0f));
    const Entity& ball = last(b);
    std::printf("  a sphere dragged: %s %s at (%.3f, %.3f, %.3f) size %.3f, selected %s, mode %s\n", ball.name.c_str(),
                ball.shape == ShapeKind::Sphere ? "sphere" : "not a sphere", double(ball.position.x), double(ball.position.y), double(ball.position.z),
                double(ball.size.x), b.selectedId() == ball.id ? "yes" : "no", v->createMode() ? "on" : "off");
    c.check(ball.shape == ShapeKind::Sphere && near(ball.size, Vector3(0.2f)) && near(ball.position, Vector3(0.5f, 0.1f, -0.3f)) &&
                b.selectedId() == ball.id && !v->createMode(),
            "a sphere: its centre and a drag of 0.1 m make a ball of radius 0.1 m on the floor, selected; the mode over");

    // A cylinder with Ctrl held: its centre, radius and height on the 5 cm grid.
    choose(w, 2);
    const Vector3 axis(-0.61f, 0.0f, -0.38f), rim = axis + Vector3(0.12f, 0.0f, 0.0f);
    dragOver(v, axis, rim, Qt::ControlModifier);
    raiseAndClick(v, rim, 0.27f, Qt::ControlModifier);
    const Entity& drum = last(b);
    c.check(drum.shape == ShapeKind::Cylinder && near(drum.size, Vector3(0.2f, 0.25f, 0.2f)) && near(drum.position, Vector3(-0.6f, 0.125f, -0.4f)),
            "Ctrl snaps a cylinder's centre, radius and height to 5 cm");

    // A lamp: its place on the floor, 1 m up, a click.
    choose(w, 1, "lightsButton");
    const size_t lights = b.graph().lights.size();
    const Vector3 lampFoot(0.3f, 0.0f, -0.5f);
    clickAt(v, onScreen(v, lampFoot));
    raiseAndClick(v, lampFoot, 1.0f);
    const Light* lamp = b.graph().lights.size() == lights + 1 ? &b.graph().lights.back() : nullptr;
    std::printf("  a lamp: %zu new, at (%.3f, %.3f, %.3f), mode %s\n", b.graph().lights.size() - lights, lamp ? double(lamp->position.x) : 0.0,
                lamp ? double(lamp->position.y) : 0.0, lamp ? double(lamp->position.z) : 0.0, v->createMode() ? "on" : "off");
    c.check(!v->createMode() && lamp && lamp->kind == LightKind::Point &&
                length(lamp->position - Vector3(0.3f, 1.0f, -0.5f)) < 0.02f && b.selectedId() == lamp->id &&
                log->toPlainText().contains("scene.add_light(\"point\", position=(0.3, 1, -0.5))"),
            "a lamp: a click on the floor, the mouse up 1 m, a click - it hangs there; one scene.add_light line");

    // A spot: its place, 1.2 m up, then aimed at a point of the floor.
    choose(w, 2, "lightsButton");
    const Vector3 spotFoot(-0.5f, 0.0f, 0.5f), lit(0.2f, 0.0f, 0.1f);
    clickAt(v, onScreen(v, spotFoot));
    raiseAndClick(v, spotFoot, 1.2f);
    glide(v, spotFoot + Vector3(0.0f, 1.2f, 0.0f), lit);
    const QString aiming = v->mouseHint();
    if (!dir.isEmpty()) { // seen from further away for the picture: the spot's cone reaches the floor
        const QVector3D eye = v->camera().eye(), centre = v->camera().target();
        v->camera().lookAt(QVector3D(2.6f, 2.2f, 3.2f), QVector3D(0.0f, 0.4f, 0.0f));
        glide(v, lit, lit);
        windowShot(w).save(dir + "/create-spot.png");
        v->camera().lookAt(eye, centre);
        glide(v, lit, lit);
    }
    clickAt(v, onScreen(v, lit));
    const Light& spotlight = b.graph().lights.back();
    const Vector3 shine = looking(spotlight, Vector3(0.0f, -1.0f, 0.0f)), wanted = normalize(lit - spotlight.position);
    std::printf("  spot at (%.3f, %.3f, %.3f) shining (%.3f, %.3f, %.3f); %s\n", double(spotlight.position.x), double(spotlight.position.y),
                double(spotlight.position.z), double(shine.x), double(shine.y), double(shine.z), qPrintable(aiming));
    c.check(spotlight.kind == LightKind::Spot && length(spotlight.position - Vector3(-0.5f, 1.2f, 0.5f)) < 0.02f && dot(shine, wanted) > 0.999f &&
                aiming.contains("шаг 3/3") && aiming.contains("куда светит"),
            "a spot: a place, 1.2 m up, a click on the floor where it shines - it shines there");

    // A camera: the toolbar's «Камера», a place, 0.8 m up, aimed at the first box.
    if (QAction* cam = w.findChild<QAction*>("create_camera")) cam->trigger();
    pump(30);
    const size_t cameras = b.graph().cameras.size();
    const Vector3 camFoot(0.6f, 0.0f, 0.9f), seen(-0.1f, 0.2f, 0.25f);
    clickAt(v, onScreen(v, camFoot));
    raiseAndClick(v, camFoot, 0.8f);
    glide(v, camFoot + Vector3(0.0f, 0.8f, 0.0f), seen);
    if (!dir.isEmpty()) {
        const QVector3D eye = v->camera().eye(), centre = v->camera().target();
        v->camera().lookAt(QVector3D(2.6f, 2.2f, 3.2f), QVector3D(0.0f, 0.4f, 0.0f));
        glide(v, seen, seen);
        windowShot(w).save(dir + "/create-camera.png");
        v->camera().lookAt(eye, centre);
        glide(v, seen, seen);
    }
    clickAt(v, onScreen(v, seen));
    const Camera* camera = b.graph().cameras.size() == cameras + 1 ? &b.graph().cameras.back() : nullptr;
    const Vector3 view = camera ? looking(*camera, Vector3(0.0f, 0.0f, -1.0f)) : Vector3(0.0f);
    c.check(camera && length(camera->position - Vector3(0.6f, 0.8f, 0.9f)) < 0.02f && dot(view, normalize(seen - camera->position)) > 0.995f &&
                log->toPlainText().contains("scene.add_camera(eye=("),
            "a camera: a place, 0.8 m up, a click on the box - it looks at the box; one scene.add_camera line");

    // Leaving the mode: Esc with nothing begun; a right click; a tool of the shelf.
    key(v, Qt::Key_Escape);
    const bool escaped = !v->createMode() && v->cursor().shape() == Qt::ArrowCursor;
    choose(w, 3); // Конус
    const bool cone = v->createMode() && v->createWhat().shape == ShapeKind::Cone;
    clickAt(v, onScreen(v, Vector3(-0.6f, 0.0f, -0.4f)), Qt::RightButton);
    const bool rightOut = !v->createMode();
    choose(w, 2); // Цилиндр
    trigger(w, "toolMove");
    pump(30);
    const size_t now = b.graph().entities.size();
    clickAt(v, onScreen(v, Vector3(-0.6f, 0.0f, -0.4f)));
    c.check(escaped && cone && rightOut && !v->createMode() && b.graph().entities.size() == now,
            "Esc with nothing begun, a right click and the shelf's Move leave the mode: a click then makes nothing");

    // «Сустав»: a block and a bar lying against it; the mouse on the block's top edge where the bar touches - the guess is a
    // hinge along that edge (a lid's) - a click there, a click on the bar: the bar (the smaller) hangs on the block about the
    // edge, free; the tool is over. Then a corner: a ball.
    b.newScene();
    EditorOps* ops = dynamic_cast<MainWindow&>(w).operators();
    const auto put = [ops](Vector3 at, Vector3 size) {
        return uint32_t(ops->runNow("scene_add", {{"shape", "box"}, {"role", "rigid"}, {"position", QJsonArray{at.x, at.y, at.z}},
                                                  {"size", QJsonArray{size.x, size.y, size.z}}}).value("id").toInt());
    };
    const uint32_t block = put({0, 0.1f, 0}, {0.4f, 0.2f, 0.4f}), bar = put({0.4f, 0.15f, 0}, {0.4f, 0.1f, 0.1f});
    v->camera().lookAt(QVector3D(0.9f, 0.7f, 0.9f), QVector3D(0.3f, 0.1f, 0.0f));
    pump(100);
    trigger(w, "jointTool");
    const auto join = [&](Vector3 first, Vector3 second) {
        sendMouse(v, QEvent::MouseMove, onScreen(v, first), Qt::NoButton, Qt::NoButton); // near the edge, not on it: the snap finds it
        pump(30);
        clickAt(v, onScreen(v, first));
        sendMouse(v, QEvent::MouseMove, onScreen(v, second), Qt::NoButton, Qt::NoButton);
        pump(30);
        clickAt(v, onScreen(v, second));
        pump(30);
    };
    join({0.19f, 0.2f, 0.01f}, {0.5f, 0.2f, 0});
    const QJsonArray joints = ops->runNow("scene_list", {{"kind", "joint"}}).value("objects").toArray();
    const QString text = joints.isEmpty() ? QString() : ops->runNow("scene_get", {{"id", joints[0].toObject().value("id").toInt()}}).value("text").toString();
    std::printf("  «Сустав» at the block's edge, then the bar:\n%s", qPrintable(text));
    const ke::JointObject* hinge = nullptr;
    for (const ke::JointObject& j : b.graph().joints)
        if (!joints.isEmpty() && j.id == uint32_t(joints[0].toObject().value("id").toInt())) hinge = &j;
    const auto* jointTool = w.findChild<QAction*>("jointTool");
    c.check(hinge && hinge->a == bar && hinge->b == block && near(hinge->anchorB, Vector3(0.2f, 0.1f, 0.0f)) && text.contains("free rx") &&
                jointTool && !jointTool->isChecked(),
            "«Сустав»: a click at the block's edge and one on the bar - the bar hangs on the block about that edge, free; the tool is over");

    trigger(w, "jointTool");
    join({-0.19f, 0.2f, -0.19f}, {0.5f, 0.2f, 0});
    const QJsonArray two = ops->runNow("scene_list", {{"kind", "joint"}}).value("objects").toArray();
    const QString cornerJoint = two.size() < 2 ? QString() : ops->runNow("scene_get", {{"id", two[1].toObject().value("id").toInt()}}).value("text").toString();
    std::printf("  «Сустав» at the block's corner:\n%s", qPrintable(cornerJoint));
    c.check(cornerJoint.contains(QRegularExpression("free[^\\n]*rx")) && cornerJoint.contains(QRegularExpression("free[^\\n]*ry")) &&
                cornerJoint.contains(QRegularExpression("free[^\\n]*rz")) && cornerJoint.contains("anchor_b -0.2 0.1 -0.2"),
            "«Сустав» at a corner guesses a ball at the corner: turns free every way");

    // The edge is caught along its whole length: the mouse near its end, not its middle, is still the edge's hinge - there.
    trigger(w, "jointTool");
    join({0.19f, 0.2f, 0.17f}, {0.5f, 0.2f, 0});
    const QJsonArray three = ops->runNow("scene_list", {{"kind", "joint"}}).value("objects").toArray();
    const QString endJoint = three.size() < 3 ? QString() : ops->runNow("scene_get", {{"id", three[2].toObject().value("id").toInt()}}).value("text").toString();
    std::printf("  «Сустав» near the end of the block's edge:\n%s", qPrintable(endJoint));
    c.check(endJoint.contains(QRegularExpression("anchor_b 0\\.2 0\\.1 0\\.1[5-9]")) && endJoint.contains("free rx") && !endJoint.contains("free ry"),
            "«Сустав» near an edge's end catches the edge: a hinge along it, where the mouse points (by the end, not the middle)");

    // Fusion's Joint: a bar lying apart, a click on the block's edge, a click on a snap point of the bar - the bar comes to the
    // edge, face to face, and hangs there; one Ctrl+Z takes back the joint and the move together.
    b.newScene();
    const uint32_t post = put({0, 0.2f, 0}, {0.4f, 0.4f, 0.4f}), loose = put({0.9f, 0.05f, 0.6f}, {0.4f, 0.1f, 0.1f});
    ops->runNow("scene_set", {{"id", int(post)}, {"line", "rigid"}, {"key", "fixed"}, {"values", QJsonArray{1}}});
    v->camera().lookAt(QVector3D(1.6f, 1.4f, 2.0f), QVector3D(0.4f, 0.2f, 0.2f));
    pump(100);
    const Vector3 apart = entity(b, loose)->position;
    trigger(w, "jointTool");
    join({0.19f, 0.4f, 0.0f}, {0.9f, 0.1f, 0.6f}); // the block's top edge, then the bar's top face's centre
    const Vector3 came = entity(b, loose)->position;
    const size_t aligned = b.graph().joints.size();
    std::printf("  aligned: the bar from (%.3f %.3f %.3f) to (%.3f %.3f %.3f), %zu joint\n", double(apart.x), double(apart.y), double(apart.z),
                double(came.x), double(came.y), double(came.z), aligned);
    ops->runNow("edit_undo", {});
    const bool undone = near(entity(b, loose)->position, apart) && b.graph().joints.empty();
    c.check(aligned == 1 && length(came - apart) > 0.3f && came.y > 0.39f && undone,
            "«Сустав» onto a snap point of a part lying apart brings it there face to face and joins it; one Ctrl+Z takes both back");

    // The menu's other kinds: 5 - a cylinder joint (turns and slides along its axis).
    trigger(w, "jointTool");
    key(v, Qt::Key_5);
    join({0.19f, 0.4f, 0.0f}, {0.9f, 0.1f, 0.6f});
    const QString cylinder = b.graph().joints.empty() ? QString() : ops->runNow("scene_get", {{"id", int(b.graph().joints[0].id)}}).value("text").toString();
    std::printf("  key 5, a cylinder joint:\n%s", qPrintable(cylinder));
    c.check(cylinder.contains(QRegularExpression("free[^\\n]*rx")) && cylinder.contains(QRegularExpression("free[^\\n]*tx")),
            "«Сустав» 5: a cylinder joint - it turns and slides along one axis");

    // The card's «Указать в виде…»: the joint selected, the button, one click on the block's top edge by its far end - the joint
    // moves there, the links stay; one undo step.
    const uint32_t cyl = b.graph().joints.empty() ? 0 : b.graph().joints[0].id;
    b.select(cyl);
    pump(60);
    const Vector3 loosePos = entity(b, loose)->position;
    if (auto* pick = w.findChild<QPushButton*>("jointPick")) pick->click();
    pump(30);
    sendMouse(v, QEvent::MouseMove, onScreen(v, {-0.15f, 0.4f, 0.19f}), Qt::NoButton, Qt::NoButton);
    pump(30);
    clickAt(v, onScreen(v, {-0.15f, 0.4f, 0.19f})); // near the block's top edge at z = 0.2, by its -x end
    pump(30);
    const QString moved = ops->runNow("scene_get", {{"id", int(cyl)}}).value("text").toString();
    std::printf("  the cylinder joint put again by «Указать в виде…»:\n%s", qPrintable(moved));
    c.check(moved.contains(QRegularExpression("anchor_b -0\\.1[0-9]* 0\\.2 0\\.2 ")) && near(entity(b, loose)->position, loosePos),
            "«Указать в виде…» on a joint's card: a click on an edge puts the joint there, the links stay");

    // 8: a shock absorber - a slider whose spring and damper carry the moving part's weight.
    b.newScene();
    const uint32_t frame = put({0, 0.2f, 0}, {0.4f, 0.4f, 0.4f}), hub = put({0.3f, 0.2f, 0}, {0.2f, 0.1f, 0.1f});
    ops->runNow("scene_set", {{"id", int(frame)}, {"line", "rigid"}, {"key", "fixed"}, {"values", QJsonArray{1}}});
    v->camera().lookAt(QVector3D(1.2f, 1.0f, 1.4f), QVector3D(0.15f, 0.2f, 0.0f));
    pump(100);
    trigger(w, "jointTool");
    key(v, Qt::Key_8);
    join({0.2f, 0.25f, 0.15f}, {0.3f, 0.25f, 0.0f});
    const QString shock = b.graph().joints.empty() ? QString() : ops->runNow("scene_get", {{"id", int(b.graph().joints[0].id)}}).value("text").toString();
    std::printf("  key 8, a shock absorber:\n%s", qPrintable(shock));
    ke::Vector3 shockAt, shockAxis;
    if (!b.graph().joints.empty()) ke::jointWorldFrame(b.graph(), b.graph().joints[0], shockAt, shockAxis);
    c.check(shock.contains("Амортизатор") && shock.contains(QRegularExpression("drive t[xyz] stiffness [1-9]")) && hub &&
                std::fabs(shockAxis.y) > 0.99f,
            "«Сустав» 8: a shock absorber - a slider with a spring that carries the part, its travel upright");
    b.newScene();
    pump(60);
    return c.failures;
}
