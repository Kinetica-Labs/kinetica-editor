// The magnet (MagnetController.h): the polygon under the mouse, the moving ghost of the ready link a
// click would put (the API's assembly_preview), the pie of the five elements, the click (assembly_preset).
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


} // namespace

MagnetController::MagnetController(EditorOps* ops, SceneBuilder* builder, Viewport* view, QObject* parent)
    : QObject(parent), ops_(ops), builder_(builder), view_(view), pie_(new PieMenu(view)), pieTimer_(new QTimer(this)),
      animation_(new QTimer(this)) {
    action_ = new QAction(magnetIcon(), uiText("Kinetica", "Магнит"), this);
    action_->setObjectName("magnetTool");
    action_->setCheckable(true);
    action_->setToolTip(uiText("Kinetica", "<b>Магнит</b>: готовое звено к грани, с сочленением.<br>"
                        "Щелчок — подсказанное (★); зажать и вести — круговое меню;<br>"
                        "1–5 выбрать, 0 подсказка, A ось, Tab перевернуть, M зеркало, Esc выход"));
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
void MagnetController::setActive(bool on) {
    if (on && !builder_->editing()) {
        action_->setChecked(false);
        emit statusMessage(uiText("Kinetica", "Магнит работает в режиме правки — остановите сцену"));
        return;
    }
    if (on && !active_) emit started(); // «Сустав» off first: then the view's mode is the magnet's
    if (!on && !active_) return;
    active_ = on;
    if (action_->isChecked() != on) action_->setChecked(on);
    view_->setMagnetMode(on);
    pie_->hide();
    pieTimer_->stop();
    animation_->stop();
    ghost_.clear();
    pressed_ = ToolPlace();
    if (on) view_->setFocus(); // the keys go to the view at once, no click needed first
    emit statusMessage(on ? uiText("Kinetica", "Магнит: наведите на грань формы. Щелчок — подсказанное звено, зажать и вести — круговое меню") : QString());
}

QJsonObject MagnetController::placeArgs(const ToolPlace& p, int preset) const {
    QJsonObject a{{"target", int(p.target)}, {"preset", ke::presetWord(ke::kMagnetPresets[preset])}, {"point", jsonOf(p.point)},
                  {"normal", jsonOf(p.normal)}, {"flip", flip_}, {"mirror", mirror_}};
    if (axis_ >= 0) a.insert("axis", axis_);
    return a;
}

int MagnetController::suggestion(const ToolPlace& p) const {
    if (pinned_ >= 0) return pinned_;
    const QJsonObject s = ops_->runNow("assembly_suggest", {{"target", int(p.target)}, {"point", jsonOf(p.point)}, {"normal", jsonOf(p.normal)}});
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
void MagnetController::showPreview(const ToolPlace& p, int preset) {
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
    view_->setMagnetPreview(ghostLines(ghost_, phase_), glyphs_);
}

void MagnetController::preview(QPointF pos) {
    if (!active_) return;
    last_ = pos;
    hasLast_ = true;
    const ToolPlace p = placeUnder(builder_, view_, pos);
    if (!p.found) {
        showPreview(p, 0);
        return emit statusMessage(uiText("Kinetica", "Магнит: наведите на грань формы"));
    }
    const int preset = suggestion(p);
    showPreview(p, preset);
    emit statusMessage(QString(uiText("Kinetica", "Магнит · %1 — щелчок ставит; зажать и вести — меню; 1–5, 0, A ось, Tab, M зеркало, Esc")).arg(choiceText(preset)));
}

void MagnetController::press(QPointF pos) {
    if (!active_) return;
    const ToolPlace p = placeUnder(builder_, view_, pos);
    pressed_ = p;
    pressPos_ = pos;
    pieOpen_ = false;
    if (pressed_.found) pieTimer_->start();
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
    if (!active_ || !pressed_.found) return;
    if (!pieOpen_ && QLineF(pos, pressPos_).length() > 10) openPie();
    if (!pieOpen_) return;
    const int before = pie_->chosen();
    pie_->track(pos);
    if (pie_->chosen() != before) showPreview(pressed_, pie_->chosen() >= 0 ? pie_->chosen() : suggestion(pressed_));
}

void MagnetController::release(QPointF pos) {
    if (!active_) return;
    pieTimer_->stop();
    const ToolPlace p = pressed_;
    pressed_ = ToolPlace();
    const int sector = pieOpen_ ? PieMenu::sectorOf(pressPos_, pos, ke::kMagnetCount) : -1;
    pie_->hide();
    pieOpen_ = false;
    if (p.found) put(p, sector >= 0 ? sector : suggestion(p));
    preview(pos); // the new link is under the mouse now: its own faces
}

void MagnetController::put(const ToolPlace& p, int preset) {
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
    if (!active_) return;
    if (key == Qt::Key_Escape) return action_->setChecked(false);
    if (key >= Qt::Key_0 && key <= Qt::Key_0 + ke::kMagnetCount) return pin(key - Qt::Key_0 - 1);
    if (key == Qt::Key_A) axis_ = axis_ == -1 ? 2 : axis_ == 2 ? 0 : axis_ == 0 ? 1 : -1; // normal, across, across, own
    if (key == Qt::Key_Tab) flip_ = !flip_;
    if (key == Qt::Key_M) mirror_ = !mirror_;
    if (hasLast_) preview(last_);
}

