// The Laboratory in the window (see MainWindow.h): the «Лаборатория» button and F8, the dock with the
// layers, the card and the profiler (LabPanel), and what a click in the view means while the Laboratory is open:
//   1. a contact point within 10 px of the cursor (the contacts of the frame on screen) - its card:
//      the two bodies, the depth, the normal, the impulse;
//   2. else the body under the cursor - its card: mass, moments of inertia about its own axes,
//      velocity, angular velocity, sleep; live from the simulation thread (the physics engine's state
//      through ke::Simulation), or from the kept frame when the timeline shows the past (then only
//      what the frame holds: the pose and the sleep).
// The thing on the card gets a ring in the view, so the numbers and the place belong together.
#include "MainWindow.h"
#include "Icons.h"
#include "UiLanguage.h"

#include "LabCard.h"
#include "LabPanel.h"
#include "SequencerPanel.h"
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
    if (mm > 0.05f) return uiText("Kinetica", "проникли друг в друга");
    if (mm >= -0.05f) return c.normalImpulse > 0 ? uiText("Kinetica", "касаются и давят") : uiText("Kinetica", "касаются");
    return c.normalImpulse > 0 ? uiText("Kinetica", "на подлёте: движок тормозит") : uiText("Kinetica", "зазор: ещё не касаются");
}

// What a body of the physics engine is called in the scene: the object it is (the frame says which),
// else the floor (the world's own body), else «тело №n».
QString bodyName(const SceneBuilder& b, const ke::Frame& s, int body) {
    if (body < 0) return uiText("Kinetica", "неподвижный мир");
    for (const ke::Frame::Body& f : s.bodies)
        if (f.body == body) {
            const ke::SceneObject* o = ke::findObject(b.graph(), f.object);
            if (!o && ke::isArrayCopyId(f.object)) o = ke::findObject(b.graph(), ke::arrayOfCopyId(f.object));
            if (o && !o->name.empty()) return QString::fromStdString(o->name) + QString(uiText("Kinetica", " (тело №%1)")).arg(body);
            return QString(uiText("Kinetica", "тело №%1")).arg(body);
        }
    return body == 0 ? QString(uiText("Kinetica", "пол")) : QString(uiText("Kinetica", "тело №%1")).arg(body);
}

} // namespace

// The button and the key, the dock (at the left, hidden), the timeline on the view.
void MainWindow::buildLaboratory() {
    labAct_ = new QAction(uiText("Kinetica", "Лаборатория"), this);
    labAct_->setObjectName("actionLaboratory");
    labAct_->setCheckable(true);
    labAct_->setShortcut(QKeySequence("F8"));
    labAct_->setToolTip(uiText("Kinetica", "Лаборатория (F8): что считает движок — контакты, силы, тела, суставы; карточка по щелчку, "
                        "время каждой стадии"));
    labDock_ = new QDockWidget(uiText("Kinetica", "Лаборатория"), this);
    labDock_->setWindowIcon(panelIcon(PanelIcon::Laboratory));
    labDock_->setObjectName("labDock");
    lab_ = new LabPanel;
    lab_->addActionRow(uiText("Kinetica", "Тела"), collidersAct_, QColor(110, 232, 130)); // the window's wireframes, among the engine's drawings
    labDock_->setWidget(lab_);
    labDock_->setMinimumWidth(290);
    addDockWidget(Qt::LeftDockWidgetArea, labDock_);
    labDock_->hide();
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
}

void MainWindow::setLaboratoryVisible(bool on) {
    labAct_->setChecked(on);
    labDock_->setVisible(on);
    if (on) labDock_->raise();
    view_->setLabInspect(on);
    updateChannelRequests(); // the scene's energy and momentum are measured while the Laboratory is open
    if (!on) view_->setLabMarker(false);
    lab_->syncFromProbe();
}

// Every new frame of a run is timed by the profiler while the Laboratory is open (a paused scene publishes the same
// frame again after each command: once). Nothing of a run is kept: a plain «Пуск» stores no frames - a run is kept
// only by «Запись», as a sequence (a run's last frames were kept, 512 MB at most, and the Sequencer's playhead moved
// over a run nobody recorded: a person took it for a recording).
void MainWindow::labProfile(const std::vector<std::shared_ptr<const ke::Frame>>& frames) {
    if (builder_->editing() || !labAct_->isChecked()) return;
    for (const auto& s : frames) {
        if (!s || s->frame == labFrame_) continue;
        labFrame_ = s->frame;
        lab_->profiler()->addFrame(s->probe);
    }
}

// The frame the view is about to draw: the layer boxes follow the Probe.
void MainWindow::labSync() {
    if (labAct_->isChecked()) lab_->syncFromProbe();
}

bool MainWindow::sceneRunning() const { return builder_->mode() == SceneBuilder::Mode::Playing; }

void MainWindow::labPlot(const QString& channel) {
    plots_->showChannel(channel.toStdString());
    setGraphsVisible(true);
    const QString name = plots_->catalog().card(channel.toStdString()).name;
    statusBar()->showMessage(uiText("Kinetica", "График «") + name + uiText("Kinetica", "» — внизу, в «Графиках» (F9)"), 4000);
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
    lab_->card()->showThing(QString(uiText("Kinetica", "Точка контакта %1 из %2")).arg(index + 1).arg(s.contacts.size()), pair,
                            {{uiText("Kinetica", "Глубина"), num(double(c.depth) * 1000.0, 2), uiText("Kinetica", "мм"), ""},
                             {uiText("Kinetica", "Касание"), touchWords(c), "", ""},
                             {uiText("Kinetica", "Нормаль, от A к B"), vec(c.normal, 3), "", ""},
                             {uiText("Kinetica", "Нормальный импульс"), num(double(c.normalImpulse), 4), uiText("Kinetica", "Н·с"), ""},
                             {uiText("Kinetica", "Где"), vec(c.position, 3), uiText("Kinetica", "м"), ""},
                             {uiText("Kinetica", "Кадр"), QString::number(s.frame) + ", t = " + num(double(s.time), 2), uiText("Kinetica", "с"), ""},
                             {uiText("Kinetica", "Контактов в сцене"), QString::number(qint64(contacts)), "", ""}});
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
    if (builder_->editing() || f.object == 0) return; // nothing live to ask
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
    const QString dash = uiText("Kinetica", "— только вживую");
    std::vector<LabCard::Row> rows;
    if (f.live && !f.alive) rows.push_back({uiText("Kinetica", "Состояние"), uiText("Kinetica", "не стало телом"), "", ""});
    // A number the simulation measures for one object is plotted from its channel, «<label>/<quantity>».
    auto channel = [&f](const char* quantity) { return f.label + "/" + quantity; };
    rows.push_back({uiText("Kinetica", "Масса"), f.live ? (f.fixed ? QString(uiText("Kinetica", "∞ (неподвижное)")) : num(f.mass, 3)) : dash, f.live && !f.fixed ? uiText("Kinetica", "кг") : "",
                    channel("mass")});
    rows.push_back({uiText("Kinetica", "Моменты инерции (оси тела)"), f.live && !f.fixed ? vec(f.inertia, 4) : (f.live ? QString("∞") : dash),
                    f.live && !f.fixed ? uiText("Kinetica", "кг·м²") : "", ""});
    rows.push_back({uiText("Kinetica", "Скорость"), f.live ? vec(f.vel, 3) : dash, f.live ? uiText("Kinetica", "м/с") : "", ""});
    rows.push_back({"|v|", f.live ? num(ke::length(f.vel), 3) : dash, f.live ? uiText("Kinetica", "м/с") : "", channel("speed")});
    rows.push_back({uiText("Kinetica", "Угловая скорость"), f.live ? vec(f.angVel, 3) : dash, f.live ? uiText("Kinetica", "рад/с") : "", ""});
    rows.push_back({"|ω|", f.live ? num(ke::length(f.angVel), 3) : dash, f.live ? uiText("Kinetica", "рад/с") : "", channel("angular speed")});
    rows.push_back({f.live ? uiText("Kinetica", "Центр масс") : uiText("Kinetica", "Где"), vec(f.pos, 3), uiText("Kinetica", "м"), ""});
    rows.push_back({uiText("Kinetica", "Высота над полом"), f.live && std::isfinite(f.height) ? num(f.height, 3) : dash, f.live ? uiText("Kinetica", "м") : "", channel("height")});
    rows.push_back({uiText("Kinetica", "Сон"), f.sleeping ? QString(uiText("Kinetica", "спит")) : QString(uiText("Kinetica", "не спит")), "", channel("asleep")});
    if (const auto s = view_->snapshot()) {
        double kinetic = std::nan(""), rotational = 0;
        for (const ke::Measurement& m : s->measurements) {
            if (m.info.id == "rigid/kinetic energy") kinetic = m.value;
            if (m.info.id == "rigid/rotational energy") rotational = m.value;
        }
        int awake = 0;
        for (const ke::Frame::Body& b : s->bodies) awake += b.body >= 0 && b.movable && !b.sleeping;
        if (std::isfinite(kinetic))
            rows.push_back({uiText("Kinetica", "Энергия движения всех тел"), num(kinetic + rotational, 3), uiText("Kinetica", "Дж"), "scene/kinetic energy"});
        rows.push_back({uiText("Kinetica", "Тел не спит"), QString::number(awake), "", ""});
    }
    lab_->card()->showThing(uiText("Kinetica", "Тело"), f.label.empty() ? QString(uiText("Kinetica", "тело")) : QString::fromStdString(f.label) + (f.live ? "" : uiText("Kinetica", " · из сохранённого кадра")), rows);
    lab_->card()->setButton(QString(), QString(), nullptr);
}
