// The shape of a scene-graph entity as a triangle mesh: the primitives (box, sphere, cylinder, cone,
// plane) built from the entity's size, and models read from OBJ/STL files. The physics engine builds
// its colliders from these meshes (src/physics) and an editor draws the same meshes, so what is seen is
// what is simulated. Model files are cached: a scene is rebuilt at every edit and on every Play, and a
// model must be read only once.
#include "scene/SceneGraph.h"

#include "core/Format.h"

#include <filesystem>
#include <map>
#include <mutex>

namespace ke {

namespace {

// A relative model path is found from the scene file's folder; an absolute one is used as it is.
std::string resolvePath(const std::string& file, const std::string& baseDirectory) {
    const bool absolute = !file.empty() && (file[0] == '/' || file[0] == '\\' || (file.size() > 1 && file[1] == ':'));
    if (absolute || baseDirectory.empty()) return file;
    const char last = baseDirectory.back();
    return baseDirectory + (last == '/' || last == '\\' ? "" : "/") + file;
}

// The editor (drawing) and the simulation thread (building) read the caches at the same time.
std::mutex& cacheMutex() {
    static std::mutex m;
    return m;
}

// A model file as read from disk, once per path. An unreadable file is remembered as its error.
struct ModelFile {
    std::shared_ptr<const TriMesh> mesh;
    std::string error;
};

const ModelFile& modelFile(const std::string& path) {
    static std::map<std::string, ModelFile> cache;
    std::lock_guard<std::mutex> lock(cacheMutex());
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    ModelFile f;
    auto mesh = std::make_shared<TriMesh>();
    if (loadMesh(path, *mesh, f.error) && !mesh->empty()) f.mesh = mesh;
    else if (f.error.empty()) f.error = "the file holds no triangles";
    return cache.emplace(path, f).first->second;
}

// The primitive shapes, centred on the origin: size is the full size, round shapes take the
// diameter from size.x and the height (along y) from size.y.
TriMesh primitiveMesh(ShapeKind shape, const Vector3& s) {
    TriMesh m;
    switch (shape) {
    case ShapeKind::Box: m = primitives::box(s * 0.5_r); break;
    case ShapeKind::Plane: m = primitives::box(Vector3(0.5_r * s.x, 0.01_r, 0.5_r * s.z)); break;
    case ShapeKind::Sphere: m = primitives::sphere(0.5_r * s.x, 24, 12); break;
    case ShapeKind::Cylinder: // the primitive's axis is z: turn it to y
        m = primitives::cylinder(0.5_r * s.x, s.y, 24);
        m.transform(Quaternion::fromAxisAngle({1, 0, 0}, -0.5_r * kPi).toMatrix3x3(), Vector3(1.0_r), Vector3(0.0_r));
        break;
    case ShapeKind::Cone: // the primitive's apex points to -x: turn it up (+y)
        m = primitives::cone(0.5_r * s.x, s.y, 24);
        m.transform(Quaternion::fromAxisAngle({0, 0, 1}, -0.5_r * kPi).toMatrix3x3(), Vector3(1.0_r), Vector3(0.0_r));
        break;
    case ShapeKind::Mesh: break; // read from a file: see entityLocalMesh
    }
    m.translate(-m.bounds().center()); // centred on the entity's position
    return m;
}

} // namespace

Quaternion entityRotation(const Entity& e) {
    const Vector3& d = e.rotationDeg;
    return Quaternion::fromAxisAngle({0, 1, 0}, degToRad(d.y)) * Quaternion::fromAxisAngle({1, 0, 0}, degToRad(d.x)) *
           Quaternion::fromAxisAngle({0, 0, 1}, degToRad(d.z));
}

namespace {

// The primitive at its size, or the model fitted to it.
TriMesh baseMesh(const Entity& e, const std::string& baseDirectory, std::string* error) {
    if (e.shape != ShapeKind::Mesh) return primitiveMesh(e.shape, e.size);
    const ModelFile& file = modelFile(resolvePath(e.meshFile, baseDirectory));
    if (!file.mesh) {
        if (error) *error = e.meshFile + ": " + file.error;
        return TriMesh();
    }
    TriMesh m = *file.mesh;
    m.fitTo(Vector3(0.0_r), maxComp(e.size)); // uniform scale: the model keeps its proportions
    return m;
}

} // namespace

// A relative path, read from `from`, written for a file in `to`: lexically, through absolute folders (no file is touched);
// on another root (another drive) the absolute path.
static std::string rebased(const std::string& file, const std::string& from, const std::string& to) {
    namespace fs = std::filesystem;
    const bool absolute = !file.empty() && (file[0] == '/' || file[0] == '\\' || (file.size() > 1 && file[1] == ':'));
    if (file.empty() || absolute || from.empty()) return file;
    const fs::path whole = (fs::absolute(fs::u8path(from)) / fs::u8path(file)).lexically_normal();
    if (to.empty()) return whole.generic_u8string(); // into a scene of no folder yet (never saved): the file by its whole path
    const fs::path there = whole.lexically_relative(fs::absolute(fs::u8path(to)).lexically_normal());
    return (there.empty() ? whole : there).generic_u8string();
}

void rebaseFiles(SceneGraph& g, const std::string& directory) {
    for (Entity& e : g.entities) {
        e.meshFile = rebased(e.meshFile, g.baseDirectory, directory);
        e.texture.file = rebased(e.texture.file, g.baseDirectory, directory);
        e.texture.heightFile = rebased(e.texture.heightFile, g.baseDirectory, directory);
        e.texture.normalFile = rebased(e.texture.normalFile, g.baseDirectory, directory);
    }
    g.baseDirectory = directory;
}

TriMesh entityLocalMesh(const Entity& e, const std::string& baseDirectory, std::string* error) {
    return baseMesh(e, baseDirectory, error);
}

bool entityIsGeometryOnly(const Entity& e) {
    return !e.rigid.enabled && !e.collider.enabled;
}

Real bodyMass(const Entity& e) {
    static const Real share[] = {1.0_r, kPi / 6, kPi / 4, kPi / 12, 1.0_r, 1.0_r}; // box, sphere, cylinder, cone, plane, mesh
    return e.rigid.mass > 0 ? e.rigid.mass : e.rigid.density * share[int(e.shape)] * e.size.x * e.size.y * e.size.z;
}

std::string entityGeometryKey(const Entity& e, const std::string& baseDirectory) {
    std::string key = std::to_string(int(e.shape)) + "|" + (e.shape == ShapeKind::Mesh ? resolvePath(e.meshFile, baseDirectory) : "");
    for (int k = 0; k < (e.shape == ShapeKind::Mesh ? 1 : 3); ++k) key += "|" + numberText(e.shape == ShapeKind::Mesh ? maxComp(e.size) : e.size[k]);
    return key;
}

} // namespace ke
