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

const char* lightName(LightKind k) { return ke::lightTitle(k); }

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
    for (const SensorObject& s : graph_.sensors)
        if (s.id == id) return deviceIcon(DeviceIcon(int(s.kind)), size);
    return findCamera(graph_, id) ? sceneIcon(SceneIcon::Camera, size) : QIcon();
}

// ---------------------------------------------------------------------------
// Making them
// ---------------------------------------------------------------------------
void SceneBuilder::buildLightActions() {
    const char* tips[] = {"Солнце: параллельный свет издалека, с тенями; важно только, куда оно светит",
                          "Лампа: светит во все стороны и гаснет к своей дальности", "Прожектор: конус света с мягким краем"};
    for (int k = 0; k < 3; ++k) {
        const LightKind kind = LightKind(k);
        auto* a = new QAction(sceneIcon(lightIcon(kind), 48), lightName(kind), this);
        a->setToolTip(tips[k]);
        connect(a, &QAction::triggered, this, [this, kind] { addLight(kind); });
        lightActions_.push_back(a);
    }
    cameraAct_ = new QAction(sceneIcon(SceneIcon::Camera, 48), "Камера", this);
    cameraAct_->setToolTip("Камера там, где сейчас вид редактора, смотрит в центр сцены. Через неё — скриншоты");
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
    emit firstAction();
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
    lightCard_ = new ComponentCard("Свет", sceneIcon(SceneIcon::Bulb, 22));
    lightCard_->setObjectName("lightCard");
    lightCard_->setToolTip("Свет только для глаза: физика его не видит. Цвет — в блоке «Объект»");
    std::vector<FieldForm::Tile> kinds;
    for (int k = 0; k < 3; ++k) kinds.push_back({sceneIcon(lightIcon(LightKind(k)), 32), lightName(LightKind(k))});
    Light* l = &lightDraft_;
    const auto spot = [l] { return l->kind == LightKind::Spot; };
    lightFields_ = new FieldForm([this](bool click) { commitSelected(lightFields_, lightDraft_, [this](uint32_t id) { return lightById(id); }, click); });
    lightFields_->tiles(kinds, 32, l->kind).name("lightKind")
        .number("Яркость", 0, 100, 0.1, 2, l->intensity)
        .number("Дальность, м", 0.1, 1000, 0.5, 2, l->range).shown([l] { return l->kind != LightKind::Sun; })
        .number("Конус, °", 1, 179, 1, 1, l->coneDeg).shown(spot)
        .number("Мягкий край, °", 0, 90, 1, 1, l->softnessDeg).shown(spot)
        .toggle("Отбрасывает тени", l->shadows).name("lightShadows")
        .tip("Тела отбрасывают тень от этого света. Каждая тень — лишний проход рендера каждый кадр (у лампы — шесть), поэтому у "
             "ламп и прожекторов она сначала выключена, как в Blender и Unity. Тени рисуются не больше чем от 4 источников сразу");
    lightCard_->body()->addWidget(lightFields_);
    connect(lightCard_, &ComponentCard::removeClicked, this, &SceneBuilder::removeSelected);
    col->addWidget(lightCard_);

    cameraCard_ = new ComponentCard("Камера", sceneIcon(SceneIcon::Camera, 22));
    cameraCard_->setObjectName("cameraCard");
    Camera* c = &cameraDraft_;
    cameraFields_ = new FieldForm([this](bool click) { commitSelected(cameraFields_, cameraDraft_, [this](uint32_t id) { return cameraById(id); }, click); });
    cameraFields_->number("Угол обзора, °", 1, 170, 1, 1, c->fovDeg)
        .number("Ближе не видит, м", 0.001, 10, 0.01, 3, c->nearClip)
        .number("Дальше не видит, м", 1, 100000, 10, 0, c->farClip);
    cameraCard_->body()->addWidget(cameraFields_);
    throughButton_ = new QPushButton(sceneIcon(SceneIcon::Camera, 20), "Смотреть через камеру");
    throughButton_->setObjectName("throughCamera");
    throughButton_->setCheckable(true);
    throughButton_->setToolTip("Вид станет этой камерой; движение вида двигает камеру. Esc — назад");
    connect(throughButton_, &QPushButton::toggled, this, [this](bool on) {
        if (!filling_) lookThrough(on ? selectedId_ : 0);
    });
    cameraCard_->body()->addWidget(throughButton_);
    connect(cameraCard_, &ComponentCard::removeClicked, this, &SceneBuilder::removeSelected);
    col->addWidget(cameraCard_);
}

// The active light's or camera's values, "—" where the selected ones differ; the rows a kind has no use for are hidden.
void SceneBuilder::fillLightCards(const SceneObject& o) {
    lightCard_->setVisible(lightById(o.id));
    cameraCard_->setVisible(cameraById(o.id));
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

// Every visible light and camera while editing; while playing only the selected ones. The camera the
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
    for (const Camera& c : graph_.cameras) {
        SceneMarker m;
        m.kind = SceneMarker::Camera;
        m.fovDeg = c.fovDeg;
        add(c, m);
    }
    emit sceneMarkers(std::move(out));
}

std::vector<PickHit> SceneBuilder::pickMarkers(const Ray& ray) const {
    std::vector<PickHit> hits;
    auto test = [&](const SceneObject& o, SceneMarker::Kind kind, float fovDeg) {
        if (o.locked || o.id == throughId_ || !effectivelyVisible(graph_, o)) return;
        SceneMarker m;
        m.kind = kind;
        m.fovDeg = fovDeg;
        worldPose(graph_, o, m.position, m.rotation);
        float t;
        if (markerHit(m, ray, t)) hits.push_back({o.id, t, ray.at(t), o.name});
    };
    for (const Light& l : graph_.lights) test(l, SceneMarker::Kind(int(l.kind)), 0);
    for (const Camera& c : graph_.cameras) test(c, SceneMarker::Camera, c.fovDeg);
    for (const SensorVis& v : sensorVisuals(graph_)) {
        const SceneObject* o = findObject(graph_, v.id);
        float t;
        if (o && !o->locked && sensorHit(v, ray, t)) hits.push_back({v.id, t, ray.at(t), o->name});
    }
    return hits;
}

void SceneBuilder::emitCameraView() {
    const Camera* c = findCamera(graph_, throughId_);
    if (!c) {
        throughId_ = 0; // the camera is gone (removed, undone, another scene)
        emit cameraView(false, Vector3(0.0f), Vector3(0.0f), Vector3(0.0f), 0, 0, 0);
        return;
    }
    Vector3 eye, forward, up;
    cameraFrame(graph_, *c, eye, forward, up);
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
    emit statusMessage(cameraId ? "Вид через камеру: движение вида двигает камеру. Esc — назад к виду редактора" : "Вид редактора");
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
        emit statusMessage("Камера заперта замком: вид не двигает её");
        return emitCameraView();
    }
    remember(true);
    const Vector3 right = normalize(cross(forward, up));
    const Matrix3x3 R = Matrix3x3::fromColumns(right, up, forward * -1.0f);
    setWorldPose(graph_, *c, eye, Quaternion::fromMatrix3x3(R));
    if (isSelected(c->id)) showTransform(*c);
    if (editing()) {
        refreshLightsAndCameras();
        updateGizmoTarget();
    } else {
        applyEdit(c->id);
    }
}
