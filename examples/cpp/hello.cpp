// The first rung of the C++ way: a box falls on the floor and its height is printed - the same operations as over the
// wire, called in place with no pipe. Built as the target ke-hello by every build, so that it cannot rot; run it from
// the repository's root (it reads the sensor tables from ./data).
#include "api/Scene.h"
#include "core/Json.h"

#include <cstdio>

using namespace ke;

int main() {
    Scene scene("data");
    const Value added = scene.call("scene_add", parseJson(R"({"shape":"box","role":"rigid","position":[0,1,0]})"));
    if (!added["ok"].truth()) return std::fprintf(stderr, "%s\n", added["error"].text().c_str()), 1;
    const int box = added["id"].integer();
    const Value state = scene.call("sim_step", parseJson(R"({"frames":60})"));
    for (const Value& shape : state["shapes"].items())
        if (shape["id"].integer() == box)
            std::printf("after %.2f s the box is %.3f m high\n", state["time"].number(), shape["position"].items()[1].number());
}
