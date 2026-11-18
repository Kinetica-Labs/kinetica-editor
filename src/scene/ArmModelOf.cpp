// A scene's arm as the arm planner's model (robot/ArmPlanner.h): the hinges of ikChain from the base to the effector,
// their frames as the joint keeps them (anchor and frame of each side, Assembly.cpp jointWorldFrames), their limits
// within -pi..pi (a coordinate read from a pose wraps there: GenericJoint frameCoordinates), and each link drawn as
// spheres over its surface and over what hangs on it but is no link of the chain - the fingers on the hand - sampled a
// centimetre apart on every triangle and posed in the link's frame as the scene stands.
#include "scene/Kinematics.h"

#include "scene/Assembly.h"
#include "robot/ArmSurfaceCover.h"

#include <algorithm>
#include <cmath>

namespace ke {

namespace {

const Entity* findEntity(const SceneGraph& g, uint32_t id) {
    for (const Entity& e : g.entities)
        if (e.id == id) return &e;
    return nullptr;
}

void poseOf(const SceneGraph& g, uint32_t id, ArmPose& out) {
    out = ArmPose();
    if (const Entity* e = findEntity(g, id)) worldPose(g, *e, out.position, out.turn);
}

// The entity's surface a centimetre apart, in the frame `link` (both posed as the scene stands).
void surfacePoints(const SceneGraph& g, const Entity& e, const ArmPose& link, std::vector<Vector3>& out) {
    const TriMesh mesh = entityLocalMesh(resolveInstance(g, e), g.baseDirectory);
    ArmPose at;
    worldPose(g, e, at.position, at.turn);
    const Quaternion back = link.turn.conjugate();
    const auto put = [&](const Vector3& local) { out.push_back(back.rotate(at.position + at.turn.rotate(local) - link.position)); };
    for (const auto& t : mesh.triangles) {
        const Vector3 a = mesh.positions[t[0]], b = mesh.positions[t[1]], c = mesh.positions[t[2]];
        const int n = std::max(1, int(std::ceil(std::max(length(b - a), std::max(length(c - b), length(a - c))) / 0.01_r)));
        for (int i = 0; i <= n; ++i)
            for (int j = 0; i + j <= n; ++j) put(a + (b - a) * (Real(i) / Real(n)) + (c - a) * (Real(j) / Real(n)));
    }
}

// The entities joined below `link` (by joints whose b it is), and below them, that are no link of the chain.
void hanging(const SceneGraph& g, uint32_t link, const std::vector<uint32_t>& chainLinks, std::vector<uint32_t>& out) {
    for (const JointObject& j : g.joints)
        if (j.b == link && std::find(chainLinks.begin(), chainLinks.end(), j.a) == chainLinks.end() &&
            std::find(out.begin(), out.end(), j.a) == out.end()) {
            out.push_back(j.a);
            hanging(g, j.a, chainLinks, out);
        }
}

std::vector<ArmSphere> surfaceCover(const SceneGraph& g, const Entity& entity, const ArmPose& link, Real step) {
    TriMesh mesh = entityLocalMesh(resolveInstance(g, entity), g.baseDirectory);
    ArmPose at;
    worldPose(g, entity, at.position, at.turn);
    for (auto& p : mesh.positions) p = link.turn.conjugate().rotate(at.position + at.turn.rotate(p) - link.position);
    return armSurfaceCover(mesh, step);
}

// The fixed base: a fine cover for the arm's own collisions, a coarse one (`maskStep`) for the cameras' self mask.
void baseCollision(const SceneGraph& g, uint32_t id, Real step, Real maskStep, ArmModel& model, std::vector<uint32_t>& bodies) {
    const Entity* base = findEntity(g, id);
    if (!base) return; // A joint can be anchored directly to the world (id 0).
    model.collisionBodies.push_back({-1, surfaceCover(g, *base, model.base, step)});
    model.collisionEntities.push_back(id);
    bodies.push_back(id);
    std::vector<Vector3> points;
    surfacePoints(g, *base, model.base, points);
    model.fixedSpheres = armSpheres(points, maskStep);
}

void collisionPermissions(const SceneGraph& g, const std::vector<uint32_t>& bodies, ArmModel& model) {
    for (const auto& joint : g.joints) {
        if (joint.collide) continue;
        const auto a = std::find(bodies.begin(), bodies.end(), joint.a);
        const auto b = std::find(bodies.begin(), bodies.end(), joint.b);
        if (a != bodies.end() && b != bodies.end() && a != b)
            model.allowedCollisionPairs.push_back({size_t(a - bodies.begin()), size_t(b - bodies.begin())});
    }
}

} // namespace

ArmModel armModelOf(const SceneGraph& g, uint32_t effector, std::vector<uint32_t>* jointIds, Real step) {
    ArmModel m;
    // Own-body interfaces need finer geometry than a centimetre-scale external map.
    const Real selfStep = std::min(step, 0.01_r);
    const std::vector<IkLink> chain = ikChain(g, effector);
    std::vector<uint32_t> links, bodies;
    for (const IkLink& l : chain) links.push_back(findJoint(g, l.joint)->a);
    if (jointIds) jointIds->clear();
    for (size_t i = 0; i < chain.size(); ++i) {
        const JointObject& j = *findJoint(g, chain[i].joint);
        if (chain[i].dof != DofRx) return ArmModel(); // hinges only: a slider in the chain is not this planner's
        if (i == 0) {
            poseOf(g, j.b, m.base);
            baseCollision(g, j.b, selfStep, step, m, bodies);
        }
        ArmJoint a;
        const auto parent = std::find(links.begin(), links.end(), j.b);
        a.parent = parent == links.end() ? -1 : int(parent - links.begin());
        SceneObject turnA, turnB;
        turnA.rotationDeg = j.frameA, turnB.rotationDeg = j.frameB;
        a.originA = j.anchorA, a.originB = j.anchorB, a.turnA = objectRotation(turnA), a.turnB = objectRotation(turnB);
        const JointFreedom& f = j.dof[DofRx];
        if (f.motion == JointMotion::Limited) a.lower = std::max(f.lower, -kPi), a.upper = std::min(f.upper, kPi);
        ArmPose link;
        poseOf(g, j.a, link);
        std::vector<uint32_t> parts{j.a};
        hanging(g, j.a, links, parts);
        for (uint32_t id : parts) {
            const Entity* e = findEntity(g, id);
            if (!e || e->goalOf != 0) continue;
            std::vector<Vector3> points;
            surfacePoints(g, *e, link, points);
            const std::vector<ArmSphere> s = armSpheres(points, i + 1 == chain.size() ? step / 2 : step); // the hand finer: it passes slots
            a.spheres.insert(a.spheres.end(), s.begin(), s.end());
            m.collisionBodies.push_back({int(i), surfaceCover(g, *e, link, i + 1 == chain.size() ? selfStep / 2 : selfStep)});
            m.collisionEntities.push_back(id);
            bodies.push_back(id);
        }
        m.joints.push_back(a);
        if (jointIds) jointIds->push_back(j.id);
        if (i + 1 == chain.size()) { // the effector in the last link's frame: itself, or a hand welded on its flange
            ArmPose hand;
            poseOf(g, effector, hand);
            m.effector = {link.turn.conjugate().rotate(hand.position - link.position), (link.turn.conjugate() * hand.turn).normalized()};
        }
    }
    collisionPermissions(g, bodies, m);
    return m;
}

namespace {

// The joint whose moving link the entity is (null: none holds it).
const JointObject* heldBy(const SceneGraph& g, uint32_t entity) {
    for (const JointObject& j : g.joints)
        if (j.a == entity) return &j;
    return nullptr;
}

// A joint the chain to a camera may pass: a weld (nothing free) or a hinge (only the turn about x not locked).
bool weldOrHinge(const JointObject& j, bool& weld) {
    bool hinge = true;
    weld = true;
    for (int d = 0; d < 6; ++d) {
        const bool locked = j.dof[d].motion == JointMotion::Locked;
        weld = weld && locked, hinge = hinge && (d == DofRx || locked);
    }
    return weld || hinge;
}

} // namespace

bool carrierChain(const SceneGraph& g, uint32_t body, ArmModel& chain, std::vector<uint32_t>& jointIds, std::string& problem) {
    chain = ArmModel(), jointIds.clear();
    std::vector<const JointObject*> way;
    uint32_t root = body;
    for (const JointObject* j = heldBy(g, root); j; j = root ? heldBy(g, root) : nullptr) {
        if (way.size() >= 64) return problem = "the chain holding body " + std::to_string(body) + " does not end", false;
        way.insert(way.begin(), j), root = j->b;
    }
    if (way.empty()) return true; // nothing holds it: it stands where the scene puts it
    if (root != 0) { // the chain's first link hangs on a body: it must not move
        const Entity* e = findEntity(g, root);
        if (!e || !e->rigid.fixed) return problem = "the chain holding body " + std::to_string(body) + " starts on body " +
                                                    std::to_string(root) + ", which is not fixed", false;
        poseOf(g, root, chain.base);
    }
    for (size_t i = 0; i < way.size(); ++i) {
        const JointObject& j = *way[i];
        bool weld = false;
        if (!weldOrHinge(j, weld)) return problem = "joint " + std::to_string(j.id) + " on the way to body " +
                                                    std::to_string(body) + " is neither a weld nor a hinge", false;
        ArmJoint a;
        a.parent = int(i) - 1;
        SceneObject turnA, turnB;
        turnA.rotationDeg = j.frameA, turnB.rotationDeg = j.frameB;
        a.originA = j.anchorA, a.originB = j.anchorB, a.turnA = objectRotation(turnA), a.turnB = objectRotation(turnB);
        chain.joints.push_back(a);
        jointIds.push_back(weld ? 0 : j.id);
    }
    return true;
}

} // namespace ke
