// The physics engines built in: a row each - the word a scene names it with (the physics line's engine) and its factory,
// defined in its own folder (plugins/physics/<engine>, the only place that includes it). The first row is the default:
// a scene that names no engine runs on it. A new engine is a folder, its sources in kecore (CMakeLists.txt) and a row
// here; the contract's tests (tests/PhysicsTests.cpp) then run on it too (docs/contracts/physics.md, «Движки»).
#include "physics/PhysicsWorld.h"

#include <algorithm>
#include <cstdlib>
#include <thread>

namespace ke {

std::unique_ptr<PhysicsWorld> makeBox3dWorld(const PhysicsSettings& settings, const Vector3& gravity, int threads);
std::unique_ptr<PhysicsWorld> makeKineticaWorld(const PhysicsSettings& settings, const Vector3& gravity, int threads);

namespace {

struct Engine {
    const char* word;
    std::unique_ptr<PhysicsWorld> (*make)(const PhysicsSettings&, const Vector3&, int threads);
};

const Engine kEngines[] = {
    {"box3d", makeBox3dWorld},
    {"kinetica-physics", makeKineticaWorld},
};

} // namespace

const std::vector<std::string>& physicsEngines() {
    static const std::vector<std::string> words = [] {
        std::vector<std::string> out;
        for (const Engine& e : kEngines) out.push_back(e.word);
        return out;
    }();
    return words;
}

std::unique_ptr<PhysicsWorld> makePhysicsWorld(const PhysicsSettings& settings, const Vector3& gravity, int threads) {
    const Engine* e = std::find_if(std::begin(kEngines), std::end(kEngines), [&](const Engine& x) { return settings.engine == x.word; });
    if (e == std::end(kEngines)) e = &kEngines[0]; // none named; a word not built in is refused where the scene is read
    return e->make(settings, gravity, threads > 0 ? threads : physicsThreads());
}

int physicsThreads() {
    if (const char* given = std::getenv("KE_PHYSICS_THREADS"); given && std::atoi(given) > 0) return std::min(std::atoi(given), 64);
    return std::clamp(int(std::thread::hardware_concurrency()) / 2, 1, 8);
}

} // namespace ke
