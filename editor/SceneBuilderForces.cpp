// Forces in the scene builder (see SceneBuilder.h): making one from «Сила ▾» and its "Сила" card with the
// «Сработать» button. A force is placed like a sensor, at once: over the selection, else at the scene's
// centre, then moved and turned with the gizmo. Its marks in the view are in SceneMarkers.cpp and picked in
// SceneBuilderLights.cpp; what it does to the bodies while the world runs is the core's (sim/RobotWorld.cpp).
// The card writes the draft and the core judges it: a refusal comes back as the operation's status text.
#include "SceneBuilder.h"
#include "UiLanguage.h"

#include "InspectorWidgets.h"
#include "scene/SceneActions.h"

#include <QBoxLayout>
#include <QPushButton>

#include <set>

using namespace ke;

SceneIcon forceIcon(ForceShape s) { return s == ForceShape::Radial ? SceneIcon::ForceBall : SceneIcon::ForceArrow; }

Force* SceneBuilder::forceById(uint32_t id) {
    for (Force& f : graph_.forces)
        if (id != 0 && f.id == id) return &f;
    return nullptr;
}

void SceneBuilder::addForce(ForceShape shape, ForceMode mode) {
    flushCode();
    // Beside what is selected, not inside it (there its ring is hidden and a radial force has no direction to push
    // in): a directional force over its top, pushing up; a radial one at its side, pushing it away.
    Vector3 at = spawnTarget();
    if (const AABB box = findObject(graph_, selectedId_) ? boundsOf(selectedId_) : AABB(); box.valid())
        at = box.center() + (shape == ForceShape::Radial ? Vector3(-0.5f * (box.hi.x - box.lo.x) - 0.5f, 0, 0) : Vector3(0, 0.5f * (box.hi.y - box.lo.y) + 0.3f, 0));
    remember();
    addObjectDone(coreEdit([&](SceneGraph& g) { return ke::addForce(g, shape, mode, at); }));
    const QString shapeWord = kForceShapeNames[int(shape)], modeWord = kForceModeNames[int(mode)];
    codeAction("scene_add_force", QString("scene.add_force(\"%1\", \"%2\")").arg(shapeWord, modeWord),
               {{"shape", shapeWord}, {"mode", modeWord}, {"at", QJsonArray{double(at.x), double(at.y), double(at.z)}}}, false);
}

// The orange points of the one selected force, in edit: the end of a directional force's arrow - dragged, the force
// turns to push there - and a point on its reach, dragged along the force's own x: the radius. A radial force has the
// second alone.
std::vector<ShapeHandle> SceneBuilder::forceHandles() {
    std::vector<ShapeHandle> out;
    const Force* f = editing() && selection_.size() == 1 ? forceById(selectedId_) : nullptr;
    if (!f) return out;
    Vector3 p;
    Quaternion q;
    worldPose(graph_, *f, p, q);
    ShapeHandle h;
    h.plugin = true;
    if (f->shape == ForceShape::Directional) {
        h.free = true, h.axis = Vector3(1, 0, 0), h.reach = 0;
        h.centre = p + forceAxis(q) * (f->strength < 0 ? -1.0f : 1.0f);
        h.label = uiText("Kinetica", "Направление");
        out.push_back(h);
    }
    if (f->radius > 0) {
        h.free = false, h.centre = p, h.axis = q.rotate(Vector3(1, 0, 0)), h.reach = float(f->radius);
        h.label = uiText("Kinetica", "Радиус");
        out.push_back(h);
    }
    return out;
}

void SceneBuilder::onForceHandle(int index, Vector3 point, int phase) {
    if (phase == 0) {
        componentGraph_ = graph_;
        componentDragging_ = true;
        return;
    }
    const Force* start = nullptr;
    for (const Force& f : componentGraph_.forces)
        if (f.id == selectedId_) start = &f;
    if (!componentDragging_ || !start || index < 0) return;
    const auto shown = [this] {
        refreshLightsAndCameras();
        emitShapeHandles();
        if (const SceneObject* o = selectedObject()) showTransform(*o);
    };
    if (phase == 3) { // Esc: back as it was
        graph_ = componentGraph_;
        componentDragging_ = false;
        return shown();
    }
    Vector3 p;
    Quaternion q;
    worldPose(componentGraph_, *start, p, q);
    const bool aim = start->shape == ForceShape::Directional && index == 0;
    const auto drag = [&](Force& f) {
        if (!aim) {
            f.radius = std::clamp(dot(point - p, q.rotate(Vector3(1, 0, 0))), 0.05f, float(kForceMaxRadius));
            return;
        }
        const Vector3 to = point - p;
        if (dot(to, to) < 1e-6f) return; // dropped on the force itself: no direction
        const Vector3 from = forceAxis(q) * (start->strength < 0 ? -1.0f : 1.0f);
        setWorldPose(graph_, f, p, Quaternion::fromTwoVectors(from, to) * q);
    };
    if (phase == 1) { // live: the mark follows the mouse; the undo step and the journal's line at the release
        drag(*forceById(selectedId_));
        return shown();
    }
    componentDragging_ = false;
    graph_ = componentGraph_;
    remember();
    drag(*forceById(selectedId_));
    afterCardEdit(selectedId_, true);
    shown();
}

void SceneBuilder::buildForceCard(QVBoxLayout* col) {
    forceCard_ = new ComponentCard(uiText("Kinetica", "Сила"), sceneIcon(SceneIcon::Force, 22));
    forceCard_->setObjectName("forceCard");
    forceCard_->setToolTip(uiText("Kinetica", "Сила действует на центр масс твёрдых тел, пока мир идёт, и не крутит их. Цвет — в блоке «Объект»"));
    Force* d = &forceDraft_;
    const auto impulse = [d] { return d->mode == ForceMode::Impulse; };
    forceFields_ = new FieldForm([this](bool click) { commitSelected(forceFields_, forceDraft_, [this](uint32_t id) { return forceById(id); }, click); });
    forceFields_->choice(uiText("Kinetica", "Форма"), QStringList{uiText("Kinetica", "Направленная"), uiText("Kinetica", "Радиальная")},
                         [d] { return double(int(d->shape)); }, [d](double v) {
                             d->shape = ForceShape(int(v));
                             if (d->shape == ForceShape::Radial && d->radius < 0.01f) d->radius = 3.0f; // a radial force needs a reach
                         }).name("forceShape")
        .tip(uiText("Kinetica", "Направленная толкает вдоль своей оси +Y, радиальная — от своей точки (к ней, если сила отрицательная)"));
    forceFields_->choice(uiText("Kinetica", "Режим"), QStringList{uiText("Kinetica", "Импульс"), uiText("Kinetica", "Постоянная")},
                         [d] { return double(int(d->mode)); }, [d](double v) { d->mode = ForceMode(int(v)); }).name("forceMode")
        .tip(uiText("Kinetica", "Импульс срабатывает один раз, в заданный момент или кнопкой; постоянная действует всё время"));
    forceFields_->number(uiText("Kinetica", "Сила, Н·с"), -double(kForceMaxStrength), double(kForceMaxStrength), 1, 2, d->strength)
        .label([d] { return QString(d->mode == ForceMode::Impulse ? uiText("Kinetica", "Сила, Н·с") : uiText("Kinetica", "Сила, Н")); }).name("forceStrength");
    forceFields_->number(uiText("Kinetica", "Радиус, м"), 0, double(kForceMaxRadius), 0.5, 2, d->radius).name("forceRadius")
        .tip(uiText("Kinetica", "0 у направленной — без предела"));
    forceFields_->choice(uiText("Kinetica", "Спад"), QStringList{uiText("Kinetica", "Нет"), uiText("Kinetica", "Линейный"), uiText("Kinetica", "Обратный квадрат")},
                         [d] { return double(int(d->falloff)); }, [d](double v) { d->falloff = ForceFalloff(int(v)); }).name("forceFalloff");
    forceFields_->number(uiText("Kinetica", "Момент, с"), 0, double(kForceMaxStart), 0.1, 2, d->start).shown(impulse).name("forceStart")
        .tip(uiText("Kinetica", "Время мира от пуска, когда импульс срабатывает сам"));
    forceFields_->choice(uiText("Kinetica", "Действует на"), [this] {
        std::vector<FieldForm::Option> out;
        for (size_t i = 0; i < forceTags_.size(); ++i)
            out.push_back({forceTags_[i].empty() ? QString(uiText("Kinetica", "Все тела")) : QString::fromStdString(forceTags_[i]), double(i)});
        return out;
    }, [this] {
        const auto at = std::find(forceTags_.begin(), forceTags_.end(), forceDraft_.tag);
        return double(at == forceTags_.end() ? 0 : at - forceTags_.begin());
    }, [this](double i) { forceDraft_.tag = forceTags_[size_t(i)]; }).name("forceTag")
        .tip(uiText("Kinetica", "Только тела с этой меткой (метки — в блоке «Объект»); пусто — все тела"));
    forceCard_->body()->addWidget(forceFields_);
    forceFire_ = new QPushButton(uiText("Kinetica", "Сработать"));
    forceFire_->setObjectName("forceFire");
    forceFire_->setToolTip(uiText("Kinetica", "Импульс срабатывает сейчас, в идущем мире; когда настанет его момент, он второй раз не сработает"));
    connect(forceFire_, &QPushButton::clicked, this, [this] { emit fireForceRequested(selectedId_); });
    connect(this, &SceneBuilder::modeChanged, this, [this] { forceFire_->setEnabled(!editing()); });
    forceCard_->body()->addWidget(forceFire_);
    connect(forceCard_, &ComponentCard::removeClicked, this, &SceneBuilder::removeSelected);
    col->addWidget(forceCard_);
}

// The active force's values, "—" where the selected ones differ; «Действует на» lists the tags of the scene and of the selected forces.
void SceneBuilder::fillForceCard(const SceneObject& o) {
    const Force* f = forceById(o.id);
    forceCard_->setVisible(f);
    if (!f) return;
    std::set<std::string> tags;
    eachObjectList(graph_, [&](const auto& list) {
        for (const auto& x : list) tags.insert(x.tags.begin(), x.tags.end());
    });
    for (uint32_t id : selection_)
        if (const Force* s = forceById(id); s && !s->tag.empty()) tags.insert(s->tag);
    forceTags_.assign(1, std::string());
    forceTags_.insert(forceTags_.end(), tags.begin(), tags.end());
    fillSelected(forceFields_, forceDraft_, [this](uint32_t id) { return forceById(id); });
    forceFire_->setVisible(f->mode == ForceMode::Impulse);
    forceFire_->setEnabled(!editing());
}
