// sim_impulse (EditorOps.h): a push on a body of the running scene - one world-space impulse on the body an
// entity became (ke::RobotWorld::bodyOf), applied by the physics engine on the simulation's thread
// (ke::PhysicsWorld::applyImpulse). It moves the world, not the scene: no edit, no undo step, and the answer
// says only that it was taken - never the body's pose or speed, which a robot learns through its sensors.
#include "EditorOps.h"

#include "EditorRuntime.h"
#include "SceneBuilder.h"
#include "SimController.h"
#include "sim/Simulation.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

#include <cmath>
#include <limits>

namespace {

// Three finite numbers a Real can hold.
bool vectorArg(const QJsonValue& value, ke::Vector3& out) {
    const QJsonArray a = value.toArray();
    if (!value.isArray() || a.size() != 3) return false;
    for (int k = 0; k < 3; ++k) {
        const double x = a[k].toDouble(std::nan(""));
        if (!a[k].isDouble() || !std::isfinite(x) || std::fabs(x) > double(std::numeric_limits<ke::Real>::max())) return false;
        out[k] = ke::Real(x);
    }
    return true;
}

} // namespace

void EditorOps::addImpulseOp() {
    add("sim_impulse",
        "Apply one physical world-space impulse in N s to an already built dynamic rigid body addressed by scene entity id. "
        "Optional point is in world metres; omitted point applies at the centre of mass. Wakes a sleeping body; adds momentum "
        "without teleporting, resetting velocity or editing the graph. Available while playing or paused after sim_step(0). "
        "Missing or fixed bodies and malformed or nonfinite commands are refused. Returns only acceptance, not the body's pose "
        "or velocity: this is external actuation, not a sensor observation.",
        "{body: positive scene entity id, impulse: [x,y,z] N s, point?: [x,y,z] world metres}",
        [this](const QJsonObject& a, const Reply& reply) {
            if (builder_->editing()) return reply(error("the scene is not running: play or step it first"));
            if (runtime_->stopping()) return reply(error("runtime is stopping"));
            for (auto it = a.begin(); it != a.end(); ++it)
                if (it.key() != "body" && it.key() != "impulse" && it.key() != "point") return reply(error("unknown sim_impulse field: " + it.key()));
            const double id = a.value("body").toDouble(-1);
            if (!a.value("body").isDouble() || !std::isfinite(id) || id < 1 || id > double(UINT32_MAX) || id != std::floor(id))
                return reply(error("body is a positive scene entity id"));
            ke::Vector3 impulse(0.0f), point(0.0f);
            if (!vectorArg(a.value("impulse"), impulse)) return reply(error("impulse is three finite N s values"));
            const bool atPoint = a.contains("point");
            if (atPoint && !vectorArg(a.value("point"), point)) return reply(error("point is three finite world metres"));
            const uint32_t entity = uint32_t(id);
            QPointer<EditorOps> self(this);
            ctrl_->post([self, reply, entity, impulse, point, atPoint](ke::Simulation& s) {
                QString why;
                const int body = s.robot() ? s.robot()->bodyOf(entity) : -1;
                if (body < 0 || !s.physics()->alive(body)) why = "body entity has no live rigid body";
                else if (!(s.physics()->state(body).mass > 0)) why = "body must be dynamic (a fixed one does not move)";
                else {
                    const ke::Vector3 at = atPoint ? point : s.physics()->state(body).centre;
                    s.physics()->wake(body);
                    s.physics()->applyImpulse(body, impulse, at);
                }
                QMetaObject::invokeMethod(qApp, [self, reply, why, entity] {
                    if (!self) return;
                    reply(why.isEmpty() ? ok({{"accepted", true}, {"body", int(entity)}}) : error(why));
                }, Qt::QueuedConnection);
            }, "sim_impulse");
        });
}
