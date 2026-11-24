#pragma once
// Bodies for a test's scene, shared by the files of the running world's tests (SimTests.cpp, ForceTests.cpp).
#include "scene/SceneGraph.h"

namespace ke {

// A box of `size` at `at`: a rigid body with its collider, fixed or moving.
inline Entity solid(uint32_t id, const char* name, const Vector3& size, const Vector3& at, bool fixed) {
    Entity e;
    e.id = id, e.name = name, e.size = size, e.position = at;
    e.rigid.enabled = e.collider.enabled = true;
    e.rigid.fixed = fixed;
    return e;
}

// A fixed post and a horizontal arm 0.4 m long on a hinge about z at the post's top, the arm's centre
// 0.2 m out along +x: its turn about z is the joint's coordinate (frames with x on the world's z).
inline SceneGraph armOnPost(const JointDrive& drive) {
    SceneGraph g;
    g.world.physics.sleeping = false;
    g.entities.push_back(solid(1, "Стойка", {0.1_r, 1, 0.1_r}, {0, 0.5_r, 0}, true));
    g.entities.push_back(solid(2, "Рука", {0.4_r, 0.04_r, 0.04_r}, {0.2_r, 1.05_r, 0}, false));
    JointObject j;
    j.id = 3, j.name = "Плечо", j.a = 2, j.b = 1;
    j.anchorA = {-0.2_r, 0, 0}, j.anchorB = {0, 0.55_r, 0};
    j.frameA = j.frameB = {0, -90, 0}; // x onto the world's z
    j.dof[DofRx].motion = JointMotion::Free;
    j.dof[DofRx].drive = drive;
    g.joints.push_back(j);
    return g;
}

} // namespace ke
