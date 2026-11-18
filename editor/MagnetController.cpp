// The magnet (MagnetController.h): the polygon under the mouse, the moving ghost of the ready link a
// click would put (the API's assembly_preview), the pie of the five elements, the click (assembly_preset) and
// the two clicks that join two shapes (assembly_joint).
#include "MagnetController.h"

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

const char* kAxisNames[] = {"ось поперёк грани", "ось поперёк, другая", "ось по нормали"};

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
    action_ = new QAction(magnetIcon(), "Магнит", this);
    action_->setObjectName("magnetTool");
    action_->setCheckable(true);
    action_->setToolTip("<b>Магнит</b>: готовое звено к грани, с сочленением.<br>"
                        "Щелчок — подсказанное (★); зажать и вести — круговое меню;<br>"
                        "Ctrl+щелчок по двум деталям — соединить.<br>"
                        "1–5 выбрать, 0 подсказка, A ось, Tab перевернуть, M зеркало, Esc выход");
    action_->setMenu(buildMenu());
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
        const QString text = k == 0 ? QString("0  ★ подсказка по месту") : QString("%1  %2").arg(k).arg(ke::presetName(e));
        presetActs_[k] = k == 0 ? menu->addAction(text) : menu->addAction(linkIcon(LinkIcon(k - 1), 24), text);
        presetActs_[k]->setObjectName(k == 0 ? QString("magnetSuggested") : QString("magnet_") + ke::presetWord(e));
        presetActs_[k]->setCheckable(true);
        group->addAction(presetActs_[k]);
        connect(presetActs_[k], &QAction::triggered, this, [this, k] { pin(k - 1); });
    }
    presetActs_[0]->setChecked(true);
    menu->addSeparator();
    const std::pair<const char*, int> toggles[] = {{"A  повернуть ось", Qt::Key_A}, {"Tab  перевернуть ось", Qt::Key_Tab}, {"M  зеркально", Qt::Key_M}};
    for (const auto& [text, k] : toggles) {
        QAction* a = menu->addAction(text);
        a->setObjectName(QString("magnetKey_") + QChar(k == Qt::Key_Tab ? 'T' : char(k)));
        connect(a, &QAction::triggered, this, [this, k = k] { key(k); });
    }
    return menu;
}

void MagnetController::setActive(bool on) {
    if (on && !builder_->editing()) {
        action_->setChecked(false);
        emit statusMessage("Магнит работает в режиме правки — остановите сцену");
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
    emit statusMessage(on ? "Магнит: наведите на грань формы. Щелчок — подсказанное звено, зажать и вести — круговое меню" : QString());
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
    return QString(pinned_ < 0 ? "★ " : "") + ke::presetName(ke::kMagnetPresets[preset]) + (axis_ < 0 ? QString() : QString(", ") + kAxisNames[axis_]) +
           (flip_ ? ", ось перевёрнута" : "") + (mirror_ ? ", зеркально" : "");
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
        appendJointGizmo(lines, v, float(v.size), false);
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
    const Place p = placeAt(pos);
    const QString connecting = connectFrom_.found ? "Соединить: Ctrl+щелчок по второй детали · " : "";
    if (!p.found) {
        showPreview(p, 0);
        return emit statusMessage(connecting + "Магнит: наведите на грань формы");
    }
    const int preset = suggestion(p);
    showPreview(p, preset);
    emit statusMessage(connecting + QString("Магнит · %1 — щелчок ставит; зажать и вести — меню; Ctrl+щелчок ×2 — соединить; "
                                            "1–5, 0, A ось, Tab, M зеркало, Esc").arg(choiceText(preset)));
}

void MagnetController::press(QPointF pos, Qt::KeyboardModifiers modifiers) {
    const Place p = placeAt(pos);
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
        return emit statusMessage("Соединить: теперь Ctrl+щелчок по второй детали — сочленение встанет в первой точке");
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
    emit statusMessage(done.value("ok").toBool() ? QString("Соединено: %1").arg(type) : "Соединить: " + done.value("error").toString());
}

void MagnetController::openPie() {
    if (!pressed_.found || pieOpen_) return;
    QStringList labels;
    QList<QPixmap> icons;
    for (int k = 0; k < ke::kMagnetCount; ++k) labels << ke::presetName(ke::kMagnetPresets[k]), icons << linkPixmap(LinkIcon(k), 48);
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
    if (p.found) put(p, sector >= 0 ? sector : suggestion(p));
    preview(pos); // the new link is under the mouse now: its own faces
}

void MagnetController::put(const Place& p, int preset) {
    const QJsonObject done = ops_->runNow("assembly_preset", placeArgs(p, preset));
    if (!done.value("ok").toBool()) return emit statusMessage("Магнит: " + done.value("error").toString());
    const QJsonArray links = done.value("links").toArray();
    if (!links.isEmpty()) builder_->select(uint32_t(links[0].toObject().value("entity").toInt()));
}

void MagnetController::pin(int preset) {
    pinned_ = preset;
    if (presetActs_[preset + 1]) presetActs_[preset + 1]->setChecked(true);
    if (hasLast_) preview(last_);
}

void MagnetController::key(int key) {
    if (key == Qt::Key_Escape) return action_->setChecked(false);
    if (key >= Qt::Key_0 && key <= Qt::Key_0 + ke::kMagnetCount) return pin(key - Qt::Key_0 - 1);
    if (key == Qt::Key_A) axis_ = axis_ == -1 ? 2 : axis_ == 2 ? 0 : axis_ == 0 ? 1 : -1; // normal, across, across, own
    if (key == Qt::Key_Tab) flip_ = !flip_;
    if (key == Qt::Key_M) mirror_ = !mirror_;
    if (hasLast_) preview(last_);
}

