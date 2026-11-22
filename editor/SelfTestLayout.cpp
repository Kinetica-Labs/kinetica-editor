// Self-test of the window's layout buttons (MainWindowLayout.cpp, see SelfTest.h): each area closes
// and opens with what was open in it, an empty area opens its main panel, a panel closed by its own
// ✕ unchecks its area, the layout menu lists every panel, and "only the 3D view" comes back as it was.
#include "SelfTestSupport.h"

#include <QAction>
#include <QDockWidget>
#include <QMenu>
#include <QStatusBar>
#include <QToolBar>

using namespace selftest;

namespace {

bool open(QMainWindow& w, const char* dock) {
    auto* d = w.findChild<QDockWidget*>(dock);
    return d && !d->isHidden();
}

QAction* action(QMainWindow& w, const char* name) { return w.findChild<QAction*>(name); }

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

} // namespace

int runLayoutTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir) {
    Checker c;
    (void)b, (void)v;
    testAreas(c, w);
    testMenuAndOnlyView(c, w, shotsDir);
    return c.failures;
}
