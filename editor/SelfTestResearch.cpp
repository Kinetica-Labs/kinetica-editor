// Analytical distributions, stable body targets and the simulation's real channels through the research
// UI. Includes retained-history thinning, missing measurements and a body of the robot arm picked by the mouse.
#include "SelfTestSupport.h"
#include "PlotPanel.h"
#include "MainWindow.h"

#include <QComboBox>
#include <QLabel>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QMainWindow>

#include <cmath>
#include <limits>
#include <numeric>

using namespace selftest;

namespace {

void distributions(Checker& c) {
    PlotSeries samples;
    samples.setKeep(true);
    for (int i = 0; i < 3; ++i) samples.add(double(i), double(i + 1));
    samples.add(3, std::nan(""));
    const auto s = plotStatistics(samples, 0, 3, 3);
    c.check(s.count == 3 && s.gaps == 1 && s.mean == 2 && std::abs(s.deviation - std::sqrt(2.0 / 3)) < 1e-12 &&
            s.bins == std::vector<size_t>({1, 1, 1}), "histogram: analytic mean, population deviation, edge bins and a missing sample");
    const auto window = plotStatistics(samples, 1, 2, 2);
    c.check(window.count == 2 && window.firstTime == 1 && window.lastTime == 2 && window.mean == 2.5,
            "histogram: the time interval includes both endpoints");
    samples.clear();
    for (int i = 0; i < 4; ++i) samples.add(i, 0);
    const auto zero = plotStatistics(samples, 0, 3, 20);
    c.check(zero.count == 4 && zero.mean == 0 && zero.deviation == 0 && zero.upperEdge > zero.lowerEdge &&
            std::accumulate(zero.bins.begin(), zero.bins.end(), size_t(0)) == 4, "histogram: a constant zero signal keeps all samples");
    samples.clear();
    for (int i = 0; i < 3; ++i) samples.add(i, 1e12 + 2 * (i - 1));
    const auto large = plotStatistics(samples, 0, 2, 20);
    c.check(large.mean == 1e12 && std::abs(large.deviation - std::sqrt(8.0 / 3)) < 1e-12,
            "statistics: a large offset does not cancel the small variance");
    samples.clear();
    for (int i = 0; i <= 60000; ++i) samples.add(i, i < 24001 ? 100 : 2);
    const auto raw = plotStatistics(samples, -std::numeric_limits<double>::infinity(), 60000, 20);
    c.check(raw.shortened && raw.count == 36000 && raw.minimum == 2 && raw.maximum == 2,
            "histogram: old retained peaks are excluded instead of biasing the distribution");
    samples.clear();
    samples.add(0, 7);
    c.check(samples.firstRaw() == 0 && plotStatistics(samples, 0, 0, 20).count == 1, "a cleared run has no stale thinning boundary");
}

void targets(Checker& c) {
    PlotResearch research;
    auto* object = research.findChild<QComboBox*>("plotObject");
    research.setObjects({{0, "Пол"}, {4, "Куб"}, {8, "Кольцо"}}, 4);
    c.check(research.body() == 4 && research.bodyLabel() == "Куб" && object->currentText().contains("Куб"),
            "research: follow the selected object's stable body index and visible name");
    object->setCurrentIndex(object->findData(8));
    research.setObjects({{0, "Пол"}, {4, "Куб"}, {8, "Кольцо"}}, 4);
    c.check(research.body() == 8, "research: a pinned body survives another inspector selection");
    research.setChannels({{"Кольцо/speed", "Кольцо — скорость", "м/с", PlotGroup::Object},
                          {"Куб/speed", "Куб — скорость", "м/с", PlotGroup::Object},
                          {"scene/mechanical energy", "Полная энергия", "Дж", PlotGroup::Scene}});
    auto* quantity = research.findChild<QComboBox*>("plotQuantity");
    c.check(quantity->count() == 1 && research.channel() == "Кольцо/speed", "research: body quantities exclude other bodies and scene totals");
    research.setObjects({{0, "Пол"}, {4, "Куб"}, {9, "Осколок"}}, 4);
    c.check(research.body() == -1 && research.channel() == "scene/mechanical energy", "research: a destroyed pinned body cannot silently select a fragment");
    research.resetObjects();
    c.check(object->count() == 2 && research.body() == -1 && research.channel().empty(), "research: a new scene forgets old body slots and channel names");
}

void live(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    auto* panel = w.findChild<PlotPanel*>();
    b.newScene();
    b.createActions()[0]->trigger();
    b.editEntity(last(b).id, [](ke::Entity& e) { e.position = ke::Vector3(0, 3, 0); });
    clickRole(w, RoleIcon::Rigid);
    trigger(w, "actionPlay");
    c.check(waitFrames(v, 30, 5000), "research: the simulation advances");
    auto* research = panel->research();
    auto* object = research->findChild<QComboBox*>("plotObject");
    int body = -1;
    for (const auto& drawn : v->snapshot()->bodies) if (drawn.movable) body = int(drawn.object); // the research names an object by its entity
    object->setCurrentIndex(object->findData(body));
    c.check(waitFrames(v, v->snapshot()->frame + 20, 5000), "research: selected-body measurements advance");
    b.pause();
    pump(200);
    const std::string id = research->bodyLabel().toStdString() + "/speed";
    auto* quantity = research->findChild<QComboBox*>("plotQuantity");
    quantity->setCurrentIndex(quantity->findData(QString::fromStdString(id)));
    research->findChild<QComboBox*>("plotView")->setCurrentIndex(1);
    pump(300);
    c.check(research->body() == body && research->result().count > 3 && research->result().maximum > 0 && panel->series(id),
            "research: a body selected from the list records its measured speed and produces a real distribution");
    c.check(panel->series("scene/mechanical energy") && !panel->objectTitle().isEmpty(), "research: scene history remains available beside the object's channels");
    QTemporaryDir output;
    const QString csvPath = output.path() + "/distribution.csv";
    c.check(research->writeCsv(csvPath), "research: histogram CSV export succeeds");
    QFile csv(csvPath);
    csv.open(QIODevice::ReadOnly);
    const auto lines = QString::fromUtf8(csv.readAll()).split('\n');
    size_t total = 0;
    for (const QString& line : lines) {
        const auto fields = line.split(',');
        if (fields.size() == 4 && fields[0] != "lower") total += size_t(fields[2].toULongLong());
    }
    c.check(total == research->result().count && lines[0].contains("unit=м/с") && lines[0].contains("weighting=frame"),
            "research: exported bin counts sum to N and preserve units and weighting");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/research-body-histogram.png");
    object->setCurrentIndex(object->findData(-1));
    pump(200);
    c.check(research->channel() == "scene/mechanical energy" && research->result().count > 3 && panel->objectTitle().isEmpty(),
            "research: switching to the scene gives its own energy distribution");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/research-scene-histogram.png");
    research->findChild<QComboBox*>("plotView")->setCurrentIndex(0);
    b.stop();
}

// The robot arm of the examples, playing: a mouse click on one of its links makes that link the research
// target and the builder's selection; its measured quantities come with the next frames.
void armMouse(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    QString error;
    if (!b.openFile(MainWindow::examplesDir() + "/robot-arm.kscene", error)) return c.check(false, "the robot arm example opens");
    b.play();
    waitFrames(v, 30, 10000);
    b.pause();
    pump(250);
    uint32_t object = 0;
    ke::Vector3 position;
    for (const auto& body : v->snapshot()->bodies)
        if (body.movable && !body.marker) { object = body.object; position = body.pos; break; }
    auto* research = w.findChild<PlotPanel*>()->research();
    const QPointF point = toPoint(v->gizmoView().project(position));
    int picked = -1; ke::Vector3 hit;
    const bool canPick = v->pickAnyBody(v->camera().screenRay(point, v->size()), picked, hit);
    const uint32_t under = canPick ? v->snapshot()->bodies[size_t(picked)].object : 0;
    sendMouse(v, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    sendMouse(v, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    pump(150);
    std::printf("  arm mouse: link %u, under the cursor %u, research target %d, selected %u\n", object, under, research->body(), b.selectedId());
    if (!dir.isEmpty()) windowShot(w).save(dir + "/research-arm-selection.png");
    c.check(canPick && under != 0 && research->body() == int(under) && b.selectedId() == b.clickTarget(under),
            "research: a mouse click on a link of the playing arm makes it the research target and the selection");
    b.play();
    c.check(waitFrames(v, v->snapshot()->frame + 20, 5000), "research: the selected link advances");
    b.pause();
    pump(100);
    c.check(!research->channel().empty(), "research: the link's measured quantities are available");
    b.stop();
}

} // namespace

int runResearchTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir) {
    Checker c;
    distributions(c);
    targets(c);
    live(c, w, b, v, shotsDir);
    armMouse(c, w, b, v, shotsDir);
    return c.failures;
}
