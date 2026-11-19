// An editor's actions as the core of the API (scene/SceneActions.h): what a button does to the scene
// graph, checked as numbers - names and places of new things, roles and what they switch on, groups
// that keep world poses, a delete that takes what cannot live without the object, copies that carry
// their sensors, and the gizmo's move, turn and scale.
#include "TestRunner.h"

#include "scene/Animation.h"
#include "scene/Assembly.h"
#include "scene/SceneActions.h"
#include "scene/SceneOps.h"

#include <algorithm>
#include <cmath>

using namespace ke;

namespace {

Vector3 worldAt(const SceneGraph& g, uint32_t id) {
    Vector3 p;
    Quaternion q;
    worldPose(g, *findObject(g, id), p, q);
    return p;
}

Real gap(const Vector3& a, const Vector3& b) { return length(a - b); }
bool same(const Vector3& a, const Vector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; } // to the bit

const Entity& entity(const SceneGraph& g, uint32_t id) { return *static_cast<const Entity*>(findObject(g, id)); }

} // namespace

// A group stands at the centre of its members' boxes and they keep their world poses (to rounding);
// ungrouped, they stand where they stood.
void testActionsGroupKeepsPoses() {
    SceneGraph g;
    const uint32_t a = addShape(g, ShapeKind::Box), b = addShape(g, ShapeKind::Sphere);
    for (Entity& e : g.entities)
        if (e.id == b) e.rotationDeg = Vector3(0, 30, 10);
    const Vector3 pa = worldAt(g, a), pb = worldAt(g, b);
    AABB box = objectBounds(g, a);
    box.expand(objectBounds(g, b));
    const uint32_t group = groupObjects(g, topmost(g, {a, b}));
    CHECK(group != 0 && gap(worldAt(g, group), box.center()) < 1e-6_r, "the group at the centre of the boxes");
    CHECK(gap(worldAt(g, a), pa) < 1e-6_r && gap(worldAt(g, b), pb) < 1e-6_r && findObject(g, a)->parent == group,
          "members keep their world places: %.2e, %.2e", double(gap(worldAt(g, a), pa)), double(gap(worldAt(g, b), pb)));
    CHECK(topmost(g, {a, group}) == std::vector<uint32_t>{group}, "a member of a selected group goes with its group");
    const std::vector<uint32_t> freed = ungroupObjects(g, {group});
    CHECK(freed.size() == 2 && !findObject(g, group) && findObject(g, a)->parent == 0 && gap(worldAt(g, b), pb) < 1e-6_r, "ungroup gives them back");
}

// Deleting a group takes its member, the member's sensor and the joint it held; the other link stays.
// A duplicate carries the body's sensor; a sensor's own copy stays on its mount.
void testActionsDeleteAndDuplicate() {
    SceneGraph g;
    const uint32_t a = addShape(g, ShapeKind::Box), b = addShape(g, ShapeKind::Box);
    const uint32_t joint = addJoint(g, JointType::Hinge, a, b, worldAt(g, a), Vector3(0, 0, 1));
    const uint32_t sonar = attachSensor(g, a, snapToFace(g, a, BoxFace::PlusX, Vector3(0.02_r)), SensorKind::Sonar);
    const uint32_t group = groupObjects(g, {a});
    const SceneGraph before = g;
    const std::vector<uint32_t> copies = duplicateObjects(g, {a});
    const Entity& copy = entity(g, copies[0]);
    CHECK(copy.name == "Куб 1 копия" && worldAt(g, copies[0]).y > worldAt(g, a).y + 0.2_r && g.sensors.size() == 2 && g.sensors[1].parent == copies[0],
          "a copy above the original carries its sensor");
    const std::vector<uint32_t> sensorCopy = duplicateObjects(g, {sonar});
    CHECK(gap(worldAt(g, sensorCopy[0]), worldAt(g, sonar)) == 0 && findObject(g, sensorCopy[0])->parent == a, "a sensor's copy stays on its mount");
    g = before;
    deleteObjects(g, {group});
    CHECK(!findObject(g, a) && !findObject(g, sonar) && !findJoint(g, joint) && findObject(g, b), "the group, its member, the sensor and the joint go; the other link stays");
    CHECK(joint != 0 && sonar != 0, "the scene had a joint and a sensor to lose");
}

// The gizmo's effect: a move adds the step to the world position exactly and keeps the typed angles;
// a quarter turn about y carries the object around the pivot; a scale of a group scales the members'
// sizes and their places in it.
void testActionsTransforms() {
    SceneGraph g;
    const uint32_t a = addShape(g, ShapeKind::Box), b = addShape(g, ShapeKind::Box);
    for (Entity& e : g.entities)
        if (e.id == a) e.rotationDeg = Vector3(0, 37, 0);
    const SceneGraph start = g;
    const Vector3 by(0.4_r, 0.0_r, -0.1_r);
    moveObjects(g, start, {a}, by);
    CHECK(same(entity(g, a).position, entity(start, a).position + by) && same(entity(g, a).rotationDeg, entity(start, a).rotationDeg), "a move: exact, angles kept");
    g = start;
    const Vector3 pivot = worldAt(g, b);
    turnObjects(g, start, {a}, pivot, Quaternion::fromAxisAngle(Vector3(0, 1, 0), 0.5_r * kPi));
    const Vector3 r = worldAt(start, a) - pivot, want = pivot + Vector3(r.z, r.y, -r.x);
    CHECK(gap(worldAt(g, a), want) < 1e-5_r, "a quarter turn about y carries it around the pivot (%.2e m off)", double(gap(worldAt(g, a), want)));
    g = start;
    const uint32_t group = groupObjects(g, {a, b});
    const SceneGraph grouped = g;
    scaleObjects(g, grouped, {group}, Vector3(2.0_r));
    CHECK(same(entity(g, a).size, entity(grouped, a).size * 2.0_r) && same(entity(g, b).position, entity(grouped, b).position * 2.0_r),
          "a group scaled twice: sizes and places in it doubled");
    const std::vector<uint32_t> arrays = arrayObjects(g, {b});
    CHECK(arrays.size() == 1 && !entity(g, b).visible && g.arrays[0].count[0] == 5 &&
              std::fabs(g.arrays[0].step.x - (objectBounds(grouped, b).extent().x * 2.0_r + 0.05_r)) < 1e-5_r,
          "an array of 5, the shape its hidden template, a little more than its width apart");
}

// The core's operations by name (scene/SceneOps.h): each is a question or an edit, never both, with its
// help and arguments; an action answers what a window selects after it; a batch is all or nothing; an
// action without its objects says so instead of guessing.
void testSceneOpsRegistry() {
    int malformed = 0;
    for (const SceneOp& op : sceneOps()) malformed += op.name.empty() || op.help.empty() || op.args.empty() || bool(op.query) == bool(op.edit);
    CHECK(malformed == 0 && findSceneOp("scene_add") && findSceneOp("assembly_preset") && !findSceneOp("sim_step"),
          "%d operations, each a question or an edit with its help; time stays with the clients", int(sceneOps().size()));
    SceneGraph g;
    Value args = Value::map(), answer = Value::map();
    std::string error;
    args.set("shape", "sphere");
    CHECK(findSceneOp("scene_add")->edit(g, args, answer, error) && answer["ids"].size() == 1 && answer["id"].integer() == int(g.entities[0].id),
          "scene_add answers the new sphere as what to select");
    Value list = Value::map();
    findSceneOp("scene_list")->query(g, Value::map(), list, error);
    CHECK(list["objects"].size() == 1 && list["objects"].items()[0]["kind"].text() == "entity" && list["objects"].items()[0]["name"].text() == "Сфера 1",
          "scene_list: the sphere, its kind and name");
    Value batch = Value::map(), steps = Value::list(), add = Value::map(), role = Value::map(), roleArgs = Value::map();
    add.set("op", "scene_add"), add.set("args", Value::map());
    roleArgs.set("ids", idsValue({g.entities[0].id})), roleArgs.set("role", "teapot");
    role.set("op", "scene_role"), role.set("args", roleArgs);
    steps.push(add), steps.push(role), batch.set("ops", steps);
    const SceneGraph before = g;
    const bool done = findSceneOp("scene_batch")->edit(g, batch, answer, error);
    CHECK(!done && error.find("scene_role") != std::string::npos, "a batch stops at its broken step and says which: %s", error.c_str());
    CHECK(g.entities.size() == before.entities.size() + 1, "the batch's earlier steps are left for the client to undo (it keeps the graph from before)");
    Value turn = Value::map();
    turn.set("axis", "y"), turn.set("degrees", 90.0);
    CHECK(!findSceneOp("scene_turn")->edit(g, turn, answer, error) && error.find("ids") != std::string::npos, "a turn without ids says so: %s", error.c_str());
    // A spot and a camera placed as the mouse makes them: where they stand, where they look.
    const auto point = [](double x, double y, double z) {
        Value v = Value::list();
        v.push(x), v.push(y), v.push(z);
        return v;
    };
    Value spot = Value::map(), camera = Value::map();
    spot.set("kind", "spot"), spot.set("position", point(1, 2, 0)), spot.set("target", point(1, 0, 2));
    camera.set("eye", point(0, 1, 3)), camera.set("target", point(0, 1, 0));
    const bool placed = findSceneOp("scene_add_light")->edit(g, spot, answer, error) && findSceneOp("scene_add_camera")->edit(g, camera, answer, error);
    const Vector3 shine = objectRotation(g.lights.back()).rotate(Vector3(0.0_r, -1.0_r, 0.0_r));
    const Vector3 look = objectRotation(g.cameras.back()).rotate(Vector3(0.0_r, 0.0_r, -1.0_r));
    CHECK(placed && length(g.lights.back().position - Vector3(1.0_r, 2.0_r, 0.0_r)) < 1e-5_r && dot(shine, normalize(Vector3(0.0_r, -1.0_r, 1.0_r))) > 0.9999_r &&
              length(g.cameras.back().position - Vector3(0.0_r, 1.0_r, 3.0_r)) < 1e-5_r && dot(look, Vector3(0.0_r, 0.0_r, -1.0_r)) > 0.9999_r,
          "scene_add_light with position and target shines there; scene_add_camera with eye and target looks there");
}

// A target camera (3ds Max's Target camera) always looks at its target. Aimed at a cube, it looks at the cube; the cube
// moved, it turns after it; the camera moved, it still looks at it. Set free, it keeps looking where it looked. Its
// target comes back from the file to the bit, a free camera's file has no target line. scene_add_camera makes one aimed
// at an object; camera_aim refuses a camera or an object that is not there.
void testTargetCamera() {
    SceneGraph g;
    const uint32_t cube = addShape(g, ShapeKind::Box);
    g.entities.back().position = Vector3(1, 0.5_r, -2);
    const uint32_t cam = addCamera(g, Vector3(3, 2, 3), Vector3(0.0_r));
    const auto looksAt = [&](const Vector3& point) {
        const Camera& c = *std::find_if(g.cameras.begin(), g.cameras.end(), [&](const Camera& x) { return x.id == cam; });
        Vector3 eye, forward, up;
        cameraFrame(g, c, eye, forward, up);
        return length(forward - normalize(point - eye)) < 1e-5_r && std::fabs(dot(forward, up)) < 1e-5_r && up.y > 0;
    };
    const bool aimed = aimCamera(g, cam, cube, nullptr) && looksAt(Vector3(1, 0.5_r, -2));
    g.entities.back().position = Vector3(-1, 0.2_r, 1);
    const bool follows = looksAt(Vector3(-1, 0.2_r, 1));
    std::find_if(g.cameras.begin(), g.cameras.end(), [&](const Camera& x) { return x.id == cam; })->position = Vector3(-3, 1, -2);
    const bool keeps = looksAt(Vector3(-1, 0.2_r, 1));
    SceneGraph back;
    std::string error;
    const std::string text = g.save();
    const bool file = back.load(text, error) && back.save() == text && back.cameras[0].kind == CameraKind::Target && back.cameras[0].target == cube;
    const bool freed = freeCamera(g, cam) && looksAt(Vector3(-1, 0.2_r, 1)) && g.save().find("  target ") == std::string::npos;
    std::printf("  a target camera on the cube: looks at it %s, after it moved %s, from where the camera moved %s; its file to the bit %s; "
                "free again, still looking there %s\n", aimed ? "yes" : "no", follows ? "yes" : "no", keeps ? "yes" : "no", file ? "yes" : "no", freed ? "yes" : "no");
    CHECK(aimed && follows && keeps, "a target camera does not keep looking at its cube");
    CHECK(file && freed, "a target camera's file, or set free it jumped: %s", error.c_str());

    // Its own target: a cube at the point, a handle (no roles); moved, the camera turns after it; set free, the cube goes;
    // the camera deleted, its cube goes with it.
    const Vector3 spot(0.5_r, 0.2_r, 0.5_r);
    const bool own = aimCamera(g, cam, 0, &spot) && looksAt(spot);
    const auto ownCube = std::find_if(g.entities.begin(), g.entities.end(), [&](const Entity& e) { return e.targetOf == cam; });
    const bool handle = ownCube != g.entities.end() && isHandle(*ownCube) && !ownCube->rigid.enabled && !ownCube->collider.enabled;
    if (handle) ownCube->position = Vector3(2, 0.1_r, -1);
    const bool turned = handle && looksAt(Vector3(2, 0.1_r, -1));
    const size_t before = g.entities.size();
    freeCamera(g, cam);
    const bool cubeGone = g.entities.size() + 1 == before;
    aimCamera(g, cam, 0, &spot);
    deleteObjects(g, {cam});
    const bool withCamera = std::none_of(g.entities.begin(), g.entities.end(), [&](const Entity& e) { return e.targetOf == cam; });
    std::printf("  its own target: a cube at the point %s, a handle %s, moved and followed %s; set free the cube goes %s; with the camera %s\n",
                own ? "yes" : "no", handle ? "yes" : "no", turned ? "yes" : "no", cubeGone ? "yes" : "no", withCamera ? "yes" : "no");
    CHECK(own && handle && turned && cubeGone && withCamera, "a camera's own target cube");

    Value add = Value::map(), answer = Value::map(), at = Value::list(), aim = Value::map();
    add.set("kind", "target"), add.set("at", int(cube));
    const bool made = findSceneOp("scene_add_camera")->edit(g, add, answer, error) && g.cameras.back().kind == CameraKind::Target &&
                      g.cameras.back().target == cube;
    aim.set("camera", 999), aim.set("at", int(cube));
    const bool noCamera = !findSceneOp("camera_aim")->edit(g, aim, answer, error) && error.find("999") != std::string::npos;
    aim.set("camera", int(g.cameras.back().id)), aim.set("at", 777);
    const bool noObject = !findSceneOp("camera_aim")->edit(g, aim, answer, error) && error.find("777") != std::string::npos;
    CHECK(made && noCamera && noObject, "scene_add_camera kind target, or camera_aim's refusals: %s", error.c_str());
}

// The Sequencer's clip (scene/Animation.h). A cube keyed at 0 s at the origin, unturned, and at 2 s 1 m along x turned
// 90 degrees about y: at 1 s it is half way, 0.5 m, turned exactly 45 degrees; before 0 s and after 2 s it holds; looped,
// 3 s is 1 s. A key at the same time replaces; the clip comes back from the file to the bit; the playhead (anim_seek)
// puts the cube where the clip has it; deleting the cube takes its track; anim_key refuses an object not there.
void testAnimationClip() {
    SceneGraph g;
    const uint32_t cube = addShape(g, ShapeKind::Box);
    const auto entity = [&]() -> Entity& { return *std::find_if(g.entities.begin(), g.entities.end(), [&](const Entity& e) { return e.id == cube; }); };
    entity().position = Vector3(0.0_r), entity().rotationDeg = Vector3(0.0_r);
    keyObject(g, cube, 0);
    entity().position = Vector3(1, 0, 0), entity().rotationDeg = Vector3(0, 90, 0);
    keyObject(g, cube, 2);
    const AnimTrack& t = *trackOf(g, cube);
    Vector3 p;
    Quaternion q;
    samplePose(t, 1, false, 2, p, q);
    const Real angle = 2 * std::acos(std::min(1.0_r, std::fabs(q.w))) * 180 / kPi;
    const bool middle = std::fabs(p.x - 0.5_r) < 1e-6_r && std::fabs(angle - 45) < 1e-3_r;
    samplePose(t, -1, false, 2, p, q);
    const bool before = length(p) < 1e-9_r;
    samplePose(t, 5, false, 2, p, q);
    const bool after = std::fabs(p.x - 1) < 1e-9_r;
    samplePose(t, 3, true, 2, p, q);
    const bool looped = std::fabs(p.x - 0.5_r) < 1e-6_r;
    entity().position = Vector3(2, 0, 0);
    keyObject(g, cube, 2); // the same time: replaced, not a second key
    const bool replaced = trackOf(g, cube)->keys.size() == 2 && trackOf(g, cube)->keys.back().position.x == 2;
    SceneGraph back;
    std::string error;
    const std::string text = g.save();
    const bool file = back.load(text, error) && back.save() == text && back.animation.tracks.size() == 1 && back.animation.tracks[0].keys.size() == 2;
    Value seek = Value::map(), answer = Value::map();
    seek.set("time", 1.0);
    const bool seeked = findSceneOp("anim_seek")->edit(g, seek, answer, error) && std::fabs(entity().position.x - 1) < 1e-6_r && g.animation.time == 1;
    Value key = Value::map();
    key.set("id", 999);
    const bool refused = !findSceneOp("anim_key")->edit(g, key, answer, error) && error.find("999") != std::string::npos;
    deleteObjects(g, {cube});
    const bool gone = g.animation.tracks.empty();
    std::printf("  a cube keyed at 0 s and 2 s (1 m, 90 deg): at 1 s %.4f m turned %.4f deg; held before %s and after %s; looped %s; a key replaced %s; "
                "the file to the bit %s; anim_seek %s; refused %s; deleted, its track gone %s\n", double(p.x), double(angle), before ? "yes" : "no",
                after ? "yes" : "no", looped ? "yes" : "no", replaced ? "yes" : "no", file ? "yes" : "no", seeked ? "yes" : "no", refused ? "yes" : "no",
                gone ? "yes" : "no");
    CHECK(middle && before && after && looped, "the clip's poses between, before, after and looped");
    CHECK(replaced && file && seeked && refused && gone, "keys, the file, the playhead, the refusal, the track of a deleted object: %s", error.c_str());
}
