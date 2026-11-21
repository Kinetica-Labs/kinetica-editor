// Lights and cameras in the scene builder (see SceneBuilder.h): making them, their inspector cards,
// their wireframes in the view and picking them, and the view through a camera.
//
// Where a new one appears: the sun high over the scene, shining down at a slant (with shadows); a
// lamp 1.5 m above the selection (or the centre of the shapes); a spotlight above and to the side,
// aimed at it; a camera where the editor's eye is, looking at the scene's centre. A light shines
// along its -y axis and a camera looks along its -z (ke::lightDirection, ke::cameraFrame).
//
// Through a camera the view IS that camera. Orbiting, panning, zooming or flying the view moves the
// camera object (each move merged into one undo step, like a drag); a locked camera does not move
// and the view snaps back to it. Esc or the button again gives the editor's own view back.
#include "SceneBuilder.h"
#include "UiLanguage.h"

#include "InspectorWidgets.h"
#include "ObjectInspector.h"
#include "SensorGlyphs.h"
#include "scene/Assembly.h"
#include "scene/SceneActions.h"

#include <QAction>
#include <QBoxLayout>
#include <QPushButton>

#include <algorithm>
#include <cmath>

using namespace ke;

const char* lightName(LightKind k) { return uiText("Kinetica", ke::lightTitle(k)); }

SceneIcon lightIcon(LightKind k) {
    static const SceneIcon icons[] = {SceneIcon::Sun, SceneIcon::Bulb, SceneIcon::Spot};
    return icons[std::clamp(int(k), 0, 2)];
}

namespace {

const Camera* findCamera(const SceneGraph& g, uint32_t id) {
    for (const Camera& c : g.cameras)
        if (id != 0 && c.id == id) return &c;
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------
Light* SceneBuilder::lightById(uint32_t id) {
    for (Light& l : graph_.lights)
        if (id != 0 && l.id == id) return &l;
    return nullptr;
}

Camera* SceneBuilder::cameraById(uint32_t id) {
    for (Camera& c : graph_.cameras)
        if (id != 0 && c.id == id) return &c;
    return nullptr;
}

QIcon SceneBuilder::kindIcon(uint32_t id, int size) const {
    for (const Light& l : graph_.lights)
        if (l.id == id) return sceneIcon(lightIcon(l.kind), size);
    for (const Force& f : graph_.forces)
        if (f.id == id) return sceneIcon(forceIcon(f.shape), size);
    for (const SensorObject& s : graph_.sensors)
        if (s.id == id) return deviceIcon(DeviceIcon(int(s.kind)), size);
    return findCamera(graph_, id) ? sceneIcon(SceneIcon::Camera, size) : QIcon();
}

// ---------------------------------------------------------------------------
// Making them
// ---------------------------------------------------------------------------
void SceneBuilder::buildLightActions() {
    const char* tips[] = {uiText("Kinetica", "Солнце: параллельный свет издалека, с тенями; важно только, куда оно светит"),
                          uiText("Kinetica", "Лампа: светит во все стороны и гаснет к своей дальности"), uiText("Kinetica", "Прожектор: конус света с мягким краем")};
    for (int k = 0; k < 3; ++k) {
        const LightKind kind = LightKind(k);
        auto* a = new QAction(sceneIcon(lightIcon(kind), 48), lightName(kind), this);
        a->setToolTip(tips[k]);
        connect(a, &QAction::triggered, this, [this, kind] { addLight(kind); });
        lightActions_.push_back(a);
    }
    cameraAct_ = new QAction(sceneIcon(SceneIcon::Camera, 48), uiText("Kinetica", "Камера"), this);
    cameraAct_->setToolTip(uiText("Kinetica", "Камера там, где сейчас вид редактора, смотрит в центр сцены. Через неё — скриншоты"));
    connect(cameraAct_, &QAction::triggered, this, [this] { addCamera(); });
}

Vector3 SceneBuilder::spawnTarget() {
    if (findObject(graph_, selectedId_)) return worldPositionOf(selectedId_);
    const AABB box = ke::sceneBounds(graph_);
    return box.valid() ? box.center() : Vector3(0.0f);
}

void SceneBuilder::addObjectDone(uint32_t id) {
    setSelected(id);
    refreshList();
    applyEdit(id);
}

void SceneBuilder::addLight(LightKind kind, const Vector3* over) {
    flushCode();
    const Vector3 at = over ? *over : spawnTarget();
    remember();
    addObjectDone(coreEdit([&](SceneGraph& g) { return ke::addLight(g, kind, at); }));
    codeAction("scene_add_light", QString("scene.add_light(\"%1\")").arg(lightWord(kind)), {{"kind", lightWord(kind)}}, false);
}

void SceneBuilder::addCamera(const Vector3* at) {
    flushCode();
    const AABB box = ke::sceneBounds(graph_);
    const Vector3 centre = box.valid() ? box.center() : Vector3(0.0f, 0.5f, 0.0f);
    Vector3 eye = at ? *at : viewEye_ ? viewEye_() : centre + Vector3(2.5f, 1.5f, 2.5f);
    if (length(eye - centre) < 0.05f) eye = centre + Vector3(0.0f, 0.5f, 2.0f);
    remember();
    addObjectDone(coreEdit([&](SceneGraph& g) { return ke::addCamera(g, eye, centre); }));
    codeAction("scene_add_camera", "scene.add_camera()", {}, false);
}

void SceneBuilder::editLight(uint32_t id, const std::function<void(Light&)>& change) {
    if (!lightById(id)) return;
    remember();
    change(*lightById(id));
    refreshList();
    if (isSelected(id)) fillInspector();
    applyEdit(id);
}

void SceneBuilder::editCamera(uint32_t id, const std::function<void(Camera&)>& change) {
    if (!cameraById(id)) return;
    remember();
    change(*cameraById(id));
    refreshList();
    if (isSelected(id)) fillInspector();
    applyEdit(id);
}

// ---------------------------------------------------------------------------
// The "Свет" and "Камера" cards
// ---------------------------------------------------------------------------
void SceneBuilder::buildLightCards(QVBoxLayout* col) {
    lightCard_ = new ComponentCard(uiText("Kinetica", "Свет"), sceneIcon(SceneIcon::Bulb, 22));
    lightCard_->setObjectName("lightCard");
    lightCard_->setToolTip(uiText("Kinetica", "Свет только для глаза: физика его не видит. Цвет — в блоке «Объект»"));
    std::vector<FieldForm::Tile> kinds;
    for (int k = 0; k < 3; ++k) kinds.push_back({sceneIcon(lightIcon(LightKind(k)), 32), lightName(LightKind(k))});
    Light* l = &lightDraft_;
    const auto spot = [l] { return l->kind == LightKind::Spot; };
    lightFields_ = new FieldForm([this](bool click) { commitSelected(lightFields_, lightDraft_, [this](uint32_t id) { return lightById(id); }, click); });
    lightFields_->tiles(kinds, 32, l->kind).name("lightKind")
        .number(uiText("Kinetica", "Яркость"), 0, 100, 0.1, 2, l->intensity)
        .number(uiText("Kinetica", "Дальность, м"), 0.1, 1000, 0.5, 2, l->range).shown([l] { return l->kind != LightKind::Sun; })
        .number(uiText("Kinetica", "Конус, °"), 1, 179, 1, 1, l->coneDeg).shown(spot)
        .number(uiText("Kinetica", "Мягкий край, °"), 0, 90, 1, 1, l->softnessDeg).shown(spot)
        .toggle(uiText("Kinetica", "Отбрасывает тени"), l->shadows).name("lightShadows")
        .tip(uiText("Kinetica", "Тела отбрасывают тень от этого света. Каждая тень — лишний проход рендера каждый кадр (у лампы — шесть), поэтому у "
             "ламп и прожекторов она сначала выключена. Тени рисуются не больше чем от 4 источников сразу"));
    lightCard_->body()->addWidget(lightFields_);
    connect(lightCard_, &ComponentCard::removeClicked, this, &SceneBuilder::removeSelected);
    col->addWidget(lightCard_);

    cameraCard_ = new ComponentCard(uiText("Kinetica", "Камера"), sceneIcon(SceneIcon::Camera, 22));
    cameraCard_->setObjectName("cameraCard");
    Camera* c = &cameraDraft_;
    cameraFields_ = new FieldForm([this](bool click) { commitSelected(cameraFields_, cameraDraft_, [this](uint32_t id) { return cameraById(id); }, click); });
    // Free or target, and what it looks at: its own target (a cube the gizmo moves) or
    // another object. Not the draft's to edit: a target is an object of its own, made or removed - camera_aim does it.
    cameraAimFields_ = new FieldForm([this](bool) { aimSelectedCameras(); });
    cameraAimFields_->tiles({{sceneIcon(SceneIcon::Camera, 26), uiText("Kinetica", "Свободная"), uiText("Kinetica", "Смотрит туда, куда повёрнута: поворот — гизмо или вид через неё")},
                             {sceneIcon(SceneIcon::Camera, 26), uiText("Kinetica", "С целью"), uiText("Kinetica", "Всегда смотрит на цель — свой кубик (двигайте его гизмо) или другой "
                                                                         "объект; двигаете камеру или цель, она поворачивается за ней; в игре следит за телом")}}, 26,
                            [this] { return cameraAim_.first; }, [this](double k) { cameraAim_.first = k; }).name("cameraKind")
        .choice(uiText("Kinetica", "Цель"), [this] {
            std::vector<FieldForm::Option> out{{uiText("Kinetica", "свой кубик-цель"), 0}};
            for (const Entity& e : graph_.entities)
                if (!e.targetOf) out.push_back({QString::fromStdString(e.name), double(e.id)});
            return out;
        }, [this] { return cameraAim_.second; }, [this](double id) { cameraAim_.second = id; })
        .name("cameraTarget").shown([this] { return cameraAim_.first == 1; })
        .tip(uiText("Kinetica", "На что смотрит камера: свой кубик-цель (выберите его и двигайте гизмо) или объект — за ним она следит и в игре"));
    cameraCard_->body()->addWidget(cameraAimFields_);
    cameraFields_->number(uiText("Kinetica", "Угол обзора, °"), 1, 170, 1, 1, c->fovDeg)
        .number(uiText("Kinetica", "Ближе не видит, м"), 0.001, 10, 0.01, 3, c->nearClip)
        .number(uiText("Kinetica", "Дальше не видит, м"), 1, 100000, 10, 0, c->farClip);
    cameraCard_->body()->addWidget(cameraFields_);
    throughButton_ = new QPushButton(sceneIcon(SceneIcon::Camera, 20), uiText("Kinetica", "Смотреть через камеру"));
    throughButton_->setObjectName("throughCamera");
    throughButton_->setCheckable(true);
    throughButton_->setToolTip(uiText("Kinetica", "Вид станет этой камерой; движение вида двигает камеру. Esc — назад"));
    connect(throughButton_, &QPushButton::toggled, this, [this](bool on) {
        if (!filling_) lookThrough(on ? selectedId_ : 0);
    });
    cameraCard_->body()->addWidget(throughButton_);
    connect(cameraCard_, &ComponentCard::removeClicked, this, &SceneBuilder::removeSelected);
    col->addWidget(cameraCard_);
}

void SceneBuilder::aimSelectedCameras() {
    if (filling_ || !cameraById(selectedId_)) return;
    flushCode();
    remember();
    const bool target = cameraAim_.first == 1;
    const uint32_t at = uint32_t(cameraAim_.second);
    for (uint32_t id : selection_) {
        if (!cameraById(id)) continue;
        if (target) aimCamera(graph_, id, at, nullptr);
        else freeCamera(graph_, id);
        codeAction("camera_aim", target ? (at ? QString("ed.camera_aim(camera=%1, at=%2)").arg(id).arg(at) : QString("ed.camera_aim(camera=%1)").arg(id))
                                        : QString("ed.camera_aim(camera=%1, free=True)").arg(id),
                   target ? (at ? QJsonObject{{"camera", int(id)}, {"at", int(at)}} : QJsonObject{{"camera", int(id)}})
                          : QJsonObject{{"camera", int(id)}, {"free", true}}, false);
    }
    afterCardEdit(selectedId_, true);
}

// The active light's or camera's values, "—" where the selected ones differ; the rows a kind has no use for are hidden.
void SceneBuilder::fillLightCards(const SceneObject& o) {
    lightCard_->setVisible(lightById(o.id));
    cameraCard_->setVisible(cameraById(o.id));
    if (const Camera* k = cameraById(o.id)) {
        const Entity* t = k->kind == CameraKind::Target ? entityById(k->target) : nullptr;
        cameraAim_ = {double(int(k->kind)), t && t->targetOf == k->id ? 0.0 : double(k->target)};
        cameraAimFields_->fill();
    }
    fillSelected(lightFields_, lightDraft_, [this](uint32_t id) { return lightById(id); });
    fillSelected(cameraFields_, cameraDraft_, [this](uint32_t id) { return cameraById(id); });
    throughButton_->setChecked(cameraById(o.id) && throughId_ == o.id);
}

// ---------------------------------------------------------------------------
// In the view: the wireframes, picking them, the view through a camera
// ---------------------------------------------------------------------------
void SceneBuilder::refreshLightsAndCameras() {
    refreshMarkers();
    emitCameraView();
}

// Every visible light, camera and force while editing; while playing only the selected ones. The camera the
// view looks through is not drawn (the view is inside it).
void SceneBuilder::refreshMarkers() {
    std::vector<SceneMarker> out;
    auto add = [&](const SceneObject& o, SceneMarker m) {
        if (o.id == throughId_ || !effectivelyVisible(graph_, o)) return;
        m.selected = drawnSelected(o.id);
        if (!editing() && !m.selected) return;
        m.id = o.id;
        m.color = o.color;
        worldPose(graph_, o, m.position, m.rotation);
        out.push_back(m);
    };
    for (const Light& l : graph_.lights) {
        SceneMarker m;
        m.kind = SceneMarker::Kind(int(l.kind));
        m.range = l.range;
        m.coneDeg = l.coneDeg;
        m.softnessDeg = l.softnessDeg;
        add(l, m);
    }
    for (const Force& f : graph_.forces) {
        add(f, forceMarker(graph_, f));
        if (const Frame::CameraPose* live = liveCamera(f.id); live && !out.empty() && out.back().id == f.id)
            out.back().position = live->position, out.back().rotation = live->rotation;
    }
    for (const Camera& c : graph_.cameras) {
        SceneMarker m;
        m.kind = SceneMarker::Camera;
        m.fovDeg = c.fovDeg;
        add(c, m);
        if (c.kind == CameraKind::Target && !out.empty() && out.back().id == c.id)
            out.back().aimed = true, out.back().aim = cameraTarget(graph_, c), out.back().rotation = lookTurn(out.back().position, out.back().aim);
        if (const Frame::CameraPose* live = liveCamera(c.id); live && !out.empty() && out.back().id == c.id)
            out.back().position = live->position, out.back().rotation = live->rotation;
    }
    emit sceneMarkers(std::move(out));
}

std::vector<PickHit> SceneBuilder::pickMarkers(const Ray& ray) const {
    std::vector<PickHit> hits;
    auto test = [&](const SceneObject& o, SceneMarker m) {
        if (o.locked || o.id == throughId_ || !effectivelyVisible(graph_, o)) return;
        worldPose(graph_, o, m.position, m.rotation);
        if (const Camera* c = findCamera(graph_, o.id); c && c->kind == CameraKind::Target) m.rotation = lookTurn(m.position, cameraTarget(graph_, *c));
        float t;
        if (markerHit(m, ray, t)) hits.push_back({o.id, t, ray.at(t), o.name});
    };
    const auto marker = [](SceneMarker::Kind kind, float fovDeg) {
        SceneMarker m;
        m.kind = kind, m.fovDeg = fovDeg;
        return m;
    };
    for (const Light& l : graph_.lights) test(l, marker(SceneMarker::Kind(int(l.kind)), 0));
    for (const Force& f : graph_.forces) test(f, forceMarker(graph_, f)); // its arrow as long as it is drawn
    for (const Camera& c : graph_.cameras) test(c, marker(SceneMarker::Camera, c.fovDeg));
    for (const SensorVis& v : sensorVisuals(graph_)) {
        const SceneObject* o = findObject(graph_, v.id);
        float t;
        if (o && !o->locked && sensorHit(v, ray, t)) hits.push_back({v.id, t, ray.at(t), o->name});
    }
    return hits;
}

// The view through the camera: a target camera looks at its target where it is now - in play where the running world
// has taken its object (the last frame's pose), so the view follows a gripper.
void SceneBuilder::emitCameraView() {
    const Camera* c = findCamera(graph_, throughId_);
    if (!c) {
        throughId_ = 0; // the camera is gone (removed, undone, another scene)
        emit cameraView(false, Vector3(0.0f), Vector3(0.0f), Vector3(0.0f), 0, 0, 0);
        return;
    }
    Vector3 eye, forward, up;
    cameraFrame(graph_, *c, eye, forward, up);
    if (const Frame::CameraPose* live = liveCamera(c->id))
        eye = live->position, forward = live->rotation.rotate(Vector3(0, 0, -1)), up = live->rotation.rotate(Vector3(0, 1, 0));
    if (c->kind == CameraKind::Target && c->target && !editing())
        if (const auto at = liveObjectPositions_.find(c->target); at != liveObjectPositions_.end()) {
            const Quaternion turn = lookTurn(eye, at->second);
            forward = turn.rotate(Vector3(0, 0, -1)), up = turn.rotate(Vector3(0, 1, 0));
        }
    emit cameraView(true, eye, forward, up, c->fovDeg, c->nearClip, c->farClip);
}

void SceneBuilder::lookThrough(uint32_t cameraId) {
    if (cameraId && !cameraById(cameraId)) return;
    throughId_ = cameraId;
    if (cameraId)
        for (Camera& c : graph_.cameras) c.active = c.id == cameraId; // saved with the scene, not an undo step
    refreshList();
    filling_ = true;
    if (const SceneObject* o = selectedObject()) fillLightCards(*o);
    filling_ = false;
    refreshLightsAndCameras();
    if (!editing()) applyEditDuringPlay(0); // the running scene's cameras follow
    emit statusMessage(cameraId ? uiText("Kinetica", "Вид через камеру: движение вида двигает камеру. Esc — назад к виду редактора") : uiText("Kinetica", "Вид редактора"));
}

// In play, a camera or a force the running world moves: where the last frame has it.
const Frame::CameraPose* SceneBuilder::liveCamera(uint32_t id) const {
    if (editing()) return nullptr;
    for (const std::vector<Frame::CameraPose>* list : {&liveCameras_, &liveForces_})
        for (const Frame::CameraPose& c : *list)
            if (c.id == id) return &c;
    return nullptr;
}

void SceneBuilder::lookThroughActiveCamera() {
    uint32_t id = graph_.cameras.empty() ? 0 : graph_.cameras.front().id;
    for (const Camera& c : graph_.cameras)
        if (c.active) id = c.id;
    if (id) lookThrough(id);
}

// The camera's world turn from the view's frame: its x is the screen's right, y the screen's up and
// -z the view's forward. A camera in a group keeps its pose relative to the group.
void SceneBuilder::onViewMovedThroughCamera(const Vector3& eye, const Vector3& forward, const Vector3& up) {
    Camera* c = cameraById(throughId_);
    if (!c) return;
    if (c->locked) {
        emit statusMessage(uiText("Kinetica", "Камера заперта замком: вид не двигает её"));
        return emitCameraView();
    }
    remember(true);
    const Vector3 right = normalize(cross(forward, up));
    const Matrix3x3 R = Matrix3x3::fromColumns(right, up, forward * -1.0f);
    // A target camera moves with the view but keeps looking at its target: the view snaps back to it.
    setWorldPose(graph_, *c, eye, c->kind == CameraKind::Target ? lookTurn(eye, cameraTarget(graph_, *c)) : Quaternion::fromMatrix3x3(R));
    if (c->kind == CameraKind::Target) emitCameraView();
    if (isSelected(c->id)) showTransform(*c);
    if (editing()) {
        refreshLightsAndCameras();
        updateGizmoTarget();
    } else {
        applyEdit(c->id);
    }
}
