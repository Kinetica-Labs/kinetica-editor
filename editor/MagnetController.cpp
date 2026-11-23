// The magnet (MagnetController.h): the polygon under the mouse, the moving ghost of the ready link a
// click would put (the API's assembly_preview), the pie of the five elements, the click (assembly_preset) and
// the two clicks that join two shapes (assembly_joint).
#include "MagnetController.h"
#include "UiLanguage.h"

#include "Icons.h"

#include "EditorOps.h"
#include "JointGlyphs.h"
#include "PieMenu.h"
#include "SceneBuilder.h"
#include "Viewport.h"

#include <algorithm>
#include "math/ElementaryFunctions.h"
#include "scene/Assembly.h"
#include "scene/SceneEdits.h"

#include <QGuiApplication>
#include <QMap>
#include <QAction>
#include <QActionGroup>
#include <QJsonArray>
#include <QLineF>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <algorithm>

using ke::Vector3;

namespace {

const char* kAxisNames[] = {QT_TRANSLATE_NOOP("Kinetica", "ось поперёк грани"), QT_TRANSLATE_NOOP("Kinetica", "ось поперёк, другая"), QT_TRANSLATE_NOOP("Kinetica", "ось по нормали")};

QJsonArray json(const Vector3& v) { return QJsonArray{double(v.x), double(v.y), double(v.z)}; }

Vector3 vec(const QJsonValue& v) {
    const QJsonArray a = v.toArray();
    return Vector3(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()));
}

// A horseshoe magnet, red and blue poles, drawn once.
QIcon magnetIcon() {
    QPixmap pm(48, 48);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath u;
    u.moveTo(12, 8);
    u.lineTo(12, 26);
    u.arcTo(12, 12, 24, 28, 180, 180);
    u.lineTo(36, 8);
    p.setPen(QPen(QColor(200, 205, 215), 9, Qt::SolidLine, Qt::FlatCap));
    p.drawPath(u);
    p.setPen(QPen(QColor(225, 70, 60), 9, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(12, 6, 12, 14);
    p.setPen(QPen(QColor(70, 120, 230), 9, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(36, 6, 36, 14);
    return QIcon(pm);
}

// The edges of a new entity's ghost as point pairs: a box's twelve; any other shape's mesh, every
// triangle's edges (its bounding box when the mesh is too fine to redraw at every frame).
std::vector<Vector3> ghostEdges(const ke::SceneGraph& g, const ke::Entity& e) {
    const ke::Quaternion q = ke::entityRotation(e);
    const ke::TriMesh m = e.shape == ke::ShapeKind::Box ? ke::TriMesh() : ke::entityLocalMesh(e, g.baseDirectory);
    std::vector<Vector3> out;
    if (m.triangles.empty() || m.triangles.size() > 3000) {
        std::vector<float> lines;
        appendBoxEdges(lines, e.position, q, e.size);
        for (size_t i = 0; i + 4 <= lines.size(); i += 4) out.push_back(Vector3(lines[i], lines[i + 1], lines[i + 2]));
        return out;
    }
    for (const auto& t : m.triangles)
        for (int k = 0; k < 3; ++k) {
            out.push_back(e.position + q.rotate(m.positions[t[k]]));
            out.push_back(e.position + q.rotate(m.positions[t[(k + 1) % 3]]));
        }
    return out;
}


} // namespace

MagnetController::MagnetController(EditorOps* ops, SceneBuilder* builder, Viewport* view, QObject* parent)
    : QObject(parent), ops_(ops), builder_(builder), view_(view), pie_(new PieMenu(view)), pieTimer_(new QTimer(this)),
      animation_(new QTimer(this)) {
    action_ = new QAction(magnetIcon(), uiText("Kinetica", "Магнит"), this);
    action_->setObjectName("magnetTool");
    action_->setCheckable(true);
    action_->setToolTip(uiText("Kinetica", "<b>Магнит</b>: готовое звено к грани, с сочленением.<br>"
                        "Щелчок — подсказанное (★); зажать и вести — круговое меню;<br>"
                        "Ctrl+щелчок по двум деталям — соединить.<br>"
                        "1–5 выбрать, 0 подсказка, A ось, Tab перевернуть, M зеркало, Esc выход"));
    action_->setMenu(buildMenu());
    jointAction_ = new QAction(svgIcon("joints/joint-tool", 48), uiText("Kinetica", "Сустав"), this);
    jointAction_->setObjectName("jointTool");
    jointAction_->setCheckable(true);
    jointAction_->setToolTip(uiText("Kinetica", "<b>Сустав</b>: соединить две стоящие детали.<br>"
                             "Наведите: центр грани — вал, ребро — петля, угол — шар; призрак показывает движение.<br>"
                             "Щелчок — место оси, щелчок по второй детали — готово (движется меньшая).<br>"
                             "0–4 вид, A ось, Tab перевернуть, M кто движется, Ctrl без привязки, Shift — ещё один, Esc"));
    auto* kinds = new QMenu(view_);
    auto* kindGroup = new QActionGroup(kinds);
    const std::pair<const char*, const char*> joinKinds[] = {{"auto", uiText("Kinetica", "0  ★ по месту: центр — вал, ребро — петля, угол — шар")},
                                                             {"hinge", uiText("Kinetica", "1  Шарнир — качается")}, {"slider", uiText("Kinetica", "2  Ползун — скользит")},
                                                             {"ball", uiText("Kinetica", "3  Шар — во все стороны")}, {"weld", uiText("Kinetica", "4  Сварка — намертво")},
                                                             {"cylinder", uiText("Kinetica", "5  Цилиндрическое — крутится и скользит вдоль оси")},
                                                             {"universal", uiText("Kinetica", "6  Карданное — две оси крест-накрест")},
                                                             {"planar", uiText("Kinetica", "7  Плоское — скользит по плоскости")},
                                                             {"shock", uiText("Kinetica", "8  Амортизатор — пружина с демпфером, несёт вес")}};
    for (int k = 0; k < 9; ++k) {
        QAction* a = kinds->addAction(svgIcon(QString("joints/") + (k == 8 ? "linear-actuator" : joinKinds[k].first), 24), joinKinds[k].second);
        a->setObjectName(QString("joint_") + joinKinds[k].first);
        a->setCheckable(true);
        a->setChecked(k == 0);
        kindGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, word = QString(joinKinds[k].first)] {
            joinType_ = word;
            if (!jointAction_->isChecked()) jointAction_->setChecked(true);
            else if (hasLast_) preview(last_);
        });
    }
    jointAction_->setMenu(kinds);
    connect(jointAction_, &QAction::toggled, this, &MagnetController::setJoining);
    pieTimer_->setSingleShot(true);
    pieTimer_->setInterval(200); // held this long without a drag: the pie opens
    animation_->setInterval(33);
    connect(pieTimer_, &QTimer::timeout, this, &MagnetController::openPie);
    connect(animation_, &QTimer::timeout, this, &MagnetController::animate);
    connect(action_, &QAction::toggled, this, &MagnetController::setActive);
    connect(view_, &Viewport::magnetMoved, this, &MagnetController::preview);
    connect(view_, &Viewport::magnetPressed, this, &MagnetController::press);
    connect(view_, &Viewport::magnetDragged, this, &MagnetController::drag);
    connect(view_, &Viewport::magnetReleased, this, &MagnetController::release);
    connect(view_, &Viewport::magnetKey, this, &MagnetController::key);
    connect(builder_, &SceneBuilder::modeChanged, this, [this] {
        if (!builder_->editing()) action_->setChecked(false);
    });
}

// The same choices as the keys, under the button's arrow (the parent is the view, so the API's
// ui_actions and ui_trigger find them).
QMenu* MagnetController::buildMenu() {
    auto* menu = new QMenu(view_);
    auto* group = new QActionGroup(menu);
    for (int k = 0; k <= ke::kMagnetCount; ++k) { // the suggestion, then the five elements with their icons
        const ke::LinkPreset e = k == 0 ? ke::LinkPreset::Hinge : ke::kMagnetPresets[k - 1];
        const QString text = k == 0 ? QString(uiText("Kinetica", "0  ★ подсказка по месту"))
                                   : QString("%1  %2").arg(k).arg(uiText("Kinetica", ke::presetName(e)));
        presetActs_[k] = k == 0 ? menu->addAction(text) : menu->addAction(linkIcon(LinkIcon(k - 1), 24), text);
        presetActs_[k]->setObjectName(k == 0 ? QString("magnetSuggested") : QString("magnet_") + ke::presetWord(e));
        presetActs_[k]->setCheckable(true);
        group->addAction(presetActs_[k]);
        connect(presetActs_[k], &QAction::triggered, this, [this, k] { pin(k - 1); });
    }
    presetActs_[0]->setChecked(true);
    menu->addSeparator();
    const std::pair<const char*, int> toggles[] = {{uiText("Kinetica", "A  повернуть ось"), Qt::Key_A}, {uiText("Kinetica", "Tab  перевернуть ось"), Qt::Key_Tab}, {uiText("Kinetica", "M  зеркально"), Qt::Key_M}};
    for (const auto& [text, k] : toggles) {
        QAction* a = menu->addAction(text);
        a->setObjectName(QString("magnetKey_") + QChar(k == Qt::Key_Tab ? 'T' : char(k)));
        connect(a, &QAction::triggered, this, [this, k = k] { key(k); });
    }
    return menu;
}

// «Сустав» on: the magnet's view mode, a plain click joins; the magnet's own button goes out (one tool at a time).
void MagnetController::setJoining(bool on) {
    if (on && !builder_->editing()) {
        jointAction_->setChecked(false);
        return emit statusMessage(uiText("Kinetica", "Сустав ставится в режиме правки — остановите сцену"));
    }
    if (on && action_->isChecked()) action_->setChecked(false);
    joining_ = on;
    joinFrom_ = JoinSnap();
    repick_ = 0;
    joinAxis_ = 0, swapMover_ = false;
    if (jointAction_->isChecked() != on) jointAction_->setChecked(on);
    view_->setMagnetMode(on);
    ghost_.clear();
    animation_->stop();
    view_->setMagnetPreview({}, {});
    pressed_ = connectFrom_ = Place();
    if (on) view_->setFocus();
    emit statusMessage(on ? uiText("Kinetica", "Сустав: наведите на грань — центр, ребро или угол подскажут вид") : QString());
}

void MagnetController::setActive(bool on) {
    if (on && jointAction_->isChecked()) jointAction_->setChecked(false);
    if (on && !builder_->editing()) {
        action_->setChecked(false);
        emit statusMessage(uiText("Kinetica", "Магнит работает в режиме правки — остановите сцену"));
        return;
    }
    if (action_->isChecked() != on) action_->setChecked(on);
    view_->setMagnetMode(on);
    pie_->hide();
    pieTimer_->stop();
    animation_->stop();
    ghost_.clear();
    pressed_ = connectFrom_ = Place();
    if (on) view_->setFocus(); // the keys go to the view at once, no click needed first
    emit statusMessage(on ? uiText("Kinetica", "Магнит: наведите на грань формы. Щелчок — подсказанное звено, зажать и вести — круговое меню") : QString());
}

MagnetController::Place MagnetController::placeAt(QPointF pos) const {
    Place p;
    p.found = builder_->pickShape(view_->camera().screenRay(pos, view_->size()), p.target, p.point, &p.normal);
    return p;
}

QJsonObject MagnetController::placeArgs(const Place& p, int preset) const {
    QJsonObject a{{"target", int(p.target)}, {"preset", ke::presetWord(ke::kMagnetPresets[preset])}, {"point", json(p.point)},
                  {"normal", json(p.normal)}, {"flip", flip_}, {"mirror", mirror_}};
    if (axis_ >= 0) a.insert("axis", axis_);
    return a;
}

int MagnetController::suggestion(const Place& p) const {
    if (pinned_ >= 0) return pinned_;
    const QJsonObject s = ops_->runNow("assembly_suggest", {{"target", int(p.target)}, {"point", json(p.point)}, {"normal", json(p.normal)}});
    ke::LinkPreset preset = ke::LinkPreset::Hinge;
    ke::presetFromWord(s.value("preset").toString().toStdString(), preset);
    const ke::LinkPreset element = ke::magnetPreset(preset); // the old suggestion as one of the five elements
    return int(std::find(ke::kMagnetPresets, ke::kMagnetPresets + ke::kMagnetCount, element) - ke::kMagnetPresets);
}

QString MagnetController::choiceText(int preset) const {
    return QString(pinned_ < 0 ? "★ " : "") + uiText("Kinetica", ke::presetName(ke::kMagnetPresets[preset])) + (axis_ < 0 ? QString() : QString(", ") + uiText("Kinetica", kAxisNames[axis_])) +
           (flip_ ? uiText("Kinetica", ", ось перевёрнута") : "") + (mirror_ ? uiText("Kinetica", ", зеркально") : "");
}

// The API's preview of the preset: each new link's edges and its joint, which animate() then moves.
void MagnetController::showPreview(const Place& p, int preset) {
    ghost_.clear();
    glyphs_.clear();
    const QJsonObject preview = p.found ? ops_->runNow("assembly_preview", placeArgs(p, preset)) : QJsonObject();
    const QJsonArray blocks = preview.value("blocks").toArray(), joints = preview.value("joints").toArray();
    for (int k = 0; k < blocks.size() && k < joints.size(); ++k) {
        ke::SceneGraph one;
        one.baseDirectory = builder_->graph().baseDirectory;
        std::string error;
        if (!ke::putObject(one, blocks[k].toString().toStdString(), error) || one.entities.empty()) continue;
        const ke::JointVis v = jointVisFromJson(joints[k].toObject());
        JointGizmoLines lines; // the preview in one colour: every freedom's glyph together
        appendJointGizmo(lines, v, float(v.size));
        for (const auto* part : {&lines.axis[0], &lines.axis[1], &lines.axis[2], &lines.dim}) glyphs_.insert(glyphs_.end(), part->begin(), part->end());
        const ke::Entity& e = one.entities[0];
        ghost_.push_back({ghostEdges(one, e), v.anchorA, ke::normalize(v.axis), v.reference, int(v.type),
                          e.shape == ke::ShapeKind::Cylinder, v.limited ? float(v.upper) : 0.5f * ke::maxComp(e.size)});
    }
    if (ghost_.empty()) animation_->stop(), view_->setMagnetPreview({}, {});
    else if (!animation_->isActive()) animation_->start(), animate();
}

// The ghost moves as its joint lets it: a hinge swings +-30 degrees (a wheel spins), a slider goes out
// and back over its travel, a ball wobbles about two axes, a weld stays.
void MagnetController::animate() {
    phase_ += 0.033f;
    std::vector<float> lines;
    for (const GhostPart& g : ghost_) {
        ke::Quaternion q;
        Vector3 shift(0.0f);
        if (g.type == int(ke::JointType::Hinge)) q = ke::Quaternion::fromAxisAngle(g.axis, g.spins ? 3.0f * phase_ : 0.5f * ke::sin(3.5f * phase_));
        if (g.type == int(ke::JointType::Slider)) shift = g.axis * (g.travel * 0.5f * (1 - ke::cos(3.0f * phase_)));
        if (g.type == int(ke::JointType::Ball))
            q = ke::Quaternion::fromAxisAngle(g.reference, 0.35f * ke::sin(3.0f * phase_)) *
                ke::Quaternion::fromAxisAngle(ke::cross(g.axis, g.reference), 0.35f * ke::cos(2.3f * phase_));
        for (const Vector3& p : g.points) {
            const Vector3 m = g.anchor + q.rotate(p - g.anchor) + shift;
            lines.insert(lines.end(), {m.x, m.y, m.z, 0});
        }
    }
    view_->setMagnetPreview(std::move(lines), glyphs_);
}

void MagnetController::preview(QPointF pos) {
    last_ = pos;
    hasLast_ = true;
    if (joining_) return showJoint(placeAt(pos));
    const Place p = placeAt(pos);
    const QString connecting = connectFrom_.found ? uiText("Kinetica", "Соединить: Ctrl+щелчок по второй детали · ") : "";
    if (!p.found) {
        showPreview(p, 0);
        return emit statusMessage(connecting + uiText("Kinetica", "Магнит: наведите на грань формы"));
    }
    const int preset = suggestion(p);
    showPreview(p, preset);
    emit statusMessage(connecting + QString(uiText("Kinetica", "Магнит · %1 — щелчок ставит; зажать и вести — меню; Ctrl+щелчок ×2 — соединить; "
                                            "1–5, 0, A ось, Tab, M зеркало, Esc")).arg(choiceText(preset)));
}

void MagnetController::press(QPointF pos, Qt::KeyboardModifiers modifiers) {
    const Place p = placeAt(pos);
    keepJoining_ = modifiers & Qt::ShiftModifier;
    if (joining_) return joinPress(p);
    if (modifiers & Qt::ControlModifier) return connectPress(p);
    pressed_ = p;
    pressPos_ = pos;
    pieOpen_ = false;
    if (pressed_.found) pieTimer_->start();
}

// The joint between two shapes where they stand: at the first click's point, about its polygon's
// normal (A: across it), of the pinned link's kind (a hinge unless a slider, weld or ball is pinned).
void MagnetController::connectPress(const Place& p) {
    if (!p.found) return;
    if (!connectFrom_.found || connectFrom_.target == p.target) {
        connectFrom_ = p;
        return emit statusMessage(uiText("Kinetica", "Соединить: теперь Ctrl+щелчок по второй детали — сочленение встанет в первой точке"));
    }
    const ke::LinkPreset kind = ke::kMagnetPresets[std::max(pinned_, 0)];
    const char* type = kind == ke::LinkPreset::Linear ? "slider" : kind == ke::LinkPreset::Weld ? "weld" : kind == ke::LinkPreset::Ball ? "ball" : "hinge";
    Vector3 axis = connectFrom_.normal;
    if (axis_ == 0 || axis_ == 1) {
        const QJsonObject snap = ops_->runNow("assembly_snap", {{"target", int(connectFrom_.target)}, {"point", json(connectFrom_.point)},
                                                                {"normal", json(connectFrom_.normal)}});
        if (snap.value("ok").toBool()) axis = vec(snap.value("across").toArray()[axis_]);
    }
    if (flip_) axis = axis * -1.0f;
    const QJsonObject done = ops_->runNow("assembly_joint", {{"a", int(p.target)}, {"b", int(connectFrom_.target)}, {"type", type},
                                                             {"anchor", json(connectFrom_.point)}, {"axis", json(axis)}});
    connectFrom_ = Place();
    static const QMap<QString, QString> words{{"hinge", uiText("Kinetica", "шарнир")}, {"weld", uiText("Kinetica", "сварка")}, {"slider", uiText("Kinetica", "ползун")}, {"ball", uiText("Kinetica", "шаровое")},
                                              {"cylinder", uiText("Kinetica", "цилиндрическое")}, {"universal", uiText("Kinetica", "карданное")}, {"planar", uiText("Kinetica", "плоское")}};
    const QString note = done.value("note").toString();
    emit statusMessage(done.value("ok").toBool() ? uiText("Kinetica", "Соединено: ") + words.value(type, type) + (note.isEmpty() ? QString() : ". " + note)
                                                 : uiText("Kinetica", "Соединить: ") + done.value("error").toString());
}

namespace {

const char* jointWord(ke::JointType t) {
    return t == ke::JointType::Slider ? "slider" : t == ke::JointType::Ball ? "ball" : t == ke::JointType::Fixed ? "weld" : "hinge";
}

QString jointText(ke::JointType t) {
    return t == ke::JointType::Slider ? uiText("Kinetica", "ползун") : t == ke::JointType::Ball ? uiText("Kinetica", "шар")
           : t == ke::JointType::Fixed ? uiText("Kinetica", "сварка") : uiText("Kinetica", "шарнир");
}

// An arrow from `a` to `b`: the shaft and a head of two strokes `head` long, in the plane facing the eye.
void appendArrow(std::vector<float>& lines, const Vector3& a, const Vector3& b, const Vector3& eye, float head) {
    const Vector3 d = ke::normalize(b - a), side = ke::normalize(ke::cross(d, eye - b));
    lines.insert(lines.end(), {a.x, a.y, a.z, 0.0f, b.x, b.y, b.z, 0.0f});
    for (float k : {-1.0f, 1.0f}) {
        const Vector3 t = b - d * head + side * (0.5f * k * head);
        lines.insert(lines.end(), {b.x, b.y, b.z, 0.0f, t.x, t.y, t.z, 0.0f});
    }
}

Vector3 across(const Vector3& axis) { // any unit direction square to the axis
    const Vector3 a = ke::normalize(axis);
    return ke::normalize(ke::cross(a, std::fabs(a.y) < 0.9f ? Vector3(0, 1, 0) : Vector3(1, 0, 0)));
}

} // namespace

MagnetController::JoinSnap MagnetController::joinSnap(const Place& p) const {
    JoinSnap s;
    s.at = p;
    if (!p.found) return s;
    s.axes = {p.normal};
    const ke::Entity* e = ke::findEntity(builder_->graph(), p.target);
    if (!e) return s;
    const ke::Quaternion q = ke::entityRotation(*e);
    const Vector3 local = q.conjugate().rotate(p.normal), unit[3] = {Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1)};
    const float half[3] = {0.5f * float(e->size.x), 0.5f * float(e->size.y), 0.5f * float(e->size.z)};
    const auto world = [&](const Vector3& v) { return e->position + q.rotate(v); };
    // An edge is caught along its whole length (the mouse near any of it), its joint put at its middle; a point, near itself.
    struct Candidate { Vector3 point, axis; ke::JointType type; int kind; Vector3 from{0.0f}, to{0.0f}; };
    std::vector<Candidate> candidates;
    if (e->shape == ke::ShapeKind::Box) {
        const float n[3] = {float(local.x), float(local.y), float(local.z)};
        const int k = std::fabs(n[0]) >= std::fabs(n[1]) && std::fabs(n[0]) >= std::fabs(n[2]) ? 0 : std::fabs(n[1]) >= std::fabs(n[2]) ? 1 : 2;
        const int i = (k + 1) % 3, j = (k + 2) % 3;
        const Vector3 centre = unit[k] * (n[k] < 0 ? -half[k] : half[k]);
        const Vector3 u = q.rotate(unit[i]), v = q.rotate(unit[j]);
        candidates.push_back({world(centre), p.normal, ke::JointType::Hinge, 0});
        for (float side : {-1.0f, 1.0f}) {
            const Vector3 alongJ = centre + unit[i] * (side * half[i]), alongI = centre + unit[j] * (side * half[j]);
            candidates.push_back({world(alongJ), v, ke::JointType::Hinge, 1, world(alongJ - unit[j] * half[j]), world(alongJ + unit[j] * half[j])});
            candidates.push_back({world(alongI), u, ke::JointType::Hinge, 1, world(alongI - unit[i] * half[i]), world(alongI + unit[i] * half[i])});
            for (float other : {-1.0f, 1.0f})
                candidates.push_back({world(centre + unit[i] * (side * half[i]) + unit[j] * (other * half[j])), p.normal, ke::JointType::Ball, 2});
        }
        s.axes = {p.normal, p.normal, u, v};
    } else if (e->shape == ke::ShapeKind::Cylinder) { // its axis is its own y: the side snaps onto the axis, a cap to its centre
        const Vector3 axis = q.rotate(unit[1]);
        const bool cap = std::fabs(float(local.y)) > 0.7f;
        const float along = cap ? (local.y < 0 ? -half[1] : half[1]) : float(ke::dot(q.conjugate().rotate(p.point - e->position), unit[1]));
        s.at.point = world(unit[1] * along), s.axes = {axis, across(axis), ke::cross(axis, across(axis))}, s.kind = 4;
        s.marks = {world(unit[1] * -half[1]), world(Vector3(0.0f)), world(unit[1] * half[1])};
        return s;
    } else if (e->shape == ke::ShapeKind::Sphere) {
        s.at.point = e->position, s.type = ke::JointType::Ball, s.kind = 5, s.marks = {e->position};
        return s;
    } else {
        const QJsonObject snap = ops_->runNow("assembly_snap", {{"target", int(p.target)}, {"point", json(p.point)}, {"normal", json(p.normal)}});
        if (snap.value("ok").toBool()) candidates.push_back({vec(snap.value("anchor").toArray()), p.normal, ke::JointType::Hinge, 0});
    }
    const GizmoView view = view_->gizmoView();
    const ke::Vector2 mouse = view.project(p.point);
    const Candidate* best = nullptr;
    Vector3 bestPoint(0.0f);
    float nearest = 24.0f; // px
    for (const Candidate& c : candidates) {
        s.marks.push_back(c.point);
        float d = float(ke::length(view.project(c.point) - mouse));
        if (c.kind == 1) { // the distance to the edge's segment on screen
            const ke::Vector2 a = view.project(c.from), b = view.project(c.to), ab = b - a;
            const float t = std::clamp(float(ke::dot(mouse - a, ab)) / std::max(float(ke::dot(ab, ab)), 1e-6f), 0.0f, 1.0f);
            d = float(ke::length(mouse - (a + ab * t))) + 4.0f; // a corner or the centre nearby wins
        }
        if (d < nearest) nearest = d, best = &c, bestPoint = c.point;
    }
    // On an edge the joint goes where the mouse points along it (the end of a lever, if that is where it points) - the edge's
    // middle only within 12 px of its cross.
    if (best && best->kind == 1 && float(ke::length(view.project(best->point) - mouse)) > 12.0f) {
        const Ray ray = view.ray(mouse);
        const Vector3 along = best->to - best->from, w0 = best->from - ray.origin;
        const float a = float(ke::dot(along, along)), b = float(ke::dot(along, ray.dir)), d = float(ke::dot(along, w0)), e = float(ke::dot(ray.dir, w0));
        const float denom = a - b * b;
        if (denom > 1e-9f) bestPoint = best->from + along * std::clamp((b * e - d) / denom, 0.0f, 1.0f);
    }
    if (best && !(QGuiApplication::keyboardModifiers() & Qt::ControlModifier)) {
        s.at.point = bestPoint, s.type = best->type, s.kind = best->kind;
        s.axes.front() = best->axis;
    }
    return s;
}

Vector3 MagnetController::joinAxis(const JoinSnap& s) const {
    std::vector<Vector3> axes = s.axes.empty() ? std::vector<Vector3>{s.at.normal} : s.axes;
    if (joinType_ == "shock") { // a shock carries weight: first the snap's direction nearest "up", as a car's (AssemblyPresets.cpp)
        const Vector3 up = ke::normalize(builder_->graph().world.gravity) * -1.0f;
        std::stable_sort(axes.begin(), axes.end(), [&up](const Vector3& x, const Vector3& y) {
            return std::fabs(float(ke::dot(x, up))) > std::fabs(float(ke::dot(y, up)));
        });
        for (Vector3& x : axes) x = float(ke::dot(x, up)) < 0 ? x * -1.0f : x;
    }
    const Vector3 a = axes[size_t(joinAxis_) % axes.size()];
    return ke::normalize(a) * (flip_ ? -1.0f : 1.0f);
}

// The kind drawn and forecast: a cylinder's as a hinge (it also slides), a universal's as a ball (it turns two ways), a planar's
// as a slider (it slides in the plane square to the axis).
ke::JointType MagnetController::joinKind(const JoinSnap& s) const {
    return joinType_ == "hinge" || joinType_ == "cylinder" ? ke::JointType::Hinge : joinType_ == "slider" || joinType_ == "planar" || joinType_ == "shock" ? ke::JointType::Slider
           : joinType_ == "ball" || joinType_ == "universal" ? ke::JointType::Ball : joinType_ == "weld" ? ke::JointType::Fixed : s.type;
}

// The word assembly_joint takes: the menu's, or the place's guess.
QString MagnetController::joinWord(const JoinSnap& s) const { return joinType_ == "auto" ? QString(jointWord(s.type)) : joinType_; }

QString MagnetController::joinText(const JoinSnap& s) const {
    return joinType_ == "cylinder" ? uiText("Kinetica", "цилиндрическое") : joinType_ == "universal" ? uiText("Kinetica", "карданное")
           : joinType_ == "planar" ? uiText("Kinetica", "плоское") : joinType_ == "shock" ? uiText("Kinetica", "амортизатор") : jointText(joinKind(s));
}

// What Play will do with the joint about to be made, said before the click (the ghost shows only what the joint lets): a base
// that is no body yet becomes a fixed one; a mover that is fixed stays; gravity turns a hinge's mover only if its centre is
// off the axis and the axis is not upright, slides a slider's only along a not-level axis.
QString MagnetController::playForecast(uint32_t second, const Vector3* centre) const {
    const uint32_t mover = moverOf(joinFrom_.at.target, second), base = mover == second ? joinFrom_.at.target : second;
    const ke::Entity* m = ke::findEntity(builder_->graph(), mover);
    const ke::Entity* b = ke::findEntity(builder_->graph(), base);
    if (!m || !b) return QString();
    QStringList said;
    if (!b->rigid.enabled) said << QString(uiText("Kinetica", "«%1» — не тело: станет неподвижной опорой")).arg(QString::fromStdString(b->name));
    const Vector3 g = builder_->graph().world.gravity, axis = joinAxis(joinFrom_), r = (centre ? *centre : m->position) - joinFrom_.at.point;
    const ke::JointType type = joinKind(joinFrom_);
    const float weight = float(ke::length(g)) + 1e-6f;
    const QString name = QString::fromStdString(m->name);
    if (m->rigid.enabled && m->rigid.fixed) said << QString(uiText("Kinetica", "«%1» закреплено — двигаться не будет")).arg(name);
    else if (type == ke::JointType::Fixed) said << QString(uiText("Kinetica", "в Пуске: «%1» держится намертво")).arg(name);
    else if (type == ke::JointType::Hinge) {
        const float turn = std::fabs(float(ke::dot(axis, ke::cross(r, g)))) / weight; // the lever gravity has about the axis, m
        said << (turn > 0.01f * float(ke::maxComp(m->size)) ? QString(uiText("Kinetica", "в Пуске: «%1» качнётся вниз")).arg(name)
                 : std::fabs(float(ke::dot(axis, g))) > 0.9f * weight ? QString(uiText("Kinetica", "в Пуске стоит: ось вертикальна — тяжесть не крутит; A — другая ось, или толчок"))
                                                                       : QString(uiText("Kinetica", "в Пуске стоит: ось через центр масс — щёлкните ближе к краю детали или A — другая ось")));
    } else if (joinType_ == "shock")
        said << QString(uiText("Kinetica", "в Пуске: «%1» повиснет на пружине, просев на 1,6 см, и будет пружинить")).arg(name);
    else if (joinType_ == "planar")
        said << (std::fabs(float(ke::dot(axis, g))) < 0.95f * weight ? QString(uiText("Kinetica", "в Пуске: «%1» поедет вниз по плоскости")).arg(name)
                                                                      : QString(uiText("Kinetica", "в Пуске стоит: плоскость горизонтальна — нужен толчок")));
    else if (type == ke::JointType::Slider)
        said << (std::fabs(float(ke::dot(axis, g))) > 0.05f * weight ? QString(uiText("Kinetica", "в Пуске: «%1» поедет вниз по оси")).arg(name)
                                                                      : QString(uiText("Kinetica", "в Пуске стоит: ход горизонтален — A — другая ось, или толчок")));
    else said << QString(uiText("Kinetica", "в Пуске: «%1» повиснет, болтаясь")).arg(name);
    if (m->rigid.dynamic() && !centre && m->position.y - 0.5f * m->size.y < 0.05f) // its bottom on the floor: the floor may hold it
        said << QString(uiText("Kinetica", "«%1» стоит на полу — пол может удержать")).arg(name);
    return said.join("\n");
}

namespace {

// Whether two shapes' boxes (turned with them) touch or overlap, within a centimetre: then they already stand together.
bool touching(const ke::Entity& a, const ke::Entity& b) {
    const auto bounds = [](const ke::Entity& e, Vector3& lo, Vector3& hi) {
        const ke::Quaternion q = ke::entityRotation(e);
        lo = Vector3(1e9f), hi = Vector3(-1e9f);
        for (int k = 0; k < 8; ++k) {
            const Vector3 c = e.position + q.rotate(Vector3(k & 1 ? 0.5f : -0.5f, k & 2 ? 0.5f : -0.5f, k & 4 ? 0.5f : -0.5f) * e.size);
            lo = ke::vmin(lo, c), hi = ke::vmax(hi, c);
        }
    };
    Vector3 la, ha, lb, hb;
    bounds(a, la, ha), bounds(b, lb, hb);
    const Vector3 gap = ke::vmax(lb - ha, la - hb);
    return ke::maxComp(gap) < 0.01f;
}

} // namespace

MagnetController::Align MagnetController::alignTo(const JoinSnap& second) const {
    Align a;
    const JoinSnap& first = joinFrom_;
    if (!first.at.found || !second.at.found || second.at.target == first.at.target || second.kind == 3) return a; // a plain point: as built
    if (float(ke::length(second.at.point - first.at.point)) < 0.005f) return a; // they meet already
    const ke::Entity* one = ke::findEntity(builder_->graph(), first.at.target);
    const ke::Entity* two = ke::findEntity(builder_->graph(), second.at.target);
    if (!one || !two || touching(*one, *two)) return a; // parts that touch stand where they belong: joined as they are
    a.mover = moverOf(first.at.target, second.at.target);
    a.base = a.mover == first.at.target ? second.at.target : first.at.target;
    const JoinSnap& own = a.mover == first.at.target ? first : second;
    const JoinSnap& other = a.mover == first.at.target ? second : first;
    const Vector3 from = ke::normalize(own.at.normal), want = ke::normalize(other.at.normal) * -1.0f; // face to face
    a.turn = ke::Quaternion::fromTwoVectors(from, want);
    a.from = own.at.point, a.to = other.at.point;
    const Vector3 axis = joinAxis(first);
    a.axis = a.mover == first.at.target ? a.turn.rotate(axis) : axis;
    a.on = true;
    return a;
}

// The smaller of the two moves (a part on a base), a shape that is no body yet stays; M swaps.
uint32_t MagnetController::moverOf(uint32_t first, uint32_t second) const {
    const auto volume = [this](uint32_t id) {
        const ke::Entity* e = ke::findEntity(builder_->graph(), id);
        return e ? float(e->size.x * e->size.y * e->size.z) * (e->rigid.enabled ? 1.0f : 1e9f) : 1e9f;
    };
    const bool firstMoves = volume(first) < volume(second);
    return firstMoves != swapMover_ ? first : second;
}

void MagnetController::pickJoint(uint32_t joint) {
    jointAction_->setChecked(true);
    if (!joining_) return;
    repick_ = joint;
    emit statusMessage(uiText("Kinetica", "Сустав: щёлкните грань, ребро или угол — его точка и ось встанут туда · Esc — отмена"));
}

void MagnetController::joinPress(const Place& p) {
    if (!p.found) return;
    if (repick_) { // the card's «Указать в виде»: one click puts that joint there
        const JoinSnap s = joinSnap(p);
        const uint32_t joint = repick_;
        const bool done = builder_->placeJointAt(joint, s.at.point, joinAxis(s));
        setJoining(false);
        builder_->select(joint);
        return emit statusMessage(done ? uiText("Kinetica", "Сустав перенесён") : uiText("Kinetica", "Сустав: перенести не вышло"));
    }
    if (!joinFrom_.at.found || joinFrom_.at.target == p.target) { // the first click, or the first again elsewhere on the same shape
        joinFrom_ = joinSnap(p);
        return showJoint(p);
    }
    const uint32_t mover = moverOf(joinFrom_.at.target, p.target), base = mover == p.target ? joinFrom_.at.target : p.target;
    const Align align = alignTo(joinSnap(p));
    QJsonObject done;
    if (align.on) { // turned face to face about its own point, moved onto the other's, joined there: one undo step
        QJsonArray steps;
        if (align.turn.angle() > 1e-4f)
            steps.append(QJsonObject{{"op", "scene_turn"}, {"args", QJsonObject{{"ids", QJsonArray{int(align.mover)}}, {"axis", json(align.turn.axis())},
                                                                                {"degrees", double(align.turn.angle()) * 57.29577951}, {"pivot", json(align.from)}}}});
        steps.append(QJsonObject{{"op", "scene_move"}, {"args", QJsonObject{{"ids", QJsonArray{int(align.mover)}}, {"by", json(align.to - align.from)}}}});
        steps.append(QJsonObject{{"op", "assembly_joint"}, {"args", QJsonObject{{"a", int(align.mover)}, {"b", int(align.base)}, {"type", joinWord(joinFrom_)},
                                                                               {"anchor", json(align.to)}, {"axis", json(align.axis)}}}});
        done = ops_->runNow("scene_batch", {{"ops", steps}});
    } else {
        done = ops_->runNow("assembly_joint", {{"a", int(mover)}, {"b", int(base)}, {"type", joinWord(joinFrom_)},
                                               {"anchor", json(joinFrom_.at.point)}, {"axis", json(joinAxis(joinFrom_))}});
    }
    joinFrom_ = JoinSnap();
    const QString note = done.value("note").toString();
    emit statusMessage(done.value("ok").toBool() ? uiText("Kinetica", "Соединено: ") + joinText(joinFrom_) + (note.isEmpty() ? QString() : ". " + note)
                                                 : uiText("Kinetica", "Сустав: ") + done.value("error").toString());
    if (done.value("ok").toBool() && !keepJoining_) setJoining(false); // one joint, then the selection; Shift: one more
    else showJoint(p);
}

// What a click would make: the snap points of the face as dots, the joint's own glyph at the chosen one at a size steady on
// screen, its axis drawn long; after the first click, the part that will move swinging (sliding, wobbling) as the joint lets
// it - the motion seen before the second click.
void MagnetController::showJoint(const Place& p) {
    const JoinSnap hover = joinSnap(p);
    const JoinSnap& s = joinFrom_.at.found ? joinFrom_ : hover;
    glyphs_.clear();
    ghost_.clear();
    if (s.at.found) {
        const GizmoView view = view_->gizmoView();
        Vector3 right, up;
        view.screenAxes(right, up);
        for (const Vector3& m : (joinFrom_.at.found ? joinFrom_ : hover).marks) { // a dot: a small cross, 4 px
            const float r = 4.0f * view.worldPerPixel(m);
            for (const Vector3& d : {right * r, up * r})
                glyphs_.insert(glyphs_.end(), {m.x - d.x, m.y - d.y, m.z - d.z, 0.0f, m.x + d.x, m.y + d.y, m.z + d.z, 0.0f});
        }
        const float size = 26.0f * view.worldPerPixel(s.at.point);
        ke::JointVis v;
        v.type = joinKind(s);
        v.anchorA = v.anchorB = s.at.point;
        v.axis = joinAxis(s);
        v.reference = across(v.axis);
        v.size = size;
        appendJointGlyph(glyphs_, v, size);
        // The axis as an arrow (the way the angle counts; a slider's both ways), its tail through the point.
        const Vector3 eye = view.eye(), o = s.at.point;
        if (v.type == ke::JointType::Hinge) appendArrow(glyphs_, o - v.axis * (1.5f * size), o + v.axis * (3.0f * size), eye, 0.6f * size);
        if (v.type == ke::JointType::Slider && joinType_ != "planar")
            for (float k : {-1.0f, 1.0f}) appendArrow(glyphs_, o, o + v.axis * (k * 3.0f * size), eye, 0.6f * size);
        if (joinType_ == "shock") { // a coil spring along the axis, beside the travel's arrows
            const Vector3 side = v.reference * (0.45f * size);
            Vector3 prev = o - v.axis * (2.0f * size);
            for (int z = 1; z <= 14; ++z) {
                const Vector3 q = o + v.axis * (-2.0f * size + 4.0f * size * float(z) / 14.0f) + (z % 2 ? side : side * -1.0f);
                glyphs_.insert(glyphs_.end(), {prev.x, prev.y, prev.z, 0.0f, q.x, q.y, q.z, 0.0f});
                prev = q;
            }
        }
        if (joinType_ == "cylinder") // it slides along its axis too
            for (float k : {-1.0f, 1.0f}) appendArrow(glyphs_, o, o + v.axis * (k * 2.2f * size), eye, 0.5f * size);
        if (joinType_ == "planar") // it slides every way in the plane square to the axis
            for (const Vector3& d : {v.reference, ke::cross(v.axis, v.reference)})
                for (float k : {-1.0f, 1.0f}) appendArrow(glyphs_, o, o + d * (k * 2.5f * size), eye, 0.5f * size);
        if (joinType_ == "universal") { // its two crossed axes, each a hinge's ring
            ke::JointVis second = v;
            second.type = ke::JointType::Hinge, second.axis = v.reference, second.reference = v.axis;
            appendJointGlyph(glyphs_, second, 0.8f * size);
            appendArrow(glyphs_, o - v.axis * (1.5f * size), o + v.axis * (3.0f * size), eye, 0.6f * size);
            appendArrow(glyphs_, o - v.reference * (1.2f * size), o + v.reference * (2.4f * size), eye, 0.6f * size);
        }
        if (joinFrom_.at.found) { // a rubber band from the axis to the mouse, wherever it is: to the part under it, else in the air
            Vector3 to = p.point;
            if (!p.found || p.target == joinFrom_.at.target) {
                const Ray r = view_->camera().screenRay(last_, view_->size());
                const Vector3 n = view.forward();
                const float d = float(ke::dot(r.dir, n));
                to = std::fabs(d) > 1e-4f ? r.at(float(ke::dot(o - r.origin, n)) / d) : o;
            }
            appendArrow(glyphs_, o, to, eye, 0.6f * size);
        }
        if (joinFrom_.at.found && p.found && p.target != joinFrom_.at.target) {
            for (const Vector3& m : hover.marks) { // the other part's snap points too: one of them, and the part comes to it
                const float r = 4.0f * view.worldPerPixel(m);
                for (const Vector3& d : {right * r, up * r})
                    glyphs_.insert(glyphs_.end(), {m.x - d.x, m.y - d.y, m.z - d.z, 0.0f, m.x + d.x, m.y + d.y, m.z + d.z, 0.0f});
            }
            const Align align = alignTo(hover);
            const uint32_t mover = align.on ? align.mover : moverOf(joinFrom_.at.target, p.target);
            if (const ke::Entity* e = ke::findEntity(builder_->graph(), mover)) {
                std::vector<Vector3> edges = ghostEdges(builder_->graph(), *e);
                if (align.on)
                    for (Vector3& x : edges) x = align.to + align.turn.rotate(x - align.from); // where it will stand
                const Vector3 at = align.on ? align.to : s.at.point, axis = align.on ? ke::normalize(align.axis) : v.axis;
                ghost_.push_back({std::move(edges), at, axis, across(axis), int(v.type), false, 0.5f * float(ke::maxComp(e->size))});
            }
        }
    }
    if (ghost_.empty()) animation_->stop(), view_->setMagnetPreview({}, glyphs_);
    else if (!animation_->isActive()) animation_->start(), animate();
    static const char* const kinds[] = {"центр грани", "ребро", "угол", "точка", "ось цилиндра", "центр шара"};
    const QString where = s.at.found ? QString("%1 · %2").arg(uiText("Kinetica", kinds[s.kind]), joinText(s)) : QString();
    const bool other = p.found && joinFrom_.at.found && p.target != joinFrom_.at.target;
    QString forecastLabel;
    if (other) {
        const Align align = alignTo(hover);
        const ke::Entity* m = align.on ? ke::findEntity(builder_->graph(), align.mover) : nullptr;
        const Vector3 centre = m ? align.to + align.turn.rotate(m->position - align.from) : Vector3(0.0f);
        forecastLabel = m ? QString(uiText("Kinetica", "2/2 · щёлкните — «%1» подъедет сюда, грань к грани, и встанет на сустав"))
                                    .arg(QString::fromStdString(m->name)) + "\n" + playForecast(p.target, &centre)
                          : QString(uiText("Kinetica", "2/2 · щёлкните — соединить, где стоят (точка второй детали — подвести её)")) + "\n" + playForecast(p.target);
    }
    view_->setMagnetLabel(repick_ ? (s.at.found ? QString(uiText("Kinetica", "%1 · щёлкните — сустав встанет сюда\nA — другая ось · Tab — перевернуть · Ctrl — без привязки")).arg(where)
                                                : QString(uiText("Kinetica", "наведите на деталь — куда перенести сустав")))
                          : !joinFrom_.at.found ? (s.at.found ? QString(uiText("Kinetica", "1/2 · %1 · щёлкните — здесь ось\nA — другая ось · Tab — перевернуть · Ctrl — без привязки")).arg(where)
                                                            : QString(uiText("Kinetica", "1/2 · наведите на деталь")))
                          : other ? forecastLabel : QString(uiText("Kinetica", "2/2 · ведите на вторую деталь")));
    emit statusMessage(!joinFrom_.at.found
                           ? (s.at.found ? QString(uiText("Kinetica", "Сустав, шаг 1/2 · %1: щёлкните — здесь будет ось · 0–4 вид, A ось, Tab, Ctrl без привязки, Esc выход")).arg(where)
                                         : QString(uiText("Kinetica", "Сустав, шаг 1/2: наведите на деталь — центр грани, ребро или угол подскажут вид")))
                       : other ? QString(uiText("Kinetica", "Сустав, шаг 2/2 · %1: щёлкните — детали соединятся; призрак показывает, как пойдёт · M — двигать другую")).arg(where)
                               : QString(uiText("Kinetica", "Сустав, шаг 2/2: ведите стрелку на ВТОРУЮ деталь и щёлкните · Esc — заново")));
}

void MagnetController::openPie() {
    if (!pressed_.found || pieOpen_) return;
    QStringList labels;
    QList<QPixmap> icons;
    for (int k = 0; k < ke::kMagnetCount; ++k) labels << uiText("Kinetica", ke::presetName(ke::kMagnetPresets[k])), icons << linkPixmap(LinkIcon(k), 48);
    pieOpen_ = true;
    pie_->open(pressPos_, labels, icons, suggestion(pressed_));
}

// Past 10 px the pie opens at once (a flick need not wait), and the lit sector's link is the preview.
void MagnetController::drag(QPointF pos) {
    if (!pressed_.found) return;
    if (!pieOpen_ && QLineF(pos, pressPos_).length() > 10) openPie();
    if (!pieOpen_) return;
    const int before = pie_->chosen();
    pie_->track(pos);
    if (pie_->chosen() != before) showPreview(pressed_, pie_->chosen() >= 0 ? pie_->chosen() : suggestion(pressed_));
}

void MagnetController::release(QPointF pos) {
    pieTimer_->stop();
    const Place p = pressed_;
    pressed_ = Place();
    const int sector = pieOpen_ ? PieMenu::sectorOf(pressPos_, pos, ke::kMagnetCount) : -1;
    pie_->hide();
    pieOpen_ = false;
    if (joining_) return preview(pos);
    if (!action_->isChecked()) return; // «Сустав» just made its joint and ended: no magnet's words after it
    if (p.found) put(p, sector >= 0 ? sector : suggestion(p));
    preview(pos); // the new link is under the mouse now: its own faces
}

void MagnetController::put(const Place& p, int preset) {
    const QJsonObject done = ops_->runNow("assembly_preset", placeArgs(p, preset));
    if (!done.value("ok").toBool()) return emit statusMessage(uiText("Kinetica", "Магнит: ") + done.value("error").toString());
    const QJsonArray links = done.value("links").toArray();
    if (!links.isEmpty()) builder_->select(uint32_t(links[0].toObject().value("entity").toInt()));
}

void MagnetController::pin(int preset) {
    pinned_ = preset;
    if (presetActs_[preset + 1]) presetActs_[preset + 1]->setChecked(true);
    if (hasLast_) preview(last_);
}

void MagnetController::key(int key) {
    if (key == Qt::Key_Escape && !(joining_ && joinFrom_.at.found)) return action_->setChecked(false), jointAction_->setChecked(false);
    if (joining_) {
        if (key == Qt::Key_Escape) { // step 2: back to step 1, not out
            joinFrom_ = JoinSnap();
            if (hasLast_) preview(last_);
            return;
        }
        if (key >= Qt::Key_0 && key <= Qt::Key_8)
            if (QAction* a = jointAction_->menu()->actions().value(key - Qt::Key_0)) a->trigger();
        if (key == Qt::Key_A) ++joinAxis_;
        if (key == Qt::Key_Tab) flip_ = !flip_;
        if (key == Qt::Key_M) swapMover_ = !swapMover_;
        if (hasLast_) preview(last_);
        return;
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_0 + ke::kMagnetCount) return pin(key - Qt::Key_0 - 1);
    if (key == Qt::Key_A) axis_ = axis_ == -1 ? 2 : axis_ == 2 ? 0 : axis_ == 0 ? 1 : -1; // normal, across, across, own
    if (key == Qt::Key_Tab) flip_ = !flip_;
    if (key == Qt::Key_M) mirror_ = !mirror_;
    if (hasLast_) preview(last_);
}

