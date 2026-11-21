// The check of an operation's arguments (scene/ArgsSchema.h) without Qt: the schema of every operation's line of help
// against tests/data/ops-list.json, the refusal texts against tests/data/args-refusals.json, and the grammar written out
// as small cases. args-refusals.json is what the window's first (Qt) copy of the check answered, now deleted: a frozen
// oracle that cannot be recorded again.
#include "TestRunner.h"

#include "core/Json.h"
#include "scene/ArgsSchema.h"

#include <fstream>
#include <locale>
#include <sstream>

namespace {

Value fixture(const char* name) {
    std::ifstream file(std::string(KE_TEST_DATA) + "/" + name, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return parseJson(text.str());
}

std::string problem(const char* line, const char* args) { return argsProblem(parseJson(args), argsSchema(line)); }

struct CommaPoint : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
};

} // namespace

void testArgsSchemaOfEveryOperation() {
    const Value ops = fixture("ops-list.json")["ops"];
    CHECK(ops.items().size() >= 100, "the recorded list holds the operations (%zu)", ops.items().size());
    for (const Value& op : ops.items()) {
        const std::string line = op["args"].text();
        CHECK(toJson(argsSchema(line)) == toJson(op["schema"]), "the schema of %s", op["name"].text().c_str());
        CHECK(line.find_first_of("\t\r\n\xC2") == std::string::npos, "the line of %s holds a space and no other whitespace", op["name"].text().c_str());
    }
}

void testArgsRefusalsFrozenFixture() {
    const Value rows = fixture("args-refusals.json");
    CHECK(rows.items().size() > 500, "the recorded refusals are there (%zu)", rows.items().size());
    for (const Value& row : rows.items()) {
        const std::string got = argsProblem(row["args"], argsSchema(row["line"].text()));
        CHECK(got == row["problem"].text(), "%s <- %s: «%s», Qt said «%s»", row["line"].text().c_str(), toJson(row["args"]).c_str(), got.c_str(), row["problem"].text().c_str());
    }
}

void testArgsGrammarCases() {
    const char* line = "{id: int, size?: [x,y,z], face?: '+x'..'-z', kind?: 'a'|'b', n?: number (0.5), ids2?: [int], pick?: ['p'|'q'], values?: {}}";
    CHECK(problem(line, R"({"id": 1})").empty(), "a field required is given");
    CHECK(problem(line, "{}") == "нет обязательного поля «id»", "a field required is missing");
    CHECK(problem(line, R"({"id": null})") == "id: ожидалось целое", "null is not a value of a typed field");
    CHECK(problem(line, R"({"id": 3.0})").empty() && problem(line, R"({"id": 2.5})") == "id: ожидалось целое", "an integer is a whole number, 3.0 included");
    CHECK(problem(line, R"({"id": 1, "n": 3})").empty(), "an integer is a number");
    CHECK(problem(line, R"({"id": 1, "n": "x"})") == "n: ожидалось число (number (0.5))", "a type that says more than itself keeps its words: the default");
    CHECK(problem(line, R"({"id": 1, "size": [1,2]})") == "size: ожидалось 3 числа", "a list of N numbers has N");
    CHECK(problem(line, R"({"id": 1, "size": [1,"a",3]})") == "size: в списке ожидалось число", "a list of numbers holds numbers");
    CHECK(problem(line, R"({"id": 1, "size": "x"})") == "size: ожидалось список", "a list is a list");
    CHECK(problem(line, R"({"id": 1, "ids2": [1, "a"]})") == "ids2: в списке ожидалось целое", "a list of integers holds integers");
    CHECK(problem(line, R"({"id": 1, "pick": ["q", "z"]})") == "pick: в списке ожидалось строка", "a list of words holds words");
    CHECK(problem(line, R"({"id": 1, "face": "+q"})") == "face: «+q» не из списка: +x, -x, +y, -y, +z, -z", "a range of faces is the six faces");
    CHECK(problem(line, R"({"id": 1, "kind": "B"})") == "kind: «B» не из списка: a, b", "a word is of the list, in its case");
    CHECK(problem(line, R"({"id": 1, "values": 1})") == "values: ожидалось объект ({})", "an object field is an object");
    CHECK(problem(line, R"({"id": 1, "zzz": 1})") == "нет поля «zzz»; поля: face, id, ids2, kind, n, pick, size, values", "a field the line does not name is refused, the fields said");
    CHECK(problem(line, R"({"id": 1, "_py": 1, "_x": "x"})").empty(), "a key that begins with an underscore is never checked");
    CHECK(problem(line, R"({"zz": 1, "id": "x", "aa": 2})") == "нет поля «aa»; поля: face, id, ids2, kind, n, pick, size, values; id: ожидалось целое; нет поля «zz»; поля: face, id, ids2, kind, n, pick, size, values",
          "every problem, in the order of the keys");
    CHECK(problem(line, R"({"zz": 1})").rfind("; нет обязательного поля «id»") != std::string::npos, "the missing fields come after the wrong ones");

    CHECK(problem("{axis?: 0|1|2}", R"({"axis": 5})") == "axis: «5» не из списка: 0, 1, 2", "words that are numbers, written as %%g");
    CHECK(problem("{axis?: 0|1|2}", R"({"axis": 1.5})") == "axis: ожидалось целое", "...and of the integer type");
    CHECK(problem("{}", R"({"a": 1})") == "аргументов нет, а дано «a»", "a line of no fields refuses any");
    CHECK(problem("{by?: [x,y,z] | number}", R"({"by": "x"})").empty() && problem("{by?: [x,y,z] | number}", R"({"by": true})").empty(), "a field of either shape is not checked");
    CHECK(toJson(argsSchema("{by?: [x,y,z] | number}")).find("anyOf") != std::string::npos, "...but its schema says them");
    CHECK(problem("{ids, count?: int}", R"({"ids": [1, 2.5]})") == "ids: в списке ожидалось целое" && problem("{ids}", "{}").empty(), "{ids} is optional and a list of integers");

    const char* either = "{target: int, kind: 'imu'|'sonar'} | {joint: int, kind: 'wrench'|'effort'}";
    CHECK(problem(either, R"({"joint": 5, "kind": "wrench"})").empty() && problem(either, R"({"target": 5, "kind": "imu"})").empty(), "a call takes one shape or the other");
    CHECK(problem(either, R"({"joint": 5, "kind": "lidar"})") == "kind: «lidar» не из списка: imu, sonar, wrench, effort", "a field of both shapes takes the words of both");
    CHECK(problem(either, "{}") == "нет обязательного поля «kind»", "required is what both shapes require");

    CHECK(toJson(argsSchema("{baseline?: m (0.10)}")) ==
              R"J({"additionalProperties":false,"properties":{"baseline":{"default":0.1,"description":"m (0.10)","type":"number"}},"type":"object"})J",
          "a unit word is a number, its default and words kept");
    CHECK(toJson(argsSchema("{a: int | b: [int]}")) ==
              R"({"additionalProperties":false,"properties":{"a":{"type":"integer"},"b":{"items":{"type":"integer"},"type":"array"}},"type":"object"})",
          "either field, neither required");
}

void testArgsNumbersWithoutTheLocale() {
    const Value schema = parseJson(R"({"properties": {"x": {"type": "number", "minimum": 0, "maximum": 0.5}}})");
    const std::locale before = std::locale::global(std::locale(std::locale::classic(), new CommaPoint));
    CHECK(argsProblem(parseJson(R"({"x": 2.25})"), schema) == "x: 2.25 вне 0…0.5", "a number in a text keeps its point under a comma locale");
    CHECK(argsProblem(parseJson(R"({"x": 1234567.0})"), schema) == "x: 1.23457e+06 вне 0…0.5", "six significant digits, as %%g");
    std::locale::global(before);
    CHECK(argsProblem(parseJson(R"({"x": [1]})"), parseJson(R"({"properties": {"x": {"type": "array", "minItems": 3, "maxItems": 3}}})")) == "x: ожидалось 3 числа", "a list of the wrong length");
}
