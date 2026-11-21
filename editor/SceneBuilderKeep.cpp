// "Сохранить из симуляции" (K) (see SceneBuilder.h). Play
// and Stop: whatever the simulation did is gone after Stop. K keeps it: the live
// poses of the selected objects (of all, when nothing is selected) become their poses in the scene
// that Stop brings back. After Stop, one Ctrl+Z puts them back where they were before K.
//
// The live pose is read on the simulation's thread from the object's body in the physics engine: the
// running world builds a body at its entity's pose (RobotWorld::build), so the body's pose is the
// object's. What is kept is what moves freely (RobotWorld::isFree): a body moved by forces or by its own
// velocity. A fixed body never moves; a body a sequence leads is where its track puts it; an array's
// copies and glued groups are not kept (their poses are not their own).
#include "SceneBuilder.h"
#include "UiLanguage.h"


#include <QPointer>

using namespace ke;

namespace {

// Where the object `id` is now in the simulation (false: it has no body that moves).
bool livePose(const Simulation& s, uint32_t id, Vector3& position, Quaternion& rotation) {
    const int b = s.robot() ? s.robot()->bodyOf(id) : -1;
    if (b < 0 || !s.physics()->alive(b) || !s.robot()->isFree(id)) return false;
    const BodyState state = s.physics()->state(b);
    position = state.position, rotation = state.rotation;
    return true;
}

} // namespace

void SceneBuilder::keepSimulationPoses() {
    if (editing()) return;
    std::vector<uint32_t> ids; // the selected objects' shapes, or every shape
    for (const Entity& e : graph_.entities) {
        bool wanted = selection_.empty();
        for (uint32_t s : selection_) wanted |= isInside(e.id, s);
        if (wanted) ids.push_back(e.id);
    }
    QPointer<SceneBuilder> self(this);
    ctrl_->post([ids, self](Simulation& s) {
        std::vector<KeptPose> poses;
        for (uint32_t id : ids) {
            KeptPose k;
            k.id = id;
            if (livePose(s, id, k.position, k.rotation)) poses.push_back(k);
        }
        QMetaObject::invokeMethod(self, [self, poses] {
            if (self) self->applyKeptPoses(poses);
        }, Qt::QueuedConnection);
    }, "keep");
}

// Back from the simulation: the poses go into the scene Stop brings back (and into the graphs of
// the running scene, so the next edit does not look like a move).
void SceneBuilder::applyKeptPoses(const std::vector<KeptPose>& poses) {
    if (editing()) return; // stopped in the meantime
    if (!hasKeptBefore_) { // the first K of this play: the undo step Stop will add
        keptBefore_ = doc_.backup;
        hasKeptBefore_ = true;
    }
    for (const KeptPose& k : poses)
        for (SceneGraph* g : {&doc_.backup, &graph_, &doc_.running}) {
            SceneObject* o = const_cast<SceneObject*>(findObject(*g, k.id)); // our own graph: ours to change
            if (!o) continue;
            setWorldPose(*g, *o, k.position, k.rotation);
        }
    if (const SceneObject* o = selectedObject()) showTransform(*o);
    emit statusMessage(QString(uiText("Kinetica", "Сохранено из симуляции: %1 — после ■ Стоп останутся здесь (Ctrl+Z после Стоп вернёт)"))
                           .arg(uiCount(static_cast<long long>(poses.size()), UiCount::Object)));
}
