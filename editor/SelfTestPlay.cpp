// The play-mode pass (MainWindowPlay.cpp, SimulationBar.h), checked the way a person meets it: the top bar never
// cuts a word at 1280, 1600 and 1920 px; an object looks the same in play as in edit (smooth, not
// darker); mode and pose saving stay outside the viewport; K keeps what the simulation did after
// Stop, and one Ctrl+Z takes it back; transport stays in the toolbar; Ctrl+K finds and runs a command;
// orbiting marks its pivot; a blank frame is recognised. Each check prints one line.
#include "SelfTestSupport.h"

#include "CommandSearch.h"
#include "MainWindow.h"
#include "SimulationBar.h"

#include <QAction>
#include <QFrame>
#include <QDockWidget>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QToolBar>

#include <algorithm>
#include <cmath>

using namespace ke;
using namespace selftest;

namespace {

// Every button of the top bar shows all of itself: nothing hidden behind », no caption cut.
bool toolbarFits(QMainWindow& w, int& cut) {
    auto* bar = w.findChild<QToolBar*>("createBar");
    if (!bar) return false;
    cut = 0;
    for (QToolButton* b : bar->findChildren<QToolButton*>()) {
        if (b->objectName() == "qt_toolbar_ext_button") {
            cut += b->isVisible() ? 1 : 0;
            continue;
        }
        if (!b->isVisible()) continue;
        const bool captioned = b->toolButtonStyle() != Qt::ToolButtonIconOnly;
        const int textWidth = captioned ? b->fontMetrics().horizontalAdvance(b->text()) : 0;
        if (b->width() + 1 < b->sizeHint().width() || textWidth > b->width()) {
            ++cut;
            std::printf("    clipped «%s»: width %d, needed %d, text %d\n", qPrintable(b->text()), b->width(), b->sizeHint().width(), textWidth);
        }
    }
    return cut == 0;
}

void testToolbarWidths(Checker& c, QMainWindow& w) {
    const QSize before = w.size();
    for (int width : {1280, 1600, 1920}) {
        w.resize(width, 900);
        pump(300);
        int cut = 0;
        const bool fits = toolbarFits(w, cut);
        std::printf("  window %d px wide (asked %d): %d buttons cut or hidden\n", w.width(), width, cut);
        c.check(fits, width == 1280 ? "at 1280 px no top-bar button is cut or hidden (words never elided)"
                                    : "at this width no top-bar button is cut or hidden");
    }
    w.resize(before);
    pump(300);
}

// The mean colour of a small square, and the largest jump between neighbours along its middle row.
struct Patch {
    double r = 0, g = 0, b = 0;
    int step = 0;
};
Patch patch(const QImage& img, QPoint c, int half, int rowHalf) {
    Patch p;
    int n = 0;
    for (int y = c.y() - half; y <= c.y() + half; ++y)
        for (int x = c.x() - half; x <= c.x() + half; ++x) {
            const QRgb q = img.pixel(x, y);
            p.r += qRed(q), p.g += qGreen(q), p.b += qBlue(q), ++n;
        }
    p.r /= n, p.g /= n, p.b /= n;
    for (int x = c.x() - rowHalf; x < c.x() + rowHalf; ++x) {
        const QRgb a = img.pixel(x, c.y()), b = img.pixel(x + 1, c.y());
        p.step = std::max(p.step, std::abs(qGray(a) - qGray(b)));
    }
    return p;
}

// One fixed sphere, seen from the same eye in edit mode and paused right after ▶.
void testSameLook(Checker& c, SceneBuilder& b, Viewport* v, const QString& dir) {
    b.newScene();
    b.createActions()[1]->trigger(); // Сфера
    const uint32_t id = last(b).id;
    b.editEntity(id, [](Entity& e) { e.size = Vector3(0.6f); e.position = Vector3(0, 0.3f, 0); e.rigid.enabled = true; e.rigid.fixed = true; });
    b.select(0);
    b.frameAll();
    pump(400);
    const Vector2 s = v->gizmoView().project(Vector3(0, 0.3f, 0));
    const float r = length(v->gizmoView().project(Vector3(0.3f, 0.3f, 0)) - s);
    const QPoint centre(int(s.x), int(s.y));
    const QImage edit = v->grabFramebuffer();
    b.play();
    b.pause();
    waitFrames(v, 1, 5000);
    pump(300);
    const QImage play = v->grabFramebuffer();
    if (!dir.isEmpty()) edit.save(dir + "/look-edit.png"), play.save(dir + "/look-play.png");
    const int half = std::max(2, int(r * 0.15f)), row = std::max(4, int(r * 0.6f));
    const Patch a = patch(edit, centre, half, row), p = patch(play, centre, half, row);
    const double diff = std::max({std::fabs(a.r - p.r), std::fabs(a.g - p.g), std::fabs(a.b - p.b)}) / std::max({a.r, a.g, a.b, 1.0});
    std::printf("  sphere centre: edit rgb (%.0f %.0f %.0f), play rgb (%.0f %.0f %.0f), difference %.1f %%; largest step "
                "along its middle row: edit %d, play %d\n", a.r, a.g, a.b, p.r, p.g, p.b, 100 * diff, a.step, p.step);
    c.check(diff < 0.05, "a sphere looks as bright in play as in edit (difference under 5 %)");
    c.check(p.step <= a.step + 6, "a sphere is as smooth in play as in edit (no facet edges)");
    b.stop();
    pump(200);
}

void testPlayBar(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    auto* bar = w.findChild<SimulationBar*>("simulationBar");
    auto* keep = bar ? bar->findChild<QPushButton*>("keepButton") : nullptr;
    b.newScene(); pump(100);
    auto* graphs = w.findChild<QAction*>("actionGraphs");
    const bool hadGraphs = graphs && graphs->isChecked();
    if (graphs) graphs->setChecked(true); // hold other docks fixed when measuring mode changes
    if (auto* dock = w.findChild<QDockWidget*>("resultsDock")) w.resizeDocks({dock}, {210}, Qt::Vertical);
    pump(200);
    c.check(bar && bar->isVisible() && keep && !keep->isVisible() && !v->playFrame(),
            "edit mode: persistent status row, pose saving hidden");
    if (!bar || !keep) return;
    c.check(!v->findChild<QFrame*>("playBanner") && !v->findChild<QFrame*>("bigControls"),
            "no play banner or duplicate transport buttons cover the 3D view");
    const QSize before = w.size();
    const int editHeight = v->height();
    trigger(w, "actionPlay"); pump(200);
    std::printf("  viewport height: edit %d, play %d (charts already visible)\n", editHeight, v->height());
    c.check(b.mode() == SceneBuilder::Mode::Playing && v->playFrame() && keep->isVisible() && keep->isEnabled() &&
            bar->findChild<QLabel*>("simulationMode")->text() == "Игра" && v->height() == editHeight,
            "toolbar Play updates the external status row and pose saving action");
    for (int width : {1024, 1280, 1600, 1920}) {
        w.resize(width, 900); pump(150);
        const QRect scene(v->mapToGlobal(QPoint()), v->size());
        const QRect chrome(bar->mapToGlobal(QPoint()), bar->size());
        c.check(!scene.intersects(chrome) && keep->width() >= keep->sizeHint().width(),
                "status row stays outside the viewport; saving button fits after resizing");
    }
    w.resize(before); pump(100);
    if (!dir.isEmpty()) windowShot(w).save(dir + "/simulation-bar.png");
    trigger(w, "actionPause"); pump(150);
    c.check(b.mode() == SceneBuilder::Mode::Paused && bar->findChild<QLabel*>("simulationMode")->text() == "Пауза",
            "toolbar Pause updates the external status label");
    trigger(w, "actionStop"); pump(150);
    c.check(b.mode() == SceneBuilder::Mode::Edit && !v->playFrame() && !keep->isVisible() && v->height() == editHeight,
            "toolbar Stop restores edit status without changing the viewport height");
    if (graphs) graphs->setChecked(hadGraphs);
}

// A cube dropped from 1 m: K after it landed, Stop - it stays down; Ctrl+Z - up again.
void testKeep(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    b.createActions()[0]->trigger();
    const uint32_t id = last(b).id;
    b.editEntity(id, [](Entity& e) { e.position = Vector3(0, 1.0f, 0); e.rigid.enabled = true; e.collider.enabled = true; });
    const Entity& floor = b.graph().entities.front();
    const float rest = floor.position.y + 0.5f * floor.size.y + 0.5f * last(b).size.y; // on the floor's top
    b.select(0);
    b.play();
    waitFrames(v, 150, 30000);
    b.pause();
    pump(200);
    float live = -1; // where the body is now, as the viewer shows it
    if (auto s = v->snapshot(); s && b.bodyOfEntity(id) >= 0) live = s->bodies[size_t(b.bodyOfEntity(id))].pos.y;
    if (auto* keep = w.findChild<QPushButton*>("keepButton")) keep->click();
    else c.check(false, "pose saving button exists outside the viewport");
    pump(600); // to the simulation's thread and back
    b.stop();
    pump(300);
    const float kept = last(b).position.y;
    std::printf("  cube dropped from 1.00 m: the body lies at %.4f m, after K and Stop the cube stands at %.4f m "
                "(on the floor: %.4f m)\n", live, kept, rest);
    c.check(std::fabs(kept - live) < 1e-3f && std::fabs(kept - rest) < 5e-3f, "K keeps the landed pose after Stop (to 1 mm)");
    b.undoAction()->trigger();
    pump(100);
    std::printf("  Ctrl+Z: %.3f m\n", last(b).position.y);
    c.check(std::fabs(last(b).position.y - 1.0f) < 1e-4f, "one Ctrl+Z after Stop puts it back up");
}

void testCommandSearch(Checker& c, QMainWindow& w, SceneBuilder& b, const QString& dir) {
    b.newScene();
    std::vector<uint32_t> ids;
    for (int i = 0; i < 2; ++i) {
        b.createActions()[0]->trigger();
        ids.push_back(last(b).id);
        b.editEntity(ids.back(), [i](Entity& e) { e.position = Vector3(0.6f * float(i), 0.1f, 0); });
    }
    b.selectMany(ids);
    trigger(w, "commandSearch");
    pump(100);
    auto* search = w.findChild<CommandSearch*>();
    if (!search) return c.check(false, "Ctrl+K opens the command search");
    search->setQuery("груп");
    pump(100);
    const std::vector<QAction*> found = search->results();
    const QString first = found.empty() ? QString() : found[0]->text();
    std::printf("  «груп»: %zu commands, first «%s»\n", found.size(), qPrintable(first));
    if (!dir.isEmpty()) { // the popup is a window of its own: laid over the window's picture where it is
        QImage shot = windowShot(w);
        QPainter p(&shot);
        p.drawPixmap(w.mapFromGlobal(search->pos()), search->grab());
        p.end();
        shot.save(dir + "/command-search.png");
    }
    c.check(first == "Сгруппировать", "Ctrl+K, «груп»: «Сгруппировать» comes first");
    bool ordered = search->substringCount() > 0;
    for (int i = 0; i < int(found.size()); ++i) // the text itself first, the letters-in-order matches after
        ordered &= (i < search->substringCount()) == found[size_t(i)]->text().remove('&').contains("груп", Qt::CaseInsensitive);
    std::printf("  «груп»: %d hold the text, %zu more only its letters in order\n", search->substringCount(),
                found.size() - size_t(search->substringCount()));
    c.check(ordered, "plain substring matches come first, the fuzzy ones after them");
    search->runSelected();
    pump(100);
    c.check(b.graph().groups.size() == 1 && !search->isVisible(), "Enter runs it: the two cubes are one group");
}

void testPivotAndBlank(Checker& c, MainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    pump(200);
    const QPointF a(v->width() * 0.5, v->height() * 0.6);
    sendMouse(v, QEvent::MouseButtonPress, a, Qt::RightButton, Qt::RightButton);
    for (int i = 1; i <= 6; ++i) sendMouse(v, QEvent::MouseMove, a + QPointF(8.0 * i, 0), Qt::NoButton, Qt::RightButton);
    const bool shown = v->orbitPivotShown();
    sendMouse(v, QEvent::MouseButtonRelease, a + QPointF(48, 0), Qt::RightButton, Qt::NoButton);
    pump(50);
    c.check(shown && !v->orbitPivotShown(), "orbiting marks the pivot; the mark goes with the button");
    QImage white(64, 48, QImage::Format_RGB32);
    white.fill(Qt::white);
    c.check(MainWindow::looksBlank(white) && !MainWindow::looksBlank(v->grabFramebuffer()),
            "a one-colour frame counts as blank, the drawn scene does not");
    w.checkBlankView(true);
    pump(100);
    auto* box = w.findChild<QMessageBox*>("blankViewDialog");
    c.check(box && box->isVisible(), "a blank view offers the restart on the processor");
    if (box) box->close();
    pump(50);
}

void testBodyOrbit(Checker& c) {
    Viewport v;
    auto snap=std::make_shared<ke::Frame>();
    snap->sceneName="follow camera fixture";
    snap->domain=ke::AABB({-10,-10,-10},{10,10,10});
    ke::Frame::Body body{}; body.object=42; body.movable=true; body.pos={2,3,4};
    snap->bodies.push_back(body); v.setSnapshot(snap);
    v.camera().lookAt({20,15,10},{0,1,0});
    const OrbitCamera scene=v.camera();
    c.check(v.followBody(42),"follow camera accepts a displayed moving body");
    c.check((v.camera().target()-QVector3D(2,3,4)).length()<1e-6f,"orbit targets centre of mass");
    v.camera().rotate(30,20); v.camera().zoom(2);
    const OrbitCamera orbit=v.camera();
    auto moved=std::make_shared<ke::Frame>(*snap);
    moved->bodies[0].pos={4,7,9};
    moved->bodies[0].rot=ke::Quaternion::fromAxisAngle({0,0,1},1);
    v.setSnapshot(moved);
    c.check((v.camera().target()-QVector3D(4,7,9)).length()<1e-6f &&
            v.camera().yaw()==orbit.yaw() && v.camera().pitch()==orbit.pitch() && v.camera().roll()==0,
            "translation follows COM; body roll does not rotate the camera");
    c.check(v.camera().distance()==orbit.distance(),"follow retains wheel zoom");
    v.followBody(0);
    c.check(v.camera().eye()==scene.eye() && v.camera().target()==scene.target(),"scene camera restored exactly");
    v.followBody(42);
    c.check(v.camera().yaw()==orbit.yaw() && v.camera().distance()==orbit.distance(),"orbit viewpoint remembered");
    c.check(!v.followBody(99) && v.followedBody()==42,"unknown follow target leaves camera intact");
    v.setSnapshot(std::make_shared<ke::Frame>(*snap));
    auto missing=std::make_shared<ke::Frame>(*snap); missing->bodies.clear(); v.setSnapshot(missing);
    c.check(!v.followedBody() && v.camera().eye()==scene.eye(),"deleted target restores scene view");
}

} // namespace

int runPlayTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    Checker c;
    testBodyOrbit(c);
    testToolbarWidths(c, w);
    testSameLook(c, b, v, dir);
    testPlayBar(c, w, b, v, dir);
    testKeep(c, w, b, v);
    testCommandSearch(c, w, b, dir);
    if (auto* mw = qobject_cast<MainWindow*>(&w)) testPivotAndBlank(c, *mw, b, v);
    return c.failures;
}
