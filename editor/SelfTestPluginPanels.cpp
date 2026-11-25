// A plugin's panel by hand (see SelfTestPlugins.cpp, which starts the plugins' host and calls these): its chart in the
// plots, its lines from what on_tick shows, a point a frame; a training's chart over steps from a thread; a picture
// (ui.svg); its words in the window's language (i18n/<language>.json).
#include "SelfTestSupport.h"

#include "EditorOps.h"
#include "MainWindow.h"
#include "PlotPanel.h"
#include "PluginHost.h"
#include "PluginPanel.h"
#include "TrackChart.h"
#include "UiLanguage.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>

using namespace ke;
using namespace selftest;

namespace {

// A plugin's chart as the plots draw it - the only place it is drawn; asked anew each time, as the plots build their views
// again with the layout and the selection.
TrackChart* plotted(QMainWindow& w, const QString& section, size_t k = 0) {
    if (auto* plots = w.findChild<PlotPanel*>())
        for (const auto& [title, views] : plots->pluginSections())
            if (title == section && k < views.size()) return views[k];
    return nullptr;
}

// A chart's row in the plots' «Что показывать»: under its section's row (by the section's title), the k-th; nullptr: none.
QTreeWidgetItem* treeRow(PlotPanel* plots, const QString& section, int k = 0) {
    auto* tree = plots->findChild<QTreeWidget*>("plotTree");
    for (int i = 0; tree && i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->text(0) == section && k < tree->topLevelItem(i)->childCount()) return tree->topLevelItem(i)->child(k);
    return nullptr;
}

void tick(PlotPanel* plots, const QString& section, bool on, int k = 0) {
    if (QTreeWidgetItem* row = treeRow(plots, section, k)) row->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
    pump(50);
}

// The window's first level (docs/contracts/ui.md): its docked panels by name, each open (+) or closed (-).
QStringList firstLevel(QMainWindow& w) {
    QStringList out;
    for (QDockWidget* d : w.findChildren<QDockWidget*>()) out << d->objectName() + (d->isHidden() ? "-" : "+");
    out.sort();
    return out;
}

// A plugin's chart (ui.chart): lines over the run's time, drawn by the editor from what the plugin shows in on_tick - a
// point a frame at that frame's time, with the frame's answer; the same value is a word of the panel; a new run is a new
// history; the plugin gone, its chart too. The panel asked for the bottom, and is a tab of the inspector all the same:
// the plugin, started, started again and gone, leaves the window's panels as they were; the chart is drawn in the plots
// only, the panel's «Графики ▸» opens them.
void testChart(Checker& c, QMainWindow& w, SceneBuilder& b, EditorOps* ops, PluginHost* host, const QString& folder) {
    const QStringList before = firstLevel(w);
    QDir().mkpath(folder + "/meter");
    const QString file = R"py(from kinetica.plugin import on_play, on_tick, show, ui

clock = 0.0

ui.panel("Счётчик", ui.chart("Время кадра", {"clock": "время", "half": "половина"}, unit="с"), ui.value("clock", "Сейчас"),
         ui.chart("Плата", {"board": "значение"}, x={"since": "время с подключения, с"}), place="bottom")


@on_play
def start(scene):
    global clock
    clock = 0.0


@on_tick
def count(scene, dt, sensors):
    global clock
    clock += dt
    show(clock=clock, half=clock / 2, since=100 + clock, board=clock)  # a board's own clock, 100 s ahead, as depz's
)py";
    write(folder + "/meter/plugin.py", file);
    host->start(true);
    TrackChart* chart = nullptr;
    waitUntil([&] { return (chart = plotted(w, "Счётчик")) != nullptr && host->ticking(); }, 10000);
    if (!chart) return c.check(false, "a plugin's chart comes with its panel, in the plots"), void();
    const QWidget* first = w.findChild<QWidget*>("pluginPanel");
    write(folder + "/meter/plugin.py", file + "\n");
    waitUntil([&] { return host->ticking() && plotted(w, "Счётчик") && w.findChild<QWidget*>("pluginPanel") != first; }, 10000); // started again
    pump(300);
    auto* panel = w.findChild<QWidget*>("pluginPanel");
    QWidget* dock = panel;
    while (dock && !qobject_cast<QDockWidget*>(dock)) dock = dock->parentWidget();
    auto* button = w.findChild<QPushButton*>("pluginChartsButton");
    std::printf("  the window's panels with the plugin: %s (before: %s)\n", qPrintable(firstLevel(w).join(' ')), qPrintable(before.join(' ')));
    c.check(firstLevel(w) == before && dock && dock->objectName() == "inspectorDock",
            "a plugin's panel - asked for the bottom too - is a tab of the inspector: the window's panels stay as they were");
    c.check(panel && panel->findChildren<TrackChart*>().isEmpty() && button && button->text().contains("Время кадра"),
            "the panel draws no chart: one «Графики ▸» button names its charts");
    if (button) button->click();
    pump(50);
    auto* results = w.findChild<QDockWidget*>("resultsDock");
    c.check(results && results->isVisible(), "the panel's «Графики ▸» opens the plots");
    int tabs = 0, iconed = 0; // the docks' tabs carry their panel's icon beside its words
    for (QTabBar* bar : w.findChildren<QTabBar*>(Qt::FindDirectChildrenOnly))
        for (int i = 0; i < bar->count(); ++i) ++tabs, iconed += !bar->tabIcon(i).isNull();
    c.check(tabs > 0 && iconed == tabs, "every tab of the window's panels has its icon beside its words");
    auto* wide = w.findChild<QToolButton*>("actionBottomWidePlots");
    c.check(wide && wide->defaultAction() && wide->defaultAction()->objectName() == "actionBottomWide",
            "the plots' bar has the bottom panel's ⇔ (the window's whole width), as the Sequencer's has");
    chart = plotted(w, "Счётчик");
    b.newScene();
    ops->runNow("scene_add", {{"shape", "box"}, {"position", QJsonArray{0.0, 1.0, 0.0}}, {"role", "rigid"}});
    const PlotSeries* line = chart->tracks().size() == 2 ? chart->tracks()[0].series : nullptr;
    const PlotSeries* half = line ? chart->tracks()[1].series : nullptr;
    const auto run = [&](int frames) {
        QJsonObject stepped;
        ops->run("sim_step", {{"frames", frames}}, [&stepped](const QJsonObject& r) { stepped = r; }, "ui");
        waitUntil([&] { return !stepped.isEmpty(); }, 20000);
        waitUntil([&] { return line && line->size() == size_t(frames) + 1; }, 3000); // the answers come after the fact
        pump(100);                                                                    // and the panel follows at 25 a second
    };
    run(30);
    // A point a frame, the starting one too, at the frame's time: the plugin's clock runs one step ahead of it (the
    // simulation's time is a float's sum of steps: the two agree to its rounding).
    bool timed = line && line->size() == 31 && half->size() == 31 && line->time(0) == 0;
    for (size_t k = 1; timed && k < line->size(); ++k)
        timed = line->time(k) > line->time(k - 1) && std::fabs((line->value(k) - line->value(k - 1)) - (line->time(k) - line->time(k - 1))) < 1e-6 &&
                std::fabs(half->value(k) * 2 - line->value(k)) < 1e-12;
    auto* word = w.findChild<QLabel*>("pluginValue_clock");
    const QString said = word ? word->text() : QString();
    const bool worded = line && line->size() > 0 && said == "Сейчас: " + QString::number(line->latest(), 'g', 6);
    chart = plotted(w, "Счётчик");
    if (!chart) return c.check(false, "the plugin's chart stays in the plots"), void();
    const auto [from, to] = chart->timeRange();
    const bool framed = line && line->size() > 0 && from == 0 && std::fabs(to - line->time(line->size() - 1)) < 1e-9 && chart->title() == "Время кадра, с";
    std::printf("  a plugin's chart «%s»: %zu lines, %zu points over %.3f s after 30 frames, in step with the frames %s; the word «%s»\n",
                qPrintable(chart->title()), chart->tracks().size(), line ? line->size() : 0, to, timed ? "yes" : "no", qPrintable(said));
    c.check(timed && framed, "a plugin's chart: what on_tick shows is a point of its line at that frame's time, a frame each");
    c.check(worded, "the value a chart draws is a word of the panel too: one show() for both");
    // The wheel over a chart while the windows slide sets its own group's - the charts of its clock: over the plugin's
    // chart on the run's time, the scene's charts with it and not the board's (its own clock); over the board's, the
    // board's alone (the user: a sensor's charts together, not every chart).
    if (auto* plots = w.findChild<PlotPanel*>()) {
        plots->setView(PlotPanel::View());
        pump(100);
        const auto wheelOver = [&](TrackChart* over) {
            const QPointF middle(over->width() * 0.6, over->height() * 0.6);
            QWheelEvent wheel(middle, over->mapToGlobal(middle), {}, {0, 480}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(over, &wheel);
            QJsonObject stepped;
            ops->run("sim_step", {{"frames", 30}}, [&stepped](const QJsonObject& r) { stepped = r; }, "ui");
            waitUntil([&] { return !stepped.isEmpty(); }, 20000);
            pump(200);
        };
        const auto width = [](TrackChart* v) { return v ? v->shownTime().second - v->shownTime().first : 0.0; };
        const auto sceneWidth = [&] {
            double most = 0;
            for (TrackChart* v : plots->views()) most = std::max(most, width(v));
            return most;
        };
        const double boardBefore = width(plotted(w, "Счётчик", 1));
        wheelOver(plotted(w, "Счётчик"));
        const double runSpan = plots->view().span(""), boardAfter = width(plotted(w, "Счётчик", 1));
        const auto [p0, p1] = plotted(w, "Счётчик")->shownTime();
        std::printf("  the wheel over the plugin's chart on the run's time: its group's window %.3f s - the plugin's %.3f, the scene's %.3f; "
                    "the board's (own clock) %.3f, was %.3f\n", runSpan, p1 - p0, sceneWidth(), boardAfter, boardBefore);
        const bool runGroup = runSpan > 0 && std::fabs((p1 - p0) - runSpan) < 1e-6 && std::fabs(p1 - line->time(line->size() - 1)) < 1e-9 &&
                              std::fabs(sceneWidth() - runSpan) < 1e-6 && boardAfter > runSpan * 1.5;
        TrackChart* board = plotted(w, "Счётчик", 1);
        wheelOver(board);
        const double boardSpan = plots->view().span(board ? board->axisWords() : QString());
        const auto [b0, b1] = plotted(w, "Счётчик", 1)->shownTime();
        std::printf("  then over the board's chart («%s»): its window %.3f s (%.3f..%.3f); the run's still %.3f\n",
                    board ? qPrintable(board->axisWords()) : "none", boardSpan, b0, b1, plots->view().span(""));
        c.check(runGroup && boardSpan > 0 && std::fabs((b1 - b0) - boardSpan) < 1e-6 && b1 > 100 && plots->view().span("") == runSpan,
                "the wheel sets its own clock's group: the run's charts (the scene's and a plugin's) together, a board's on its own clock apart");
        plots->setView(PlotPanel::View());
    }
    b.stop();
    pump(100);
    run(10);
    c.check(line && line->size() == 11 && line->time(0) == 0, "a new run is a new history of the chart");
    b.stop();
    QDir(folder + "/meter").removeRecursively();
    c.check(waitUntil([&] { return plotted(w, "Счётчик") == nullptr && !w.findChild<QWidget*>("pluginPanel"); }, 5000),
            "the plugin gone, its chart goes with its panel");
    if (results && before.contains("resultsDock-")) results->hide(); // as it was: the button opened it
    pump(50);
    c.check(firstLevel(w) == before, "the plugin gone, the window's panels are as they were before it");
}

// A plugin's words in the window's language (docs/contracts/editor-plugin.md, «Перевод»): its catalog
// i18n/<language>.json beside plugin.py; the editor translates what the plugin describes - the panel's title, a button, a
// command's label, a choice's words (the value it gets stays the plugin's) -, tr() what it makes up; the catalog saved,
// the plugin starts again in its new words.
void testWords(Checker& c, QMainWindow& w, EditorOps* ops, PluginHost* host, const QString& folder) {
    QDir().mkpath(folder + "/speaker/i18n");
    const QString catalog = folder + "/speaker/i18n/" + UiLanguage::active() + ".json";
    write(catalog, R"({"Слова": "Words", "Сказать": "Say", "да": "yes", "нет": "no", "привет, {who}": "hello, {who}", "тишина": "silence"})");
    write(folder + "/speaker/plugin.py", R"py(from kinetica.plugin import command, field, tr, ui


@command(label="Сказать")
def say(scene, answer: str = field("да", choices=("да", "нет"), label="Ответ")):
    return {"said": tr("привет, {who}").format(who=answer)}


ui.panel("Слова", ui.choice("answer", ("да", "нет"), label="Ответ"), ui.button(say), ui.value("state", default="тишина"))
)py");
    host->start(true);
    QPushButton* button = nullptr;
    waitUntil([&] { return (button = w.findChild<QPushButton*>("pluginButton_say")) != nullptr; }, 10000);
    QAction* act = action(w, "plugin:speaker_say");
    auto* choice = button ? button->parentWidget()->findChild<QComboBox*>() : nullptr;
    QJsonObject said;
    ops->run("speaker_say", {{"answer", "нет"}}, [&said](const QJsonObject& r) { said = r; }, "ui");
    waitUntil([&] { return !said.isEmpty(); }, 5000);
    std::printf("  a plugin's words in «%s»: button «%s», action «%s», choice «%s», said «%s»\n", qPrintable(UiLanguage::active()),
                button ? qPrintable(button->text()) : "-", act ? qPrintable(act->text()) : "-", choice ? qPrintable(choice->itemText(0)) : "-",
                qPrintable(said.value("said").toString()));
    c.check(button && button->text() == "Say" && act && act->text().startsWith("Say") && choice && choice->itemText(0) == "yes",
            "the editor shows a plugin's labels and choices in its catalog's words");
    auto* state = w.findChild<QLabel*>("pluginValue_state");
    c.check(state && state->text() == "silence", "a value's first words are the catalog's too");
    c.check(said.value("said").toString() == "hello, нет", "tr() in the plugin gives its catalog's word; the value a choice sends stays the plugin's");
    write(catalog, R"({"Сказать": "Speak"})");
    c.check(waitUntil([&] { return (button = w.findChild<QPushButton*>("pluginButton_say")) && button->text() == "Speak"; }, 10000),
            "the catalog saved, the plugin starts again in its new words");
    QDir(folder + "/speaker").removeRecursively();
    waitUntil([&] { return !w.findChild<QPushButton*>("pluginButton_say"); }, 5000);
}

// A picture in the panel (ui.svg): an SVG the plugin sends through show() from a command is drawn by the editor, scaled to
// the panel with the aspect kept; an invalid one does not crash and leaves the placeholder.
void testSvg(Checker& c, QMainWindow& w, EditorOps* ops, PluginHost* host, const QString& folder) {
    QDir().mkpath(folder + "/painter");
    write(folder + "/painter/plugin.py", R"py(from kinetica.plugin import command, show, ui

ui.panel("Рисунок", ui.svg("pic", height=120), place="bottom")


@command(label="Рисовать")
def draw(scene, good: bool = True):
    show(pic='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 100"><rect width="200" height="100" fill="#ff0000"/></svg>' if good else "not an svg")
)py");
    host->start(true);
    SvgView* view = nullptr;
    waitUntil([&] { return (view = w.findChild<SvgView*>("pluginSvg_pic")) != nullptr && action(w, "plugin:painter_draw") != nullptr; }, 10000);
    if (!view) return c.check(false, "a picture comes with its panel"), void();
    const auto look = [&](bool good) {
        ops->run("painter_draw", {{"good", good}}, [](const QJsonObject&) {}, "ui");
        waitUntil([&] { return view->valid() == good; }, 5000);
        pump(150);
    };
    look(true);
    view->resize(300, 200);
    const QImage red = view->grab().toImage();
    int painted = 0;
    for (int y = 0; y < red.height(); ++y)
        for (int x = 0; x < red.width(); ++x) painted += qRed(red.pixel(x, y)) > 200 && qGreen(red.pixel(x, y)) < 60;
    const bool fitted = view->heightForWidth(300) == 120 && view->heightForWidth(100) == 50; // aspect 2:1, at most 120 px
    std::printf("  a picture «pluginSvg_pic»: valid %s, %d red pixels in 300x200, heightForWidth(300)=%d\n", view->valid() ? "yes" : "no", painted, view->heightForWidth(300));
    c.check(view->valid() && painted > 5000, "an SVG the plugin shows is drawn in the panel (a valid renderer, a painted picture)");
    c.check(fitted, "the picture takes the panel's width with its aspect kept, at most the given height");
    const QPointF middle(150, 60);
    QWheelEvent wheel(middle, view->mapToGlobal(middle), {}, {0, 480}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view, &wheel);
    const double zoomed = view->zoom();
    const QPointF dragTo = middle + QPointF(40, 0);
    QMouseEvent press(QEvent::MouseButtonPress, middle, view->mapToGlobal(middle), Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, dragTo, view->mapToGlobal(dragTo), Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, dragTo, view->mapToGlobal(dragTo), Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
    const QPointF before = view->pan();
    QApplication::sendEvent(view, &press), QApplication::sendEvent(view, &move), QApplication::sendEvent(view, &release);
    const bool moved = std::abs(view->pan().x() - before.x() - 40) < 0.5;
    QMouseEvent twice(QEvent::MouseButtonDblClick, middle, view->mapToGlobal(middle), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view, &twice);
    std::printf("  the wheel over it: zoom %.2f; a double click: %.2f\n", zoomed, view->zoom());
    c.check(zoomed > 1.5 && view->zoom() == 1.0, "the wheel zooms the picture in, a double click shows it whole again");
    c.check(moved, "the wheel held moves the zoomed picture, as in the 3D view");
    look(false);
    const QImage placeholder = view->grab().toImage();
    c.check(!view->valid() && placeholder.width() > 0, "an invalid SVG does not crash: the quiet placeholder");
    QDir(folder + "/painter").removeRecursively();
    c.check(waitUntil([&] { return w.findChild<SvgView*>("pluginSvg_pic") == nullptr; }, 5000), "the plugin gone, its picture goes with its panel");
}

// A training's chart (ui.chart(..., x="step")): the axis is a step, not the run's time, and the points come from a thread
// of the plugin through show() - no run, no on_tick. They are in the plots' section of the plugin, «Что показывать»
// chooses the charts drawn; a step going back is a new training; the plugin gone, the section goes with its panel.
void testStepChart(Checker& c, QMainWindow& w, EditorOps* ops, PluginHost* host, const QString& folder) {
    QDir().mkpath(folder + "/trainer");
    write(folder + "/trainer/plugin.py", R"py(import threading

from kinetica.plugin import command, show, ui

ui.panel("Обучение", ui.chart("Награда", {"best": "лучший", "mean": "средний"}, unit="м", x={"step": "поколение"}), place="bottom")


@command(label="Учить")
def train(scene, generations: int = 5, first: int = 0):
    def loop():
        for g in range(first, first + generations):
            show(step=g, best=10.0 * g, mean=g)
    threading.Thread(target=loop).start()
)py");
    host->start(true);
    TrackChart* chart = nullptr;
    waitUntil([&] { return (chart = plotted(w, "Обучение")) != nullptr && action(w, "plugin:trainer_train") != nullptr; }, 10000);
    auto* plots = w.findChild<PlotPanel*>();
    if (!chart || !plots) return c.check(false, "a step chart comes with its panel, in the plots"), void();
    const PlotSeries* best = chart->tracks().size() == 2 ? chart->tracks()[0].series : nullptr;
    const auto train = [&](int n, size_t points, int first = 0) {
        ops->run("trainer_train", {{"generations", n}, {"first", first}}, [](const QJsonObject&) {}, "ui");
        waitUntil([&] { return best && best->size() == points; }, 5000);
        pump(150);
    };
    train(5, 5);
    chart = plotted(w, "Обучение");
    bool stepped = chart && best && best->size() == 5 && chart->axisWords() == "поколение";
    for (size_t k = 0; stepped && k < best->size(); ++k) stepped = best->time(k) == double(k) && best->value(k) == 10.0 * double(k);
    const bool shown = chart && chart->title() == "Награда, м" && chart->timeRange().second == 4;
    std::printf("  a step chart «%s» in the plots' section «Обучение»: %zu points by generation from a thread's show()\n",
                chart ? qPrintable(chart->title()) : "-", best ? best->size() : 0);
    c.check(stepped, "a step chart takes a point per line whenever one show() carries the x and the line's value, from any thread");
    c.check(shown, "the plugin's chart is in the plots, in a section headed with the panel's title, all its points in sight");
    // «Что показывать»: a chart unticked is not drawn - its section neither, none of its charts ticked, the others take
    // the room; ticked again, it is back. The scene's charts the same; the find line keeps the rows that have its words.
    const bool listed = treeRow(plots, "Обучение") && treeRow(plots, "Обучение")->text(0) == "Награда, м";
    tick(plots, "Обучение", false);
    const bool gone = !plotted(w, "Обучение") && treeRow(plots, "Обучение");
    tick(plots, "Обучение", true);
    c.check(listed && gone && plotted(w, "Обучение"), "«Что показывать» lists the plugin's charts: unticked - not drawn, its section neither; ticked - back");
    const auto sceneCharts = [&] {
        int n = 0;
        for (TrackChart* v : plots->findChildren<TrackChart*>("trackChart")) n += v->isVisibleTo(plots);
        return n;
    };
    const int energy = sceneCharts();
    tick(plots, uiText("Kinetica", "Сцена"), false);
    const int without = sceneCharts();
    tick(plots, uiText("Kinetica", "Сцена"), true);
    c.check(energy > 0 && without == energy - 1 && sceneCharts() == energy, "the scene's charts are ticked there too: the energy goes and comes back");
    auto* find = plots->findChild<QLineEdit*>("plotFind");
    if (find) find->setText("Наград");
    const bool found = treeRow(plots, "Обучение") && !treeRow(plots, "Обучение")->isHidden() && treeRow(plots, uiText("Kinetica", "Сцена")) &&
                       treeRow(plots, uiText("Kinetica", "Сцена"))->isHidden();
    if (find) find->clear();
    c.check(found, "its find line keeps the charts whose words have it, hides the others");
    // The wheel over a chart: its time closer about the mouse, frozen while it is the reader's; a drag moves it; ↺ - all
    // of it again. Shift + the wheel: the values.
    if (TrackChart* v = plotted(w, "Обучение")) {
        const auto [a0, a1] = v->timeRange();
        const QRectF r = v->plotArea();
        const QPointF mid = r.center();
        QWheelEvent in(mid, v->mapToGlobal(mid), {}, {0, 240}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(v, &in);
        const auto [b0, b1] = v->timeRange();
        QMouseEvent press(QEvent::MouseButtonPress, mid, v->mapToGlobal(mid), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        const QPointF to = mid + QPointF(r.width() / 4, 0);
        QMouseEvent move(QEvent::MouseMove, to, v->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, to, v->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(v, &press), QApplication::sendEvent(v, &move), QApplication::sendEvent(v, &release);
        const auto [c0, c1] = v->timeRange();
        pump(100); // the panel's own window keeps coming: the reader's stays
        const bool kept = v->timeRange().first == c0;
        const QPointF corner(r.right() - 12, r.top() + 10);
        QMouseEvent back(QEvent::MouseButtonPress, corner, v->mapToGlobal(corner), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(v, &back);
        std::printf("  the wheel: %.2f..%.2f -> %.2f..%.2f; dragged a quarter: %.2f..%.2f, kept %s; ↺: %.2f..%.2f\n", a0, a1, b0, b1, c0, c1,
                    kept ? "yes" : "no", v->timeRange().first, v->timeRange().second);
        c.check(b1 - b0 < (a1 - a0) * 0.8 && b0 > a0 && b1 < a1, "the wheel over a chart brings its time closer, about the mouse");
        c.check(c0 < b0 && std::fabs((c1 - c0) - (b1 - b0)) < 1e-9 && kept, "a drag moves the window; the reader's window stays while the data come");
        // all of it: at least what was there - the trainer's points keep coming, so the whole may have grown meanwhile
        c.check(!v->zoomed() && v->timeRange().first <= a0 && v->timeRange().second >= a1, "↺ in the plot's corner gives all of it back");
    } else c.check(false, "the step chart is there to zoom");
    // The plots are a state machine: the view is one value; what is not plain is said under the charts with one way back;
    // the value is saved by name and set again through plots_set, read through plots_get - the same as the hands make it.
    {
        const auto call = [&](const QString& op, const QJsonObject& args) {
            QJsonObject answer;
            ops->run(op, args, [&answer](const QJsonObject& r) { answer = r; }, "ui");
            waitUntil([&] { return !answer.isEmpty(); }, 3000);
            return answer;
        };
        auto* words = w.findChild<QLabel*>("plotStateWords"); // on the bar under the charts: the window's
        auto* state = w.findChild<QWidget*>("plotState");
        tick(plots, "Обучение", false);
        const QString said = words ? words->text() : QString();
        const bool shown = state && state->isVisibleTo(&w);
        const QJsonObject saved = call("plots_set", {{"save", "selftest"}});
        if (auto* reset = w.findChild<QToolButton*>("plotResetView")) reset->click();
        pump(50);
        const bool plain = plots->view().hidden.empty() && plotted(w, "Обучение") && state && !state->isVisibleTo(&w);
        const QJsonObject back = call("plots_set", {{"preset", "selftest"}});
        const bool restored = plots->view().hidden.size() == 1 && !plotted(w, "Обучение") && back.value("view").toObject() == saved.value("view").toObject();
        const QJsonObject got = call("plots_get", {});
        const QJsonObject gone = call("plots_set", {{"delete", "selftest"}, {"reset", true}});
        std::printf("  the view's words: «%s», shown %s; reset plain %s; the preset restored %s; plots_get: %d charts, presets after delete %d\n",
                    qPrintable(said), shown ? "yes" : "no", plain ? "yes" : "no", restored ? "yes" : "no", int(got.value("charts").toArray().size()),
                    int(gone.value("presets").toArray().size()));
        c.check(!said.isEmpty() && shown && plain, "what is not the plain view is said under the charts, and «Сбросить вид» gives the plain one");
        c.check(saved.value("ok").toBool() && restored && got.value("view").toObject() == saved.value("view").toObject(),
                "the view is one value: saved by name (plots_set save), set again (preset), read (plots_get) the same");
        c.check(gone.value("ok").toBool() && !gone.value("presets").toArray().contains("selftest") && plotted(w, "Обучение"),
                "plots_set delete removes the preset, reset - every chart back");
        // What «Ещё величины…», «Очистить» and «Сохранить CSV» do, an agent does too (MCP offers every operation).
        const auto shownOf = [](const QJsonObject& r, const QString& id) {
            for (const QJsonValue& ch : r.value("channels").toArray())
                if (ch.toObject().value("id").toString() == id) return ch.toObject().value("shown").toBool();
            return false;
        };
        // A quantity on no chart (a step measures them all), put on and taken off again: the scene's charts are left as
        // they were - the plots keep them by the scene's name for the next tests.
        call("sim_step", {{"frames", 3}});
        QString energy;
        for (const QJsonValue& ch : call("plots_get", {}).value("channels").toArray())
            if (!ch.toObject().value("shown").toBool() && energy.isEmpty()) energy = ch.toObject().value("id").toString();
        const QJsonObject on = call("plots_show", {{"channels", QJsonArray{energy}}});
        const QJsonObject off = call("plots_show", {{"channels", QJsonArray{energy}}, {"on", false}});
        const QJsonObject unknown = call("plots_show", {{"channels", QJsonArray{"no/such"}}});
        const QString csv = QDir::temp().filePath("kinetica-selftest-plots.csv");
        const QJsonObject saved2 = call("plots_save", {{"file", csv}});
        const QJsonObject cleared = call("plots_clear", {});
        call("sim_stop", {});
        std::printf("  plots_show %s off: shown %s, on: shown %s; an unknown one: %s; plots_save %s; plots_clear %s\n", qPrintable(energy),
                    shownOf(off, energy) ? "yes" : "no", shownOf(on, energy) ? "yes" : "no", qPrintable(unknown.value("error").toString()),
                    QFile::exists(csv) ? "wrote the file" : "-", cleared.value("ok").toBool() ? "ok" : "-");
        c.check(!energy.isEmpty() && !shownOf(off, energy) && shownOf(on, energy) && !unknown.value("ok").toBool(),
                "plots_show puts a quantity on its chart and takes it off; an unknown one is refused");
        c.check(saved2.value("ok").toBool() && QFile::exists(csv) && cleared.value("ok").toBool(), "plots_save writes the CSV, plots_clear erases the history");
        QFile::remove(csv);
    }
    // A double click on a chart: it alone in the whole width, a way back over it; again - every chart back.
    const auto visibleCharts = [&] {
        int n = 0;
        for (TrackChart* v : plots->findChildren<TrackChart*>()) n += v->isVisibleTo(plots);
        return n;
    };
    const int all = visibleCharts();
    const auto twice = [&](TrackChart* v) {
        if (!v) return;
        const QPointF mid(v->width() / 2.0, v->height() * 0.6);
        QMouseEvent e(QEvent::MouseButtonDblClick, mid, v->mapToGlobal(mid), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(v, &e);
        pump(50);
    };
    twice(plotted(w, "Обучение"));
    const int alone = visibleCharts();
    const bool back = plots->findChild<QToolButton*>("plotFocusBack") != nullptr;
    twice(plotted(w, "Обучение"));
    std::printf("  charts in the plots: %d; one double-clicked: %d, a way back %s; again: %d\n", all, alone, back ? "yes" : "no", visibleCharts());
    c.check(all > 1 && alone == 1 && back && visibleCharts() == all && !plots->findChild<QToolButton*>("plotFocusBack"),
            "a double click shows the chart alone in the whole width of the plots, another brings every chart back");
    train(3, 3); // the step went back: a new training, a new history
    c.check(best && best->size() == 3 && best->time(0) == 0, "the step going back starts a new history");
    train(2, 2, 2); // a new training starts again at the generation the last one ended on: still a new history
    c.check(best && best->size() == 2 && best->time(0) == 2, "a step not advancing (equal to the last) starts a new history too");
    chart = plotted(w, "Обучение");
    if (!chart) return c.check(false, "the step chart stays in the plots"), void();
    const QString tip = plotHoverText(chart->tracks()[0], 2.4, chart->axisWords());
    c.check(tip.contains("поколение 2") && !tip.contains("2,4"), "a step chart's tooltip says the generation of the nearest point, not the mouse's x");
    for (PlotPanel::Arrangement a : {PlotPanel::Arrangement::Normalized, PlotPanel::Arrangement::Lanes, PlotPanel::Arrangement::Overlay}) {
        plots->setArrangement(a);
        pump(50);
        c.check(plots->pluginSections().size() == 1 && plots->pluginSections()[0].second.size() == 1, "the plugin's section of the plots is in every layout");
        c.check(treeRow(plots, uiText("Kinetica", "Сцена")) != nullptr, "«Что показывать» is there in every layout, «Нормировать» too");
    }
    QDir(folder + "/trainer").removeRecursively();
    c.check(waitUntil([&] { return plots->pluginSections().empty(); }, 5000),
            "the plugin gone, its section of the plots goes with its panel");
}

} // namespace

void testPluginPanels(Checker& c, QMainWindow& w, SceneBuilder& b, EditorOps* ops, PluginHost* host, const QString& folder) {
    testChart(c, w, b, ops, host, folder);
    testStepChart(c, w, ops, host, folder);
    testSvg(c, w, ops, host, folder);
    testWords(c, w, ops, host, folder);
}
