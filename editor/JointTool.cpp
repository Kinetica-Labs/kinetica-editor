// «Сустав» (JointTool.h): the guess at the place, the two clicks, the alignment, Play's forecast, the drawing.
#include "JointTool.h"

#include "EditorOps.h"
#include "Icons.h"
#include "JointGlyphs.h"
#include "SceneBuilder.h"
#include "UiLanguage.h"
#include "Viewport.h"

#include "math/ElementaryFunctions.h"
#include "scene/Assembly.h"

#include <QAction>
#include <QActionGroup>
#include <QGuiApplication>
#include <QJsonArray>
#include <QMenu>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <cmath>

using ke::Vector3;

namespace {

const char* jointWord(ke::JointType t) {
    return t == ke::JointType::Slider ? "slider" : t == ke::JointType::Ball ? "ball" : t == ke::JointType::Fixed ? "weld" : "hinge";
}

QString jointText(ke::JointType t) {
    return t == ke::JointType::Slider ? uiText("Kinetica", "ползун") : t == ke::JointType::Ball ? uiText("Kinetica", "шар")
           : t == ke::JointType::Fixed ? uiText("Kinetica", "сварка") : uiText("Kinetica", "шарнир");
}

} // namespace

JointTool::JointTool(EditorOps* ops, SceneBuilder* builder, Viewport* view, QObject* parent)
    : QObject(parent), ops_(ops), builder_(builder), view_(view), animation_(new QTimer(this)) {
    action_ = new QAction(svgIcon("joints/joint-tool", 48), uiText("Kinetica", "Сустав"), this);
    action_->setObjectName("jointTool");
    action_->setCheckable(true);
    action_->setToolTip(uiText("Kinetica", "<b>Сустав</b>: соединить две стоящие детали.<br>"
                        "Наведите: центр грани — вал, ребро — петля, угол — шар; призрак показывает движение.<br>"
                        "Щелчок — место оси, щелчок по второй детали — готово (движется меньшая).<br>"
                        "0–4 вид, A ось, Tab перевернуть, M кто движется, Ctrl без привязки, Shift — ещё один, Esc"));
    auto* kinds = new QMenu(view_); // the view's child: the API's ui_actions and ui_trigger find its items
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
            if (!action_->isChecked()) action_->setChecked(true);
            else if (hasLast_) preview(last_);
        });
    }
    action_->setMenu(kinds);
    animation_->setInterval(33);
    connect(animation_, &QTimer::timeout, this, [this] { phase_ += 0.033f, view_->setMagnetPreview(ghostLines(ghost_, phase_), glyphs_); });
    connect(action_, &QAction::toggled, this, &JointTool::setActive);
    connect(view_, &Viewport::magnetMoved, this, &JointTool::preview);
    connect(view_, &Viewport::magnetPressed, this, &JointTool::press);
    connect(view_, &Viewport::magnetReleased, this, &JointTool::release);
    connect(view_, &Viewport::magnetKey, this, &JointTool::key);
    connect(builder_, &SceneBuilder::modeChanged, this, [this] {
        if (!builder_->editing()) action_->setChecked(false);
    });
}

void JointTool::setActive(bool on) {
    if (on && !builder_->editing()) {
        action_->setChecked(false);
        return emit statusMessage(uiText("Kinetica", "Сустав ставится в режиме правки — остановите сцену"));
    }
    if (on && !joining_) emit started(); // the magnet off first: then the view's mode is this tool's
    if (!on && !joining_) return;
    joining_ = on;
    joinFrom_ = JoinSnap();
    repick_ = 0;
    joinAxis_ = 0, swapMover_ = false;
    if (action_->isChecked() != on) action_->setChecked(on);
    view_->setMagnetMode(on);
    ghost_.clear();
    animation_->stop();
    view_->setMagnetPreview({}, {});
    if (on) view_->setFocus();
    emit statusMessage(on ? uiText("Kinetica", "Сустав: наведите на грань — центр, ребро или угол подскажут вид") : QString());
}

void JointTool::preview(QPointF pos) {
    if (!joining_) return;
    last_ = pos;
    hasLast_ = true;
    showJoint(placeUnder(builder_, view_, pos));
}

void JointTool::press(QPointF pos, Qt::KeyboardModifiers modifiers) {
    if (!joining_) return;
    keepJoining_ = modifiers & Qt::ShiftModifier;
    joinPress(placeUnder(builder_, view_, pos));
}

void JointTool::release(QPointF pos) {
    if (joining_) preview(pos);
}

void JointTool::key(int key) {
    if (!joining_) return;
    if (key == Qt::Key_Escape) { // step 2: back to step 1; step 1: out
        if (!joinFrom_.at.found) return action_->setChecked(false);
        joinFrom_ = JoinSnap();
        if (hasLast_) preview(last_);
        return;
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_8)
        if (QAction* a = action_->menu()->actions().value(key - Qt::Key_0)) a->trigger();
    if (key == Qt::Key_A) ++joinAxis_;
    if (key == Qt::Key_Tab) flip_ = !flip_;
    if (key == Qt::Key_M) swapMover_ = !swapMover_;
    if (hasLast_) preview(last_);
}

JointTool::JoinSnap JointTool::joinSnap(const ToolPlace& p) const {
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
        const QJsonObject snap = ops_->runNow("assembly_snap", {{"target", int(p.target)}, {"point", jsonOf(p.point)}, {"normal", jsonOf(p.normal)}});
        if (snap.value("ok").toBool()) candidates.push_back({vecOf(snap.value("anchor").toArray()), p.normal, ke::JointType::Hinge, 0});
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

Vector3 JointTool::joinAxis(const JoinSnap& s) const {
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
ke::JointType JointTool::joinKind(const JoinSnap& s) const {
    return joinType_ == "hinge" || joinType_ == "cylinder" ? ke::JointType::Hinge : joinType_ == "slider" || joinType_ == "planar" || joinType_ == "shock" ? ke::JointType::Slider
           : joinType_ == "ball" || joinType_ == "universal" ? ke::JointType::Ball : joinType_ == "weld" ? ke::JointType::Fixed : s.type;
}

// The word assembly_joint takes: the menu's, or the place's guess.
QString JointTool::joinWord(const JoinSnap& s) const { return joinType_ == "auto" ? QString(jointWord(s.type)) : joinType_; }

QString JointTool::joinText(const JoinSnap& s) const {
    return joinType_ == "cylinder" ? uiText("Kinetica", "цилиндрическое") : joinType_ == "universal" ? uiText("Kinetica", "карданное")
           : joinType_ == "planar" ? uiText("Kinetica", "плоское") : joinType_ == "shock" ? uiText("Kinetica", "амортизатор") : jointText(joinKind(s));
}

// What Play will do with the joint about to be made, said before the click (the ghost shows only what the joint lets): a base
// that is no body yet becomes a fixed one; a mover that is fixed stays; gravity turns a hinge's mover only if its centre is
// off the axis and the axis is not upright, slides a slider's only along a not-level axis.
QString JointTool::playForecast(uint32_t second, const Vector3* centre) const {
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

JointTool::Align JointTool::alignTo(const JoinSnap& second) const {
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
uint32_t JointTool::moverOf(uint32_t first, uint32_t second) const {
    const auto volume = [this](uint32_t id) {
        const ke::Entity* e = ke::findEntity(builder_->graph(), id);
        return e ? float(e->size.x * e->size.y * e->size.z) * (e->rigid.enabled ? 1.0f : 1e9f) : 1e9f;
    };
    const bool firstMoves = volume(first) < volume(second);
    return firstMoves != swapMover_ ? first : second;
}

void JointTool::pickJoint(uint32_t joint) {
    action_->setChecked(true);
    if (!joining_) return;
    repick_ = joint;
    emit statusMessage(uiText("Kinetica", "Сустав: щёлкните грань, ребро или угол — его точка и ось встанут туда · Esc — отмена"));
}

void JointTool::joinPress(const ToolPlace& p) {
    if (!p.found) return;
    if (repick_) { // the card's «Указать в виде»: one click puts that joint there
        const JoinSnap s = joinSnap(p);
        const uint32_t joint = repick_;
        const bool done = builder_->placeJointAt(joint, s.at.point, joinAxis(s));
        setActive(false);
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
            steps.append(QJsonObject{{"op", "scene_turn"}, {"args", QJsonObject{{"ids", QJsonArray{int(align.mover)}}, {"axis", jsonOf(align.turn.axis())},
                                                                                {"degrees", double(align.turn.angle()) * 57.29577951}, {"pivot", jsonOf(align.from)}}}});
        steps.append(QJsonObject{{"op", "scene_move"}, {"args", QJsonObject{{"ids", QJsonArray{int(align.mover)}}, {"by", jsonOf(align.to - align.from)}}}});
        steps.append(QJsonObject{{"op", "assembly_joint"}, {"args", QJsonObject{{"a", int(align.mover)}, {"b", int(align.base)}, {"type", joinWord(joinFrom_)},
                                                                               {"anchor", jsonOf(align.to)}, {"axis", jsonOf(align.axis)}}}});
        done = ops_->runNow("scene_batch", {{"ops", steps}});
    } else {
        done = ops_->runNow("assembly_joint", {{"a", int(mover)}, {"b", int(base)}, {"type", joinWord(joinFrom_)},
                                               {"anchor", jsonOf(joinFrom_.at.point)}, {"axis", jsonOf(joinAxis(joinFrom_))}});
    }
    joinFrom_ = JoinSnap();
    const QString note = done.value("note").toString();
    emit statusMessage(done.value("ok").toBool() ? uiText("Kinetica", "Соединено: ") + joinText(joinFrom_) + (note.isEmpty() ? QString() : ". " + note)
                                                 : uiText("Kinetica", "Сустав: ") + done.value("error").toString());
    if (done.value("ok").toBool() && !keepJoining_) setActive(false); // one joint, then the selection; Shift: one more
    else showJoint(p);
}

// What a click would make: the snap points of the face as dots, the joint's own glyph at the chosen one at a size steady on
// screen, its axis drawn long; after the first click, the part that will move swinging (sliding, wobbling) as the joint lets
// it - the motion seen before the second click.
void JointTool::showJoint(const ToolPlace& p) {
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
    else if (!animation_->isActive()) animation_->start(), view_->setMagnetPreview(ghostLines(ghost_, phase_), glyphs_);
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
                          : QString(uiText("Kinetica", "2/2 · щёлкните — соединить, где стоят")) + "\n" + playForecast(p.target) + "\n" +
                                uiText("Kinetica", "M — двигать другую · крестик детали, стоящей поодаль, — подвести её");
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

