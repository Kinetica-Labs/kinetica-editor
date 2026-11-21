// The wire of the windowless host (api/Wire.h) over string streams: the raw lines of the window's recording
// (tests/data/window-transcript.jsonl, the ones that carry "raw" and "reply") answered as the window answered them -
// where the wording is the core's own, only its shape is held - and what the recording cannot say: two requests in order,
// a flush after each answer, and a line of a megabyte read whole.
#include "TestRunner.h"

#include "api/Wire.h"
#include "core/Json.h"

#include <fstream>
#include <sstream>

using namespace ke;

namespace {

Value rows() {
    std::ifstream file(std::string(KE_TEST_DATA) + "/window-transcript.jsonl");
    Value out = Value::list();
    std::string line;
    std::getline(file, line); // the first line explains the fields
    while (std::getline(file, line)) out.push(parseJson(line));
    return out;
}

std::string answered(Scene& scene, const std::string& input) {
    std::istringstream in(input);
    std::ostringstream out;
    serve(in, out, scene);
    return out.str();
}

struct CountingBuf : std::stringbuf {
    int syncs = 0;
    int sync() override { ++syncs; return std::stringbuf::sync(); }
};

} // namespace

void testWireTranscript() {
    Scene scene("");
    for (int k = 0; k < 25; ++k) scene.call("scene_new"); // where the window stood: the floor is 26, the next object 27
    const std::string unknown = scene.call("")["error"].text();
    CHECK(unknown.find("kinetica-core") != std::string::npos, "the unknown-operation text names the host: %s", unknown.c_str());
    int compared = 0;
    const Value recording = rows();
    for (const Value& row : recording.items()) {
        if (!row.has("raw")) continue;
        const std::string got = answered(scene, row["raw"].text() + "\n");
        ++compared;
        if (row["reply"].isNull()) {
            CHECK(got.empty(), "line %d: a blank line has no answer, got %s", row["n"].integer(), got.c_str());
            continue;
        }
        Value want = parseJson(row["reply"].text()), mine;
        std::string why;
        CHECK(!got.empty() && got.back() == '\n' && parseJson(got.substr(0, got.size() - 1), mine, why), "line %d: one JSON line, got %s",
              row["n"].integer(), got.c_str());
        const std::string wantError = want["result"]["error"].text();
        if (wantError.rfind("not JSON", 0) == 0) {
            CHECK(!mine.contains("id") && !mine["result"]["ok"].truth(true) && mine["result"]["error"].text().rfind("не JSON: ", 0) == 0,
                  "line %d (%s): not JSON, no id, in the core's words: %s", row["n"].integer(), row["note"].text().c_str(), got.c_str());
            continue;
        }
        if (wantError.rfind("нет операции", 0) == 0) want.set("result", scene.call(""));
        CHECK(toJson(want) == toJson(mine), "line %d (%s)\n    window: %s\n    host:   %s", row["n"].integer(), row["note"].text().c_str(),
              toJson(want).c_str(), toJson(mine).c_str());
    }
    CHECK(compared == 19, "every raw line of the recording is replayed (%d)", compared);
}

void testWireFraming() {
    Scene scene("");
    CHECK(answered(scene, "{\"id\":1,\"op\":\"scene_list\"}\n{\"id\":\"b\",\"op\":\"scene_list\",\"args\":{\"tag\":\"zz\"}}\n") ==
              "{\"id\":1,\"result\":{\"objects\":[{\"id\":1,\"kind\":\"entity\",\"name\":\"Пол\"}],\"ok\":true}}\n{\"id\":\"b\",\"result\":{\"objects\":[],\"ok\":true}}\n",
          "two requests, two answers, in order");
    CHECK(answered(scene, "\n\r\n{\"op\":\"scene_list\",\"args\":{\"tag\":\"zz\"}}") == "{\"result\":{\"objects\":[],\"ok\":true}}\n",
          "blank lines are skipped, the last line needs no newline");

    CountingBuf buf;
    std::ostream out(&buf);
    std::istringstream in("{\"id\":1,\"op\":\"scene_list\"}\nnot json\n{\"id\":2,\"op\":\"scene_list\"}\n");
    serve(in, out, scene);
    CHECK(buf.syncs == 3, "one flush per answer (%d)", buf.syncs);

    const std::string big(1 << 20, 'x');
    std::string text = answered(scene, "{\"id\":7,\"op\":\"scene_list\",\"args\":{\"tag\":\"" + big + "\"}}\n");
    text.pop_back();
    const Value reply = parseJson(text);
    CHECK(reply["id"].integer() == 7 && reply["result"]["ok"].truth(), "a line of a megabyte is read whole: %s", text.c_str());
}
