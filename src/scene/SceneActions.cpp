// The hierarchy an action works on, the boxes it places things by, new objects (a shape, a light, a
// camera), showing everything and the roles (see SceneActions.h). The rules are an editor's: a new
// shape appears in the air over a free spot so ▶ drops it; a rigid body gets its collider
// and nothing else that could surprise.
#include "scene/SceneActions.h"
#include "core/UiLabel.h"

#include "math/ElementaryFunctions.h"

#include <algorithm>

namespace ke {

namespace {

const Real kSpawnHeight = 0.8_r; // a new shape's bottom over the floor
// Colours for new shapes, in turn, so neighbours differ at a glance.
const Vector3 kPalette[] = {{0.91_r, 0.50_r, 0.28_r}, {0.31_r, 0.59_r, 0.92_r}, {0.43_r, 0.77_r, 0.38_r}, {0.93_r, 0.77_r, 0.31_r},
                            {0.72_r, 0.47_r, 0.88_r}, {0.33_r, 0.80_r, 0.80_r}, {0.93_r, 0.42_r, 0.55_r}};
const char* kRoleWords[] = {"rigid", "collider"};

bool contains(const std::vector<uint32_t>& v, uint32_t id) { return std::find(v.begin(), v.end(), id) != v.end(); }

AABB entityBounds(const SceneGraph& g, const Entity& e) {
    const AABB local = entityLocalMesh(e, g.baseDirectory).bounds();
    const Matrix3x3 R = entityRotation(e).toMatrix3x3();
    AABB box;
    for (int k = 0; k < 8; ++k)
        box.expand(e.position + R * Vector3(k & 1 ? local.hi.x : local.lo.x, k & 2 ? local.hi.y : local.lo.y, k & 4 ? local.hi.z : local.lo.z));
    return box;
}

// A place on the floor no shape stands on (within 0.3 m): the 5 x 5 grid around the centre, in turn.
Vector3 freeSpot(const SceneGraph& g) {
    Vector3 p(0.0_r);
    for (int k = 0; k < 25; ++k) {
        p = Vector3(0.45_r * Real(k % 5 - 2), 0.0_r, 0.45_r * Real(k / 5 - 2));
        const bool taken = std::any_of(g.entities.begin(), g.entities.end(), [&](const Entity& o) {
            const Real dx = o.position.x - p.x, dz = o.position.z - p.z;
            return o.shape != ShapeKind::Plane && dx * dx + dz * dz < 0.3_r * 0.3_r;
        });
        if (!taken) break;
    }
    return p;
}

} // namespace

const char* roleWord(Role r) { return kRoleWords[int(r)]; }

bool roleOfWord(const std::string& word, Role& out) {
    for (int k = 0; k < int(Role::Count); ++k)
        if (word == kRoleWords[k]) return out = Role(k), true;
    return false;
}

bool hasRole(const Entity& e, Role r) { return r == Role::Rigid ? e.rigid.enabled : r == Role::Collider && e.collider.enabled; }

void setRole(Entity& e, Role r, bool on) { (r == Role::Rigid ? e.rigid.enabled : e.collider.enabled) = on; }

const char* shapeTitle(ShapeKind k) {
    static const char* names[] = {KE_UI_LABEL("Kinetica", "Куб"), KE_UI_LABEL("Kinetica", "Сфера"), KE_UI_LABEL("Kinetica", "Цилиндр"), KE_UI_LABEL("Kinetica", "Конус"), KE_UI_LABEL("Kinetica", "Плоскость"), KE_UI_LABEL("Kinetica", "Модель")};
    return names[std::clamp(int(k), 0, 5)];
}

const char* forceTitle(ForceShape shape, ForceMode mode) {
    static const char* names[] = {KE_UI_LABEL("Kinetica", "Толчок"), KE_UI_LABEL("Kinetica", "Тяга"), KE_UI_LABEL("Kinetica", "Взрыв"),
                                  KE_UI_LABEL("Kinetica", "Притяжение")};
    return names[int(shape) * 2 + int(mode)];
}

const char* lightTitle(LightKind k) {
    static const char* names[] = {KE_UI_LABEL("Kinetica", "Солнце"), KE_UI_LABEL("Kinetica", "Лампа"), KE_UI_LABEL("Kinetica", "Прожектор")};
    return names[std::clamp(int(k), 0, 2)];
}

std::vector<uint32_t> ancestry(const SceneGraph& g, uint32_t id) {
    std::vector<uint32_t> chain;
    for (const SceneObject* o = findObject(g, id); o && !contains(chain, o->id); o = findObject(g, o->parent)) chain.push_back(o->id);
    return chain;
}

bool isInside(const SceneGraph& g, uint32_t id, uint32_t ancestor) { return ancestor != 0 && contains(ancestry(g, id), ancestor); }

std::vector<uint32_t> childrenOf(const SceneGraph& g, uint32_t id) {
    std::vector<uint32_t> kids;
    auto add = [&](const auto& objects) {
        for (const auto& o : objects)
            if (o.parent == id) kids.push_back(o.id);
    };
    add(g.entities), add(g.groups), add(g.arrays), add(g.lights), add(g.cameras), add(g.forces);
    return kids;
}

std::vector<uint32_t> topmost(const SceneGraph& g, const std::vector<uint32_t>& ids) {
    std::vector<uint32_t> roots;
    for (uint32_t id : ids) {
        const SceneObject* o = findObject(g, id);
        if (!o || o->locked || contains(roots, id)) continue;
        const std::vector<uint32_t> chain = ancestry(g, id);
        if (std::none_of(chain.begin() + 1, chain.end(), [&](uint32_t up) { return contains(ids, up); })) roots.push_back(id);
    }
    return roots;
}

uint32_t masterOf(const SceneGraph& g, uint32_t entity) {
    std::vector<uint32_t> seen{entity};
    for (uint32_t id = entity;;) { // follow instanceOf to the end, as resolveInstance does (a cycle or a missing master ends it)
        const auto it = std::find_if(g.entities.begin(), g.entities.end(), [&](const Entity& e) { return e.id == id; });
        const uint32_t next = it != g.entities.end() ? it->instanceOf : 0;
        const bool known = std::any_of(g.entities.begin(), g.entities.end(), [&](const Entity& e) { return e.id == next; });
        if (next == 0 || contains(seen, next) || !known) return id;
        seen.push_back(next);
        id = next;
    }
}

uint32_t ownerOf(const SceneGraph& g, uint32_t drawnId) {
    if (!isArrayCopyId(drawnId)) return drawnId;
    for (const ArrayObject& a : g.arrays)
        if ((a.id & 0x7FFFu) == arrayOfCopyId(drawnId)) return a.id;
    return 0;
}

AABB objectBounds(const SceneGraph& g, uint32_t id, const std::function<AABB(const Entity&)>& entityBox) {
    AABB box;
    for (const Entity& w : worldEntities(g))
        if (w.visible && isInside(g, ownerOf(g, w.id), id)) box.expand(entityBox ? entityBox(w) : entityBounds(g, w));
    auto mark = [&](const SceneObject& o) { // a light, a camera or a force: about the size of its wireframe
        Vector3 p;
        Quaternion q;
        worldPose(g, o, p, q);
        if (effectivelyVisible(g, o) && isInside(g, o.id, id)) box.expand(AABB(p - Vector3(0.3_r), p + Vector3(0.3_r)));
    };
    for (const Light& l : g.lights) mark(l);
    for (const Camera& c : g.cameras) mark(c);
    for (const Force& f : g.forces) mark(f);
    return box;
}

AABB sceneBounds(const SceneGraph& g) {
    AABB box;
    for (const Entity& w : worldEntities(g))
        if (w.visible && !w.locked) box.expand(entityBounds(g, w));
    return box;
}

uint32_t addShape(SceneGraph& g, ShapeKind shape) {
    Entity e;
    e.id = nextId(g);
    e.shape = shape;
    const auto same = std::count_if(g.entities.begin(), g.entities.end(), [&](const Entity& o) { return o.shape == shape; });
    e.name = std::string(shapeTitle(shape)) + " " + std::to_string(same + 1);
    e.color = kPalette[g.entities.size() % 7];
    if (shape == ShapeKind::Plane) {
        e.size = Vector3(3.0_r, 0.02_r, 3.0_r);
        e.position = Vector3(0.0_r, 0.03_r, 0.0_r); // just above the floor
        e.color = Vector3(0.62_r, 0.64_r, 0.68_r);
    } else {
        e.position = freeSpot(g);
        e.position.y = kSpawnHeight + 0.5_r * e.size.y; // in the air: given a body and ▶, it falls
    }
    g.entities.push_back(e);
    return e.id;
}

uint32_t addLight(SceneGraph& g, LightKind kind, const Vector3& at) {
    Light l;
    l.id = nextId(g);
    l.kind = kind;
    const auto same = std::count_if(g.lights.begin(), g.lights.end(), [&](const Light& o) { return o.kind == kind; });
    l.name = std::string(lightTitle(kind)) + " " + std::to_string(same + 1);
    l.color = kind == LightKind::Sun ? Vector3(1.0_r, 0.96_r, 0.9_r) : Vector3(1.0_r, 0.9_r, 0.76_r);
    Vector3 dir(0.0_r, -1.0_r, 0.0_r);
    if (kind == LightKind::Sun) {
        dir = normalize(Vector3(-0.3_r, -0.8_r, -0.5_r));
        l.position = at - dir * 2.2_r;
        l.shadows = true;
    } else if (kind == LightKind::Point) {
        l.position = at + Vector3(0.0_r, 1.5_r, 0.0_r);
        l.range = 4.0_r;
    } else {
        l.position = at + Vector3(0.9_r, 1.6_r, 0.9_r);
        dir = normalize(at - l.position);
        l.range = 6.0_r;
    }
    l.rotationDeg = eulerDegrees(Quaternion::fromTwoVectors(Vector3(0.0_r, -1.0_r, 0.0_r), dir));
    g.lights.push_back(l);
    return l.id;
}

// As drawn with the mouse: the light stands at `position` and shines at `target` (a lamp ignores where; its
// range and a spot's reach past the target).
uint32_t placeLight(SceneGraph& g, LightKind kind, const Vector3& position, const Vector3& target) {
    const uint32_t id = addLight(g, kind, target);
    Light& l = g.lights.back();
    const Vector3 to = target - position;
    const Real far = length(to);
    l.position = position;
    if (kind != LightKind::Point && far > 1e-4_r)
        l.rotationDeg = eulerDegrees(Quaternion::fromTwoVectors(Vector3(0.0_r, -1.0_r, 0.0_r), to / far));
    if (kind != LightKind::Sun) l.range = std::max(l.range, 1.5_r * far);
    return id;
}

uint32_t addForce(SceneGraph& g, ForceShape shape, ForceMode mode, const Vector3& at) {
    Force f;
    f.id = nextId(g);
    f.shape = shape;
    f.mode = mode;
    const auto same = std::count_if(g.forces.begin(), g.forces.end(), [&](const Force& o) { return o.shape == shape && o.mode == mode; });
    f.name = std::string(forceTitle(shape, mode)) + " " + std::to_string(same + 1);
    f.color = Vector3(0.45_r, 0.85_r, 0.55_r);
    f.position = at;
    if (shape == ForceShape::Radial) f.radius = mode == ForceMode::Impulse ? 3.0_r : 5.0_r, f.falloff = ForceFalloff::Linear;
    else f.radius = 1.5_r; // it pushes what stands near it: placed at a body, it is that body's push (radius 0: every body)
    f.strength = shape == ForceShape::Directional ? (mode == ForceMode::Impulse ? 5.0_r : 10.0_r) : (mode == ForceMode::Impulse ? 10.0_r : -10.0_r);
    g.forces.push_back(f);
    return f.id;
}

uint32_t addCamera(SceneGraph& g, const Vector3& eye, const Vector3& centre) {
    Camera c;
    c.id = nextId(g);
    c.name = "Камера " + std::to_string(g.cameras.size() + 1);
    c.color = Vector3(0.8_r, 0.84_r, 0.9_r);
    c.position = eye;
    // Turned to look along f with its up as upright as it can be: Ry Rx of objectRotation, no roll
    // (forward = (-cos a sin b, sin a, -cos a cos b) for the angles a about x, b about y).
    const Vector3 f = normalize(centre - eye);
    c.rotationDeg = Vector3(ke::asin(std::clamp(f.y, -1.0_r, 1.0_r)), ke::atan2(-f.x, -f.z), 0.0_r) * (180.0_r / kPi);
    c.active = g.cameras.empty(); // the first camera is the scene's camera
    g.cameras.push_back(c);
    return c.id;
}

static Camera* cameraIn(SceneGraph& g, uint32_t id) {
    for (Camera& c : g.cameras)
        if (c.id == id && id != 0) return &c;
    return nullptr;
}

// The camera's own target, made if it has none: a see-through cube 8 cm across in the camera's colour.
static Entity& ownTarget(SceneGraph& g, Camera& c, const Vector3& at) {
    for (Entity& e : g.entities)
        if (e.targetOf == c.id) return e;
    Entity e;
    e.id = nextId(g);
    e.name = "Цель «" + c.name + "»";
    e.shape = ShapeKind::Box;
    e.size = Vector3(0.08_r);
    e.position = at;
    e.color = c.color;
    e.targetOf = c.id;
    e.rigid.enabled = e.collider.enabled = false;
    g.entities.push_back(e);
    return g.entities.back();
}

bool aimCamera(SceneGraph& g, uint32_t camera, uint32_t target, const Vector3* point) {
    Camera* c = cameraIn(g, camera);
    if (!c || (target && (target == camera || !findObject(g, target)))) return false;
    if (!target) { // its own target: where it is told, else where it looks now
        const Vector3 at = point ? *point : cameraTarget(g, *c);
        Entity& own = ownTarget(g, *c, at);
        setWorldPose(g, own, at, Quaternion());
        target = own.id;
    }
    std::vector<uint32_t> stale; // another object aimed at: its own target goes
    for (const Entity& e : g.entities)
        if (e.targetOf == camera && e.id != target) stale.push_back(e.id);
    deleteObjects(g, stale);
    c = cameraIn(g, camera);
    c->kind = CameraKind::Target;
    c->target = target;
    Vector3 eye;
    Quaternion turn;
    worldPose(g, *c, eye, turn);
    setWorldPose(g, *c, eye, lookTurn(eye, cameraTarget(g, *c)));
    return true;
}

bool freeCamera(SceneGraph& g, uint32_t camera) {
    Camera* c = cameraIn(g, camera);
    if (!c) return false;
    Vector3 eye, forward, up;
    cameraFrame(g, *c, eye, forward, up);
    c->kind = CameraKind::Free;
    c->target = 0;
    setWorldPose(g, *c, eye, lookTurn(eye, eye + forward));
    std::vector<uint32_t> own; // its own target goes with it (a target camera made free loses its target)
    for (const Entity& e : g.entities)
        if (e.targetOf == camera) own.push_back(e.id);
    deleteObjects(g, own);
    return true;
}

void showAll(SceneGraph& g) {
    for (Entity& e : g.entities) {
        const bool hiddenTemplate = !e.visible && std::any_of(g.arrays.begin(), g.arrays.end(), [&](const ArrayObject& a) { return a.templateId == e.id; });
        if (!hiddenTemplate) e.visible = true;
    }
    for (Group& o : g.groups) o.visible = true;
    for (ArrayObject& o : g.arrays) o.visible = true;
    for (Light& o : g.lights) o.visible = true;
    for (Camera& o : g.cameras) o.visible = true;
    for (Force& o : g.forces) o.visible = true;
}

namespace {

// A collider pairs with a rigid body or stands alone (a fixed obstacle): a rigid body gets one.
// What was added besides, in words.
std::string keepComponentsConsistent(Entity& e, Role role) {
    if (role != Role::Rigid || e.collider.enabled) return {};
    e.collider.enabled = true;
    e.collider.kind = ColliderKind::Auto;
    return KE_UI_LABEL("Kinetica", "Добавлен коллайдер «Авто»: тело сталкивается своей формой");
}

} // namespace

std::string setRoles(SceneGraph& g, const std::vector<uint32_t>& entities, Role role, bool on) {
    std::vector<uint32_t> masters;
    for (uint32_t id : entities)
        if (!contains(masters, masterOf(g, id))) masters.push_back(masterOf(g, id));
    std::string notes;
    for (uint32_t id : masters)
        for (Entity& e : g.entities) {
            if (e.id != id) continue;
            setRole(e, role, on);
            const std::string note = on ? keepComponentsConsistent(e, role) : std::string();
            if (!note.empty() && notes.find(note) == std::string::npos) notes += (notes.empty() ? "" : "; ") + note;
        }
    return notes;
}

} // namespace ke
