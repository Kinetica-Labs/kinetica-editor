// The scene as a document (scene/Document.h) without Qt: the history's limit and its edges, the transaction that restores
// a failed change to the bit, ids that are never given twice, a new scene and a load as one step each, the floor against
// the text the window gave, and the answer of an undo against the recorded ones (tests/data/window-transcript.jsonl:
// lines n 4 and 5 - nothing to undo or redo - and n 53, 59, 61 - the keys of an open undone, a light's field undone and
// redone; the rest of the recorded undos name scenes the test does not build, so the keys and the changed list of an added
// object, a world edit and an object gone are checked here by what they must say).
#include "TestRunner.h"

#include "core/Json.h"
#include "scene/Document.h"
#include "scene/SceneEdits.h"
#include "scene/SceneOps.h"

#include <fstream>
#include <set>
#include <stdexcept>
#include <sstream>

namespace {

Value recorded(int n) {
    std::ifstream file(std::string(KE_TEST_DATA) + "/window-transcript.jsonl");
    for (std::string line; std::getline(file, line);)
        if (Value row = parseJson(line); row["n"].integer() == n) return row;
    return Value();
}

bool run(Document& d, const char* op, const char* args, Value* answer = nullptr) {
    std::string error;
    Value made = Value::map();
    const bool done = d.transact([&](SceneGraph& g, std::string& why) { return findSceneOp(op)->edit(g, parseJson(args), made, why); }, error);
    if (answer) *answer = made;
    return done;
}

Document withFloor() {
    Document d;
    d.newScene();
    return d;
}

bool sameAnswer(const Value& got, const Value& row) {
    Value want = row["answer"];
    Value plain = Value::map();
    for (const std::string& key : want.keys()) if (key != "ok") plain.set(key, want[key]);
    return toJson(got) == toJson(plain);
}

bool sameKeys(const Value& got, const Value& row) {
    std::set<std::string> a, b;
    for (const std::string& key : got.keys()) a.insert(key);
    for (const std::string& key : row["answer"].keys()) if (key != "ok") b.insert(key);
    return a == b;
}

} // namespace

void testHistoryLimitAndEdges() {
    History h;
    SceneGraph g;
    for (uint32_t i = 0; i < 105; ++i) h.push(floorScene(i + 1));
    CHECK(h.steps() == 100 && h.forgotten() == 5, "a full history forgets the oldest and counts them (%zu, %zu)", h.steps(), h.forgotten());
    CHECK(h.undo(g) && h.canRedo() && h.steps() == 99, "an undo moves a step to redo");
    h.push(g);
    CHECK(!h.canRedo(), "a new step clears redo");
    while (h.undo(g)) {}
    CHECK(g.entities[0].id == 6, "the oldest five are the ones forgotten (%u)", g.entities[0].id);
    for (uint32_t i = 0; i < 100; ++i) h.push(floorScene(i + 1));
    h.undo(g);
    h.cut(40);
    CHECK(h.steps() == 40 && !h.canRedo(), "cut keeps the first steps and clears redo (%zu)", h.steps());
    h.cut(90);
    CHECK(h.steps() == 40, "cut above the count changes nothing");
    while (h.undo(g)) {}
    CHECK(g.entities[0].id == 1, "cut keeps the oldest steps (%u)", g.entities[0].id);
    h.push(floorScene(8));
    SceneGraph marked = floorScene(7);
    h.push(marked);
    CHECK(h.pop().save() == marked.save() && h.steps() == 1, "pop hands back the last step");
    History empty;
    CHECK(empty.pop().save() == SceneGraph().save() && !empty.undo(g) && !empty.redo(g), "an empty history has nothing to pop, undo or redo");
}

void testDocumentTransact() {
    Document d = withFloor();
    CHECK(d.history.steps() == 0, "the first new scene has nothing to keep");
    const std::string was = d.graph.save();
    auto spoil = [](SceneGraph& g, std::string& why) { g.entities.clear(); why = "no"; return false; };
    std::string error;
    CHECK(!d.transact(spoil, error) && d.graph.save() == was && d.history.steps() == 0 && error == "no",
          "a failing change that spoiled the graph leaves it to the bit and no step");
    CHECK(run(d, "scene_add", R"({"shape": "box"})") && d.history.steps() == 1, "a change that works is one step");
    const std::string with = d.graph.save();
    CHECK(!d.transact(spoil, error) && d.history.steps() == 1 && d.graph.save() == with, "a failing change after a step takes only its own step back");
    CHECK(!d.transact(spoil, error, [] { return false; }) && d.history.steps() == 1 &&
              d.graph.save() == with && error == "no",
          "a step the caller did not take is not taken back");
    bool thrown = false;
    try {
        d.transact([](SceneGraph& g, std::string&) -> bool { g.entities.clear(); throw std::runtime_error("boom"); }, error);
    } catch (const std::runtime_error&) {
        thrown = true;
    }
    CHECK(thrown && d.history.steps() == 1 && d.graph.save() == with, "a change that throws leaves the graph to the bit and takes its step back");
    CHECK(d.transact([](SceneGraph& g, std::string&) { g.entities.push_back(Entity()); g.entities.back().id = 0; return true; }, error) &&
              d.graph.entities.back().id != 0 && d.history.steps() == 2,
          "a new entity of id 0 is given an id");
}

void testDocumentIds() {
    Document d = withFloor();
    Value answer;
    run(d, "scene_add", R"({"shape": "box"})", &answer);
    const uint32_t first = uint32_t(answer["id"].integer());
    CHECK(first == 2, "the first object after the floor is 2 (%u)", first);
    d.undo();
    run(d, "scene_add", R"({"shape": "box"})", &answer);
    CHECK(uint32_t(answer["id"].integer()) > first, "an id undone is not given again (%d)", answer["id"].integer());
    const uint32_t next = d.nextIdToGive();
    d.newScene();
    CHECK(d.graph.entities[0].id == next && d.nextIdToGive() == next + 1, "a new scene takes the next id, the counter never goes back");
    SceneGraph small = floorScene(1);
    d.load(small);
    CHECK(d.nextIdToGive() > next, "a scene with small ids does not lower the counter (%u)", d.nextIdToGive());
    SceneGraph odd = floorScene(0);
    odd.entities.push_back(floorScene(5).entities[0]);
    odd.entities.push_back(floorScene(5).entities[0]);
    d.load(odd);
    std::set<uint32_t> ids;
    for (const Entity& e : d.graph.entities) ids.insert(e.id);
    CHECK(ids.size() == 3 && !ids.count(0), "an entity of id 0 and two of one id each get their own (%zu)", ids.size());
    d.setNextId(5);
    d.load(floorScene(1));
    CHECK(d.nextIdToGive() == 5 && d.graph.entities[0].id == 1, "a replayed session sets the counter, the graph's own ids raise it only past them");
}

void testDocumentLoadAndNewScene() {
    Document d = withFloor();
    run(d, "scene_add", R"({"shape": "box"})");
    const std::string mine = d.graph.save();
    const size_t steps = d.history.steps();
    d.load(floorScene(3));
    CHECK(d.history.steps() == steps + 1 && d.graph.entities.size() == 1, "a load is one step");
    d.undo();
    CHECK(d.graph.save() == mine, "and its undo brings the scene before back");
    d.redo();
    d.newScene();
    CHECK(d.history.steps() == steps + 2, "a new scene over a scene is one step");
    d.newScene([] { return false; });
    CHECK(d.history.steps() == steps + 2, "a caller's remember decides the step");
    Document fresh;
    fresh.newScene();
    CHECK(fresh.history.steps() == 0 && fresh.graph.entities.size() == 1 && fresh.graph.entities[0].id == 1, "the first new scene: the floor with id 1, no step");
    Document blank;
    blank.graph.world.gravity = Vector3(0.0_r, -1.0_r, 0.0_r);
    blank.newScene();
    CHECK(blank.history.steps() == 0, "a world with nothing in it has nothing to keep");
}

void testFloorSceneIsTheWindowsFloor() {
    CHECK(floorScene(1).save() == recorded(2)["answer"]["text"].text(), "the floor is the text the fresh window gave");
}

void testHistoryAnswers() {
    Document d = withFloor();
    CHECK(sameAnswer(historyAnswer(d.graph, d.graph, true, d.undo(), d.history.steps()), recorded(4)), "nothing to undo: the recorded answer");
    CHECK(sameAnswer(historyAnswer(d.graph, d.graph, false, d.redo(), d.history.steps()), recorded(5)), "nothing to redo: the recorded answer");

    Value light;
    run(d, "scene_add_light", R"({"kind": "point"})", &light);
    const int id = light["id"].integer();
    run(d, "scene_set", (R"({"id": )" + std::to_string(id) + R"(, "line": "kind", "key": "intensity", "values": [2]})").c_str());
    const SceneGraph before = d.graph;
    const bool undone = d.undo();
    Value got = historyAnswer(before, d.graph, true, undone, d.history.steps());
    CHECK(toJson(got["changed"]) == "[" + std::to_string(id) + "]" && got.keys().size() == 4 && got.has("undone") &&
              got["undone"].truth() && !got["world"].truth() && got["undo_steps"].integer() == 1 && sameKeys(got, recorded(59)),
          "a light's field undone: its id, the keys of the recorded line 59");
    const SceneGraph after = d.graph;
    const bool redone = d.redo();
    got = historyAnswer(after, d.graph, false, redone, d.history.steps());
    CHECK(toJson(got["changed"]) == "[" + std::to_string(id) + "]" && got["redone"].truth() && got["undo_steps"].integer() == 2 && sameKeys(got, recorded(61)),
          "and redone: the keys of the recorded line 61");

    run(d, "scene_add", R"({"shape": "box"})");
    const SceneGraph added = d.graph;
    d.undo();
    got = historyAnswer(added, d.graph, true, true, d.history.steps());
    CHECK(got["changed"].items().size() == 1 && got["changed"].items()[0].integer() == 3, "an added object undone: its id (%s)", toJson(got["changed"]).c_str());

    run(d, "world_set", R"({"line": "physics", "key": "substeps", "values": [12]})");
    const SceneGraph worldEdited = d.graph;
    d.undo();
    got = historyAnswer(worldEdited, d.graph, true, true, d.history.steps());
    CHECK(got["world"].truth() && got["changed"].items().empty(), "a world edit undone: world, no object");

    Document opened = withFloor();
    SceneGraph other = floorScene(9);
    other.entities.push_back(floorScene(10).entities[0]);
    opened.load(other);
    const SceneGraph openedGraph = opened.graph;
    const bool back = opened.undo();
    got = historyAnswer(openedGraph, opened.graph, true, back, opened.history.steps());
    CHECK(!got["changed"].items().empty() && got["undone"].truth() && !got["world"].truth() && sameKeys(got, recorded(53)),
          "an open undone: the objects that differ, the keys of the recorded line 53");
}

void testDocumentPlayBookkeeping() {
    Document d = withFloor();
    run(d, "scene_add", R"({"shape": "box"})");
    const std::string before = d.graph.save();
    const SceneGraph built = d.beginPlay();
    CHECK(d.mode == PlayMode::Paused && built.save() == before && d.backup.save() == before && d.running.save() == before,
          "Play remembers the scene and hands it to build from");
    run(d, "scene_add", R"({"shape": "sphere"})");
    run(d, "world_set", R"({"line": "world", "key": "gravity", "values": [0, -5, 0]})");
    CHECK(d.history.steps() == 3, "the steps made while it runs are in the history (%zu)", d.history.steps());
    d.endPlay(false);
    CHECK(d.mode == PlayMode::Edit && d.graph.save() == before && d.history.steps() == 1 && !d.history.canRedo(),
          "Stop brings the scene back and the steps made on the way go");

    d.beginPlay();
    const SceneGraph kept = d.backup;
    run(d, "scene_add", R"({"shape": "sphere"})");
    d.endPlay(false, &kept);
    CHECK(d.history.steps() == 2 && d.graph.save() == kept.save(), "the K step: one step after Stop that takes the kept poses back");

    d.beginPlay();
    run(d, "scene_add", R"({"shape": "sphere"})");
    const std::string edited = d.graph.save();
    d.endPlay(true);
    CHECK(d.mode == PlayMode::Edit && d.graph.save() == edited && d.history.steps() == 3, "a kept edit stays, with its step");

    CHECK(std::string(modeWord(PlayMode::Edit)) == "edit" && std::string(modeWord(PlayMode::Playing)) == "playing" &&
              std::string(modeWord(PlayMode::Paused)) == "paused", "the three words of a mode");
}
