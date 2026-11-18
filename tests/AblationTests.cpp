// The ablation, checked: this editor is robotics on a rigid-body engine, and a scene file with a role of
// other physics - the words below - is refused with its line and word named, never half loaded. The
// only file of the repository that may say these words (the CI's ablation check leaves it out).
#include "TestRunner.h"

#include "scene/SceneGraph.h"

#include <cstdio>

using namespace ke;

void testSceneRefusesOtherPhysics() {
    for (const char* role : {"soft density 1000", "cloth areaDensity 0.2", "magnet moment 1 0 0", "liquid", "emitter smoke 1"}) {
        SceneGraph g;
        std::string error;
        const std::string text = std::string("world gravity 0 -9.81 0\nentity \"x\"\n  shape box size 1 1 1\n  ") + role + "\nend\n";
        const bool loaded = g.load(text, error);
        const std::string word = std::string(role).substr(0, std::string(role).find(' '));
        CHECK(!loaded && error.find("line 4") != std::string::npos && error.find(word) != std::string::npos,
              "the role '%s' was not refused with its line and word: %s", word.c_str(), error.c_str());
    }
    SceneGraph g;
    std::string error;
    const bool loaded = g.load("world gravity 0 -9.81 0 gas 1\n", error);
    std::printf("  a gas switch in the world line: %s\n", error.c_str());
    CHECK(!loaded && error.find("gas") != std::string::npos, "the world's gas switch was not refused: %s", error.c_str());
}
