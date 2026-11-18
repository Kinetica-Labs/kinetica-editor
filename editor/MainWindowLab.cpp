// The Laboratory in the window (see MainWindow.h): the «Лаборатория» button and F8, the dock with the
// layers, the card and the profiler (LabPanel), the timeline strip on the view (LabTimeline), and what
// a click in the view means while the Laboratory is open:
//   1. a contact point within 10 px of the cursor (the contacts of the frame on screen) - its card:
//      the two bodies, the depth, the normal, the impulse;
//   2. else the body under the cursor - its card: mass, moments of inertia about its own axes,
//      velocity, angular velocity, sleep; live from the simulation thread (the physics engine's state
//      through ke::Simulation), or from the kept frame when the timeline shows the past (then only
//      what the frame holds: the pose and the sleep).
// The thing on the card gets a ring in the view, so the numbers and the place belong together.
#include "MainWindow.h"

#include "LabCard.h"
#include "LabPanel.h"
#include "LabTimeline.h"
#include "PlotPanel.h"
#include "ProfilerBar.h"
#include "SceneBuilder.h"
#include "Viewport.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QStatusBar>

#include <cmath>

using ke::Vector3;

namespace {

// A number the Russian way: a decimal comma, `digits` after it; a tiny negative one is plain 0
// («-0,00» reads like a mistake).
QString num(double v, int digits) {
    QString s = QString::number(v, 'f', digits);
    if (s.startsWith('-') && s.toDouble() == 0.0) s.remove(0, 1);
    return s.replace('.', ',');
}
QString vec(const Vector3& v, int digits) {
    return QString("(%1; %2; %3)").arg(num(v.x, digits), num(v.y, digits), num(v.z, digits));
}

// A contact point in words, by its depth and its push. Within 0,05 mm of the surface the bodies touch
// (the engine holds a resting contact there); a negative depth is a gap - a speculative point the
// engine already brakes when it has an impulse, one that only waits when it has none.
QString touchWords(const ke::Frame::ContactInfo& c) {
    const float mm = float(c.depth) * 1000.0f;
    if (mm > 0.05f) return "проникли друг в друга";
    if (mm >= -0.05f) return c.normalImpulse > 0 ? "касаются и давят" : "касаются";
    return c.normalImpulse > 0 ? "на подлёте: движок тормозит" : "зазор: ещё не касаются";
}

// What a body of the physics engine is called in the scene: the object it is (the frame says which),
// else the floor (the world's own body), else «тело №n».
QString bodyName(const SceneBuilder& b, const ke::Frame& s, int body) {
    if (body < 0) return "неподвижный мир";
    for (const ke::Frame::Body& f : s.bodies)
        if (f.body == body) {
            const ke::SceneObject* o = ke::findObject(b.graph(), f.object);
            if (!o && ke::isArrayCopyId(f.object)) o = ke::findObject(b.graph(), ke::arrayOfCopyId(f.object));
            if (o && !o->name.empty()) return QString::fromStdString(o->name) + QString(" (тело №%1)").arg(body);
            return QString("тело №%1").arg(body);
        }
    return body == 0 ? QString("пол") : QString("тело №%1").arg(body);
}

} // namespace

// The button and the key, the dock (at the left, hidden), the timeline on the view.
void MainWindow::buildLaboratory() {
    labAct_ = new QAction("Лаборатория", this);
    labAct_->setObjectName("actionLaboratory");
    labAct_->setCheckable(true);
    labAct_->setShortcut(QKeySequence("F8"));
    labAct_->setToolTip("Лаборатория (F8): что считает движок — контакты, силы, тела, суставы; карточка по щелчку, "
                        "запись последних кадров, время каждой стадии");
    labDock_ = new QDockWidget("Лаборатория", this);
    labDock_->setObjectName("labDock");
    lab_ = new LabPanel;
    labDock_->setWidget(lab_);
    labDock_->setMinimumWidth(290);
    addDockWidget(Qt::LeftDockWidgetArea, labDock_);
    labDock_->hide();
    timeline_ = new LabTimeline(view_);
    menuBar()->findChild<QMenu*>("viewMenu")->addAction(labAct_);
    connectLaboratory();
}

void MainWindow::connectLaboratory() {
    connect(labAct_, &QAction::toggled, this, &MainWindow::setLaboratoryVisible);
    connect(labDock_, &QDockWidget::visibilityChanged, this, [this](bool on) {
        if (!on && labDock_->isHidden()) labAct_->setChecked(false); // closed by its own ✕
    });
    connect(view_, &Viewport::labClicked, this, &MainWindow::onLabClicked);
    connect(lab_->card(), &LabCard::plotRequested, this, &MainWindow::labPlot);
    connect(lab_->profiler(), &ProfilerBar::plotRequested, this, &MainWindow::labPlot);
    connect(timeline_, &LabTimeline::showFrame, this, [this](LabTimeline::Frame s) { view_->setSnapshot(std::move(s)); });
    connect(timeline_, &LabTimeline::scrubStarted, this, &MainWindow::labScrubStarted);
    connect(timeline_, &LabTimeline::wentLive, this, &MainWindow::labWentLive);
}

void MainWindow::setLaboratoryVisible(bool on) {
    labAct_->setChecked(on);
    labDock_->setVisible(on);
    if (on) labDock_->raise();
    view_->setLabInspect(on);
    updateChannelRequests(); // the scene's energy and momentum are measured while the Laboratory is open
    if (!on) view_->setLabMarker(false);
    lab_->syncFromProbe();
    updateLabTimeline();
}

// The strip is there while the Laboratory is open and something simulated is on screen (not while the
// scene is edited: then there is nothing to replay). Leaving it shows the live frame again, quietly.
void MainWindow::updateLabTimeline() {
    if (!timeline_) return;
    const bool show = labAct_->isChecked() && !builder_->editing();
    if (!show && timeline_->showingPast()) {
        timeline_->blockSignals(true);
        timeline_->goLive();
        timeline_->blockSignals(false);
        labResume_ = false;
    }
    timeline_->setVisible(show);
}

// Every simulated frame: kept by the timeline, timed by the profiler. A new frame while the past is on
// screen means the scene runs again (▶ was pressed): the view follows it live.
// The recording keeps every frame the simulation made, drawn or not (a paused scene publishes the same
// frame again after each command: the timeline keeps it once). The profiler learns each new frame too.
void MainWindow::labRecord(const std::vector<std::shared_ptr<const ke::Frame>>& frames) {
    if (!timeline_ || builder_->editing()) return; // edit mode shows the authored scene, not a run
    for (const auto& s : frames) {
        const LabTimeline::Frame before = timeline_->newest();
        timeline_->record(s);
        if (timeline_->newest() == before) continue;
        labFresh_ = true;
        if (labAct_->isChecked()) lab_->profiler()->addFrame(s->probe);
    }
}

// The frame the view is about to draw: the layer boxes follow the Probe; new frames of a running scene
// take a timeline that shows the past back to «Вживую» (someone pressed ▶ Пуск).
bool MainWindow::labOnSnapshot() {
    if (!timeline_) return true;
    if (labAct_->isChecked()) lab_->syncFromProbe();
    if (timeline_->showingPast() && labFresh_ && sceneRunning()) timeline_->goLive();
    labFresh_ = false;
    return !timeline_->showingPast();
}

bool MainWindow::sceneRunning() const { return builder_->mode() == SceneBuilder::Mode::Playing; }

void MainWindow::labScrubStarted() {
    if (!sceneRunning()) return;
    labResume_ = true;
    builder_->pause();
}

void MainWindow::labWentLive() {
    if (!labResume_) return;
    labResume_ = false;
    builder_->play();
}

void MainWindow::labPlot(const QString& channel) {
    plots_->showChannel(channel.toStdString());
    setGraphsVisible(true);
    const QString name = plots_->catalog().card(channel.toStdString()).name;
    statusBar()->showMessage("График «" + name + "» — внизу, в «Графиках» (F9)", 4000);
}

// 1. The contact nearest to the click on the screen, if within 10 px. 2. Else the body under it.
// 3. Else the card goes back to its hint.
void MainWindow::onLabClicked(QPointF pos) {
    const auto s = view_->snapshot();
    if (!s) return;
    const GizmoView gv = view_->gizmoView();
    int best = -1;
    float bestDist = 10.0f;
    for (int i = 0; i < int(s->contacts.size()); ++i) {
        const ke::Vector2 p = gv.project(s->contacts[size_t(i)].position);
        const float d = std::hypot(p.x - float(pos.x()), p.y - float(pos.y()));
        if (d < bestDist) bestDist = d, best = i;
    }
    if (best >= 0) return showContactCard(*s, best);
    int body;
    Vector3 hit;
    if (view_->pickAnyBody(view_->camera().screenRay(pos, view_->size()), body, hit)) return showBodyCard(body);
    lab_->card()->showHint();
    view_->setLabMarker(false);
}

void MainWindow::showContactCard(const ke::Frame& s, int index) {
    // The engine's normal points from A towards B: it pushes B along it and A the other way, and the
    // «Нормали» layer draws it so. The card names A and B to match.
    const ke::Frame::ContactInfo& c = s.contacts[size_t(index)];
    const QString pair = "A: " + bodyName(*builder_, s, c.bodyA) + "   ·   B: " + bodyName(*builder_, s, c.bodyB);
    const double contacts = s.probe.value("physics/contacts", double(s.contacts.size()));
    lab_->card()->showThing(QString("Точка контакта %1 из %2").arg(index + 1).arg(s.contacts.size()), pair,
                            {{"Глубина", num(double(c.depth) * 1000.0, 2), "мм", ""},
                             {"Касание", touchWords(c), "", ""},
                             {"Нормаль, от A к B", vec(c.normal, 3), "", ""},
                             {"Нормальный импульс", num(double(c.normalImpulse), 4), "Н·с", ""},
                             {"Где", vec(c.position, 3), "м", ""},
                             {"Кадр", QString::number(s.frame) + ", t = " + num(double(s.time), 2), "с", ""},
                             {"Контактов в сцене", QString::number(qint64(contacts)), "", ""}});
    lab_->card()->setButton(QString(), QString(), nullptr);
    view_->setLabMarker(true, c.position);
}

// The body's card: at once from the frame on screen, then (live) the simulation thread's numbers - the
// physics engine's state of the body the entity became; its moments of inertia about its own axes
// (the world's tensor turned back into them).
void MainWindow::showBodyCard(int body) {
    const auto s = view_->snapshot();
    LabBodyFacts f;
    f.body = body;
    if (s && body >= 0 && body < int(s->bodies.size())) {
        const ke::Frame::Body& b = s->bodies[size_t(body)];
        f.object = b.object;
        f.pos = b.pos;
        f.sleeping = b.sleeping;
        f.fixed = !b.movable;
    }
    f.label = watchLabel(int(f.object)).toStdString();
    fillBodyCard(f);
    view_->setLabMarker(true, f.pos);
    if (timeline_->showingPast() || builder_->editing() || f.object == 0) return; // a kept frame: nothing live to ask
    watchBody(int(f.object)); // its channels from the next frame on: the card's numbers can be plotted
    const ke::ObjectRef ref{f.object, f.label};
    QPointer<MainWindow> self(this);
    ctrl_->post([self, body, ref](ke::Simulation& sim) {
        LabBodyFacts live;
        live.body = body;
        live.object = ref.entity;
        live.live = true;
        live.label = ref.label;
        const int b = sim.robot() ? sim.robot()->bodyOf(ref.entity) : -1;
        live.alive = b >= 0 && sim.physics()->alive(b);
        if (live.alive) {
            const ke::BodyState st = sim.physics()->state(b);
            live.fixed = st.mass <= 0, live.sleeping = !st.awake;
            live.mass = float(st.mass), live.pos = st.centre, live.vel = st.velocity, live.angVel = st.angularVelocity;
            const ke::Matrix3x3 R = st.rotation.toMatrix3x3(), local = R.transposed() * st.inertia * R;
            for (int k = 0; k < 3; ++k) live.inertia[k] = local.m[k][k];
            for (const ke::Measurement& m : ke::measureObject(sim, ref)) // the height as the plot will show it
                if (m.info.id == ref.label + "/height") live.height = float(m.value);
        }
        QMetaObject::invokeMethod(qApp, [self, live] {
            if (self) self->fillBodyCard(live);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::fillBodyCard(const LabBodyFacts& f) {
    const QString dash = "— только вживую";
    std::vector<LabCard::Row> rows;
    if (f.live && !f.alive) rows.push_back({"Состояние", "не стало телом", "", ""});
    // A number the simulation measures for one object is plotted from its channel, «<label>/<quantity>».
    auto channel = [&f](const char* quantity) { return f.label + "/" + quantity; };
    rows.push_back({"Масса", f.live ? (f.fixed ? QString("∞ (неподвижное)") : num(f.mass, 3)) : dash, f.live && !f.fixed ? "кг" : "",
                    channel("mass")});
    rows.push_back({"Моменты инерции (оси тела)", f.live && !f.fixed ? vec(f.inertia, 4) : (f.live ? QString("∞") : dash),
                    f.live && !f.fixed ? "кг·м²" : "", ""});
    rows.push_back({"Скорость", f.live ? vec(f.vel, 3) : dash, f.live ? "м/с" : "", ""});
    rows.push_back({"|v|", f.live ? num(ke::length(f.vel), 3) : dash, f.live ? "м/с" : "", channel("speed")});
    rows.push_back({"Угловая скорость", f.live ? vec(f.angVel, 3) : dash, f.live ? "рад/с" : "", ""});
    rows.push_back({"|ω|", f.live ? num(ke::length(f.angVel), 3) : dash, f.live ? "рад/с" : "", channel("angular speed")});
    rows.push_back({f.live ? "Центр масс" : "Где", vec(f.pos, 3), "м", ""});
    rows.push_back({"Высота над полом", f.live && std::isfinite(f.height) ? num(f.height, 3) : dash, f.live ? "м" : "", channel("height")});
    rows.push_back({"Сон", f.sleeping ? QString("спит") : QString("не спит"), "", channel("asleep")});
    if (const auto s = view_->snapshot()) {
        double kinetic = std::nan(""), rotational = 0;
        for (const ke::Measurement& m : s->measurements) {
            if (m.info.id == "rigid/kinetic energy") kinetic = m.value;
            if (m.info.id == "rigid/rotational energy") rotational = m.value;
        }
        int awake = 0;
        for (const ke::Frame::Body& b : s->bodies) awake += b.body >= 0 && b.movable && !b.sleeping;
        if (std::isfinite(kinetic))
            rows.push_back({"Энергия движения всех тел", num(kinetic + rotational, 3), "Дж", "scene/kinetic energy"});
        rows.push_back({"Тел не спит", QString::number(awake), "", ""});
    }
    lab_->card()->showThing("Тело", f.label.empty() ? QString("тело") : QString::fromStdString(f.label) + (f.live ? "" : " · из сохранённого кадра"), rows);
    lab_->card()->setButton(QString(), QString(), nullptr);
}
