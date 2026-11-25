// Self-test of the window's layout buttons (MainWindowLayout.cpp, see SelfTest.h): each area closes
// and opens with what was open in it, an empty area opens its main panel, a panel closed by its own
// ✕ unchecks its area, the layout menu lists every panel, and "only the 3D view" comes back as it was; the tool shelf's
// eye hides and shows the pictures plugins and agents draw over the view; a panel torn off can be maximized.
#include "SelfTestSupport.h"
#include <QJsonArray>
#include "Viewport.h"
#include "MainWindow.h"
#include "EditorOps.h"
#include "UiLanguage.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QListWidget>
#include <QTimer>
#include <QDockWidget>
#include <QMenu>
#include <QStatusBar>
#include <QToolBar>

#include <cstdio>

using namespace selftest;

namespace {

bool open(QMainWindow& w, const char* dock) {
    auto* d = w.findChild<QDockWidget*>(dock);
    return d && !d->isHidden();
}

void testAreas(Checker& c, QMainWindow& w) {
    QAction *left = action(w, "actionLeftBar"), *bottom = action(w, "actionBottomPanel"), *right = action(w, "actionRightBar");
    c.check(left && bottom && right, "the three layout buttons exist (left panel, bottom panel, right panel)");
    if (!left || !bottom || !right) return;
    w.findChild<QDockWidget*>("inspectorDock")->show();
    pump(150);
    c.check(right->isChecked(), "the inspector open: the right-panel button is on");
    right->trigger();
    pump(150);
    c.check(!open(w, "inspectorDock") && !right->isChecked(), "Ctrl+Alt+B closes the right panel with the inspector");
    right->trigger();
    pump(150);
    c.check(open(w, "inspectorDock") && right->isChecked(), "pressed again, the inspector comes back");
    auto* console = w.findChild<QDockWidget*>("consoleDock"); // tabs of the bottom panel beside the graphs: put away for this check
    auto* sequencer = w.findChild<QDockWidget*>("sequencerDock");
    if (console) console->hide();
    if (sequencer) sequencer->hide();
    pump(100);
    const bool graphsBefore = open(w, "resultsDock");
    if (graphsBefore) bottom->trigger(), pump(150);
    bottom->trigger();
    pump(150);
    QAction* graphs = action(w, "actionGraphs");
    c.check(open(w, "resultsDock") && graphs && graphs->isChecked(), "an empty bottom panel opens the graphs (and F9 shows them on)");
    if (console) console->show();
    if (sequencer) sequencer->show(), sequencer->raise();
    pump(100);
    bottom->trigger();
    pump(150);
    c.check(!open(w, "resultsDock") && !graphs->isChecked(), "closed again, the graphs close and F9 is off");
    left->trigger();
    pump(150);
    c.check(open(w, "labDock") && left->isChecked(), "an empty left panel opens the Laboratory");
    left->trigger();
    pump(150);
    c.check(!open(w, "labDock") && !left->isChecked(), "Ctrl+B closes it");
    w.findChild<QDockWidget*>("inspectorDock")->hide(); // as its ✕ does
    pump(150);
    c.check(!right->isChecked(), "a panel closed by its own ✕ unchecks its area's button");
    w.findChild<QDockWidget*>("inspectorDock")->show();
    if (graphsBefore) bottom->trigger();
    pump(150);
}

void testMenuAndOnlyView(Checker& c, QMainWindow& w, const QString& dir) {
    auto* menu = w.findChild<QMenu*>("layoutMenu");
    if (menu) emit menu->aboutToShow(); // as opening it does
    QStringList items;
    if (menu) for (QAction* a : menu->actions()) if (!a->isSeparator()) items << a->text();
    std::printf("  layout menu: %s\n", qPrintable(items.join(" | ")));
    c.check(items.contains("Инспектор") && items.contains("Лаборатория") && items.contains("Графики") && items.contains("Полка инструментов") && items.contains("Только 3D-вид"),
            "the layout menu lists the panels one by one and the 3D-only mode");
    c.check(items.contains("Нижняя панель во всю ширину"), "the layout menu has the bottom panel in the window's whole width");
    QAction* only = action(w, "actionOnlyViewport");
    if (!only) return;
    auto* shelf = w.findChild<QToolBar*>("toolShelf");
    const bool shelfBefore = shelf && !shelf->isHidden(), statusBefore = !w.statusBar()->isHidden();
    only->trigger();
    pump(200);
    if (!dir.isEmpty()) w.grab().save(dir + "/layout-only-view.png");
    int openDocks = 0;
    for (QDockWidget* d : w.findChildren<QDockWidget*>()) openDocks += !d->isFloating() && !d->isHidden();
    c.check(openDocks == 0 && (!shelf || shelf->isHidden()) && w.statusBar()->isHidden(),
            "only the 3D view: no panel, no tool shelf, no status line");
    only->trigger();
    pump(200);
    c.check(open(w, "inspectorDock") && (!shelf || shelf->isHidden() != shelfBefore) && w.statusBar()->isHidden() != statusBefore,
            "pressed again, the window is laid out as before");
    if (!dir.isEmpty()) w.grab().save(dir + "/layout-restored.png");
}

// The shelf's three eyes (view_show): the cards in the frame, the labels in the scene and the editor's own marks hidden
// each by its own, the editor's ruler kept; view_show shows them again.
void testDrawingsEye(Checker& c, QMainWindow& w, Viewport* v) {
    QAction *cards = action(w, "toolDrawings"), *labels = action(w, "toolLabels");
    EditorOps* ops = dynamic_cast<MainWindow&>(w).operators();
    if (!cards || !labels) return c.check(false, "the shelf's eyes exist");
    ops->runNow("view_draw", {{"key", "агент/карточка"}, {"text", "шаг"}, {"space", "screen"}, {"place", QJsonArray{0.1, 0.9}}});
    ops->runNow("view_draw", {{"key", "агент/формула"}, {"text", "F = ma"}, {"space", "label"}, {"at", QJsonArray{0, 1, 0}}});
    cards->trigger(); // off
    pump(50);
    const bool cardsOff = !v->shown("агент/карточка") && v->shown("агент/формула") && v->shown("editor/ruler") && !cards->isChecked();
    labels->trigger(); // off too
    pump(50);
    const bool labelsOff = !v->shown("агент/формула") && !labels->isChecked() && !ops->runNow("view_show", {}).value("drawings").toBool();
    ops->runNow("view_show", {{"drawings", true}});
    const bool back = v->shown("агент/карточка") && v->shown("агент/формула") && cards->isChecked() && labels->isChecked();
    QAction* marks = action(w, "toolMarks"); // the third: the editor's own marks, a picture with them not undoing the switch
    if (marks) marks->trigger();
    ops->runNow("view_screenshot", {{"marks", false}});
    const bool marksOff = marks && !v->marksVisible() && !marks->isChecked() && !ops->runNow("view_show", {}).value("marks").toBool();
    ops->runNow("view_show", {{"marks", true}});
    const bool marksBack = v->marksVisible() && marks && marks->isChecked();
    ops->runNow("view_draw", {{"clear", true}, {"key", "агент/"}});
    c.check(cardsOff && labelsOff && back && marksOff && marksBack,
            "the shelf's three eyes hide the cards in the frame, the labels in the scene and the editor's own marks each by its own "
            "(the editor's ruler kept; a picture without marks leaves the switch as it was); view_show shows them again");
}

// A panel torn off is a framed window that can be maximized (a second screen); its ✕ puts it back in its place.
void testTornOff(Checker& c, QMainWindow& w) {
    auto* d = w.findChild<QDockWidget*>("consoleDock");
    if (!d) return c.check(false, "the console's panel exists");
    d->show();
    d->setFloating(true);
    const bool framed = waitUntil([&] { return bool(d->windowFlags() & Qt::WindowMaximizeButtonHint); }, 2000);
    d->close(); // its ✕: back in its place, shown
    pump(50);
    const bool home = !d->isFloating() && d->isVisible();
    std::printf("  torn off: framed %s, back home %s (floating %s, visible %s)\n", framed ? "yes" : "no", home ? "yes" : "no",
                d->isFloating() ? "yes" : "no", d->isVisible() ? "yes" : "no");
    c.check(framed && home, "a panel torn off is a framed window with a maximize button; its ✕ puts it back in its place, shown");
}

// «Примеры», pressed as a person presses it: the gallery shows the example scenes - a tile each -, closed again.
void testGallery(Checker& c, QMainWindow& w) {
    QToolButton* button = nullptr;
    for (QToolButton* b : w.findChildren<QToolButton*>())
        if (b->text() == uiText("Kinetica", "Примеры")) button = b;
    if (!button) return c.check(false, "the toolbar has the button «Примеры»");
    int tiles = -1;
    QTimer::singleShot(400, &w, [&] { // the gallery runs its own loop: looked at from inside it, then closed
        if (auto* list = w.findChild<QListWidget*>("galleryList")) tiles = list->count();
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
    });
    button->click();
    std::printf("  «Примеры»: %d scenes in the gallery\n", tiles);
    c.check(tiles >= 3, "«Примеры» shows the example scenes (examples/scenes), a tile each");
}

} // namespace

int runLayoutTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir) {
    Checker c;
    (void)b;
    testAreas(c, w);
    testMenuAndOnlyView(c, w, shotsDir);
    testDrawingsEye(c, w, v);
    testTornOff(c, w);
    testGallery(c, w);
    return c.failures;
}
