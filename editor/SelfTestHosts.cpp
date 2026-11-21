// The two hosts' JSON agree: the window's server speaks Qt's QJsonDocument, the windowless host speaks the
// core's ke::toJson / ke::parseJson (core/Json.h). This group (wire-parity) is the guard of that second copy:
// a corpus of values written by both to the same bytes, read by both to the same trees, a list of malformed
// texts refused by both or listed here as a difference, and a sweep of doubles and of byte strings. It
// needs no window. tests/JsonTests.cpp pins what it found, so the core holds the borders without Qt.
// The group schema-parity (temporary, below: it goes with the Qt copy of the args check) runs that copy and the core's
// side by side over the live registry and a generated corpus, and under KE_RECORD=1 writes tests/data/args-refusals.json.
#include "SelfTestSupport.h"

#include "ArgsSchema.h"
#include "EditorOps.h"
#include "Resources.h"
#include "core/Format.h"
#include "core/Json.h"
#include "scene/ArgsSchema.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <utility>

using namespace ke;
using namespace selftest;

namespace {

Value list(std::initializer_list<Value> items) {
    Value l = Value::list();
    for (const Value& v : items) l.push(v);
    return l;
}

Value map(std::initializer_list<std::pair<const char*, Value>> members) {
    Value m = Value::map();
    for (const auto& [key, v] : members) m.set(key, v);
    return m;
}

// The same tree: shape, whole flag, numbers bit for bit, texts, key order.
bool same(const Value& a, const Value& b) {
    if (a.type() != b.type()) return false;
    switch (a.type()) {
    case Value::Type::Bool: return a.truth() == b.truth();
    case Value::Type::Number: {
        const double x = a.number(), y = b.number();
        return a.whole() == b.whole() && std::memcmp(&x, &y, sizeof x) == 0;
    }
    case Value::Type::Text: return a.text() == b.text();
    case Value::Type::List:
        if (a.items().size() != b.items().size()) return false;
        for (size_t k = 0; k < a.items().size(); ++k)
            if (!same(a.items()[k], b.items()[k])) return false;
        return true;
    case Value::Type::Map:
        if (a.keys() != b.keys()) return false;
        for (size_t k = 0; k < a.size(); ++k)
            if (!same(a.at(k), b.at(k))) return false;
        return true;
    default: return true;
    }
}

QByteArray bytes(const std::string& s) { return QByteArray(s.data(), qsizetype(s.size())); }

// What the window's server would write for v, inside an array (Qt's document holds nothing but an object or an array).
std::string qtWrapped(const Value& v) {
    QJsonArray a;
    a.append(coreJson(v));
    return QJsonDocument(a).toJson(QJsonDocument::Compact).toStdString();
}

// What the window's server reads: its valueOf of the document, or nothing if Qt refuses the text.
bool qtRead(const std::string& text, Value& out) {
    QJsonParseError e;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes(text), &e);
    if (e.error != QJsonParseError::NoError) return false;
    out = coreValue(doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()));
    return true;
}

std::string escaped(const std::string& s) {
    std::string out;
    for (const unsigned char c : s) out += c >= 0x20 && c < 0x7F ? std::string(1, char(c)) : format("\\x%02X", c);
    return out.size() > 80 ? out.substr(0, 80) + "..." : out;
}

std::string nestedLists(int n) { return std::string(size_t(n), '[') + std::string(size_t(n), ']'); }

std::string nestedObjects(int n) {
    std::string s;
    for (int k = 0; k < n; ++k) s += "{\"a\":";
    return s + "1" + std::string(size_t(n), '}');
}

std::vector<Value> corpus() {
    std::vector<Value> c = {Value(), Value(true), Value(false), Value(""), Value::list(), Value::map()};
    for (const double d : {0.0, -0.0, 1.0, -1.0, 2147483648.0, 9007199254740991.0, 9007199254740992.0, 9e15, 9.5e15, 1e15, 1e16, 1e21, 1e22, 1e23, -1e21,
                           0.1, 1.0 / 3, 1e-5, 1e-6, 1e-7, 1e-3, 1e-4, 1.5e-4, 123456789.125, 0.009999999776482582, 9.224468158208765e-06, 5e-324,
                           1.7976931348623157e308, double(0.01f), double(0.1f), double(1.0f / 3), 4503599627370496.5, 12345678901234567890.0,
                           std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
        c.push_back(Value(d));
    c.push_back(Value(int64_t(1) << 60));
    c.push_back(Value(int(-7)));
    for (int ch = 0; ch < 0x20; ++ch) c.push_back(Value(std::string(1, char(ch))));
    std::string controls;
    for (int ch = 0; ch < 0x20; ++ch) controls += char(ch);
    c.push_back(Value(controls));
    for (const char* t : {"\"", "\\", "/", "\x7f", "a\"b\\c/d", "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82", "\xF0\x9F\x98\x80", "\xE2\x80\xA8\xE2\x80\xA9", "\xEF\xBF\xBF\xF4\x8F\xBF\xBF",
                          "\xFF", "a\xFF" "b", "\xC3", "\xE2\x82", "\xC0\x80", "\xED\xA0\x80", "\xF5\x80\x80\x80", "\xF4\x90\x80\x80", "\xE2\x82\x28", "tab\there", "line\nbreak"})
        c.push_back(Value(t));
    c.push_back(map({{"b", Value(1)}, {"a", Value(2)}, {"B", Value(3)}, {"\xC3\xA9", Value(4)}, {"Z", Value(5)}, {"_", Value(6)}, {"aa", Value(7)}, {"a b", Value(8)},
                     {"10", Value(9)}, {"9", Value(10)}, {"", Value(11)}, {"\"q\"", Value(12)}, {"\n", Value(13)}, {"\xD1\x8F", Value(14)}, {"\xEF\xBD\x9E", Value(15)}}));
    c.push_back(map({{"\xFF", Value(1)}, {"\xFE", Value(2)}}));
    c.push_back(map({{"ids", list({Value(1), Value(2.5), Value("x"), Value()})}, {"nested", map({{"z", list({})}, {"y", map({})}})}}));
    Value deep = Value::list();
    Value* at = &deep;
    for (int k = 0; k < 50; ++k) {
        at = &at->push(k % 2 ? Value::list() : map({{"k", Value(k)}}));
        if (k % 2 == 0) at = &at->set("d", Value::list());
    }
    c.push_back(deep);
    return c;
}

// Texts to read. Both must refuse them, or the difference is declared below.
std::vector<std::string> malformed() {
    std::vector<std::string> m = {"[\"a", "[nul]", "[tru]", "[True]", "[truex]", "[nan]", "[Infinity]", "[1,]", "[,1]", "[1,,2]", "{\"a\":1,}", "{,}", "{\"a\"}", "{\"a\":}", "{a:1}", "{'a':1}",
                                  "{1:2}", "{\"a\":1 \"b\":2}", "[01]", "[-01]", "[00]", "[-]", "[--1]", "[+1]", "[1.]", "[.5]", "[-.5]", "[1.e5]", "[.e5]", "[.]", "[-.]", "[1e]", "[1e+]", "[1.5e]", "[0x10]",
                                  "[1_0]", "[1 2]", "[\"a\" \"b\"]", "[1e999]", "[-1e999]", "[1e-999]", "[2e-324]", "[4e-324]", "[5e-324]", "[\"\\x\"]", "[\"\\a\"]", "[\"\\U0041\"]", "[\"\\\"]",
                                  "[\"\\u12\"]", "[\"\\u00g0\"]", "[\"\\u+041\"]", "[\"\\u 041\"]", "[\"\\ud800\"]", "[\"\\udc00\"]", "[\"\\ud800\\u0041\"]", "[\"\\ud83d\\ude00\"]", "[\"\\ud83d\\ude00\\ude00\"]",
                                  "[\"\\ude00\\ud83d\"]", "[\"\\ud83dx\"]", "[\"\\u0000\"]", "[\"\\/\"]", "[\"\\u00E9\"]", "[\"\t\"]", "[\"\n\"]", "[\"\x7f\"]", "[\"a\x01\"]", "[\x0b" "1]", "[\f1]", "[1\x1f]", "[1]x",
                                  "[1][2]", "[1]//c", "/*c*/[1]", "", "  ", "[", "]", "{", "}", "[1", "{\"a\":1", "[ ]", "{ }", " [ 1 , 2 ] ", "[1\t,\r\n2]", "{\"a\":1,\"a\":2}", "{\"b\":1,\"a\":2}", "{\"\\u00e9\":1,\"z\":2,\"\xC3\xA9\":3}",
                                  "[\"\xC3\"]", "[\"\xFF\"]", "[\"\xC0\x80\"]", "[\"\xED\xA0\x80\"]", "[\"\xF4\x90\x80\x80\"]", "[\"\xE2\x82\"]", "[\"\\\xC3\xA9\"]", "[\"\xEF\xBF\xBF\"]", "[\"\xF4\x8F\xBF\xBF\"]",
                                  "[\"\xED\x9F\xBF\"]", "[\"\xEE\x80\x80\"]", "[12345678901234567890]", "[9007199254740993]", "[-0]", "[-0.0]", "[0e0]", "[1E5]", "[0.1e1]", "[100000000000000000000]", "[9e15]",
                                  "[8999999999999999]", "[9007199254740992]", "[1e15]", "[1e16]", "{\"a\":[{}]}", "[\"a\"x]", "[\"\\ud800\\u00g0\"]", "[\"\\ud800\\u12\"]", "1", "\"a\"", "null", "true"};
    m.push_back(std::string("[\"a\0b\"]", 7));
    m.push_back(std::string("[1]\0", 4));
    for (const int n : {200, 201, 1023, 1024, 1025}) m.push_back(nestedLists(n));
    for (const int n : {200, 201, 1024, 1025}) m.push_back(nestedObjects(n));
    return m;
}

// Where the core and Qt part, on purpose. Nothing else may differ.
bool declaredDifference(const std::string& text) {
    static const std::vector<std::string> table = {
        "1", "\"a\"", "null", "true", // the host splits an object from the rest; Qt reads only an object or an array
        nestedLists(201), nestedLists(1023), nestedLists(1024), nestedObjects(201), nestedObjects(1024), // the core's depth limit is 200, Qt's 1024
        "\xEF\xBB\xBF[1]",              // Qt skips a byte order mark at the start of the document
        "[\"\xEF\xBB\xBF" "x\"]",       // and drops one at the start of a text
        // A lone surrogate escape: the design says U+FFFD; Qt's QString keeps it and the window's toStdString writes '?'.
        "[\"\\ud800\"]", "[\"\\udc00\"]", "[\"\\ud800\\u0041\"]", "[\"\\ud83d\\ude00\\ude00\"]", "[\"\\ude00\\ud83d\"]", "[\"\\ud83dx\"]",
    };
    return std::find(table.begin(), table.end(), text) != table.end();
}

} // namespace

int runWireParityTests() {
    Checker c;
    const std::vector<Value> values = corpus();

    int written = 0, read = 0;
    for (const Value& v : values) {
        const std::string qt = qtWrapped(v), core = toJson(list({v}));
        if (qt != core) {
            std::printf("      write: Qt %s\n             core %s\n", escaped(qt).c_str(), escaped(core).c_str());
            ++written;
        }
        Value fromQt, fromCore;
        std::string error;
        if (!qtRead(qt, fromQt) || !parseJson(qt, fromCore, error) || !same(fromQt, fromCore)) {
            std::printf("      read: %s (%s)\n", escaped(qt).c_str(), error.c_str());
            ++read;
        }
        if (v.type() == Value::Type::Map || v.type() == Value::Type::List) {
            const QJsonDocument doc = v.type() == Value::Type::Map ? QJsonDocument(coreJson(v).toObject()) : QJsonDocument(coreJson(v).toArray());
            const std::string direct = doc.toJson(QJsonDocument::Compact).toStdString();
            if (direct != toJson(v) || !qtRead(direct, fromQt) || !parseJson(direct, fromCore, error) || !same(fromQt, fromCore)) {
                std::printf("      container: %s\n", escaped(direct).c_str());
                ++written;
            }
        }
    }
    c.check(written == 0, "the core writes every value of the corpus to the bytes Qt writes (shapes, numbers, escapes, keys in order)");
    c.check(read == 0, "the core reads Qt's text of every value to the tree Qt's valueOf makes (whole flag, bits, key order)");

    int numbers = 0;
    const auto number = [&numbers](double d) {
        if (!std::isfinite(d)) return;
        const Value n(d);
        const std::string qt = qtWrapped(n);
        Value fromQt, fromCore;
        std::string error;
        if (qt != toJson(list({n})) || !qtRead(qt, fromQt) || !parseJson(qt, fromCore, error) || !same(fromQt, fromCore))
            if (numbers++ < 5) std::printf("      number: %.17g Qt %s core %s\n", d, qt.c_str(), toJson(list({n})).c_str());
    };
    std::mt19937_64 rng(20261121);
    for (int k = 0; k < 40000; ++k) {
        double d;
        if (k % 2) {
            const uint64_t bits = rng();
            std::memcpy(&d, &bits, sizeof d);
        } else {
            d = double(int64_t(rng() % 100000000000ULL) - 50000000000LL) * std::pow(10.0, double(int(rng() % 60) - 30));
            if (k % 4 == 0) d = double(float(d));
        }
        number(d);
    }
    c.check(numbers == 0, "40000 doubles (random bits, short decimals, widened floats): the same text, the same value back");
    // A power of two has a lopsided neighbourhood - the doubles below it lie twice as close as those above: where
    // "the fewest digits" and "the nearest of them" are easiest to get wrong.
    for (int e = -1074; e <= 1023; ++e) {
        const double p = std::ldexp(1.0, e);
        number(p), number(-p), number(std::nextafter(p, 0.0)), number(std::nextafter(p, HUGE_VAL));
    }
    c.check(numbers == 0, "every power of two and its two neighbours: the same text, the same value back");

    int differences = 0, agreed = 0;
    for (const std::string& text : malformed()) {
        Value fromQt, fromCore;
        std::string error;
        const bool qtOk = qtRead(text, fromQt), coreOk = parseJson(text, fromCore, error);
        const bool differ = qtOk != coreOk || (qtOk && !same(fromQt, fromCore));
        if (differ != declaredDifference(text)) {
            std::printf("      %s: %s Qt %s, core %s\n", differ ? "undeclared difference" : "declared difference gone", escaped(text).c_str(), qtOk ? "reads" : "refuses", coreOk ? "reads" : ("refuses: " + error).c_str());
            ++differences;
        }
        agreed += !differ;
    }
    c.check(differences == 0, "over the malformed and the odd texts the core and Qt refuse the same and read to the same trees, but for the declared differences");
    c.check(agreed > 100, "...and most of the list is agreement");

    int repaired = 0;
    const unsigned char alphabet[] = {0x00, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC1, 0xC2, 0xDF, 0xE0, 0xE1, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF4, 0xF5, 0xFF};
    const size_t n = sizeof alphabet;
    for (size_t len = 1; len <= 4; ++len) {
        size_t total = 1;
        for (size_t k = 0; k < len; ++k) total *= n;
        for (size_t code = 0; code < total; ++code) {
            std::string s;
            for (size_t k = 0, rest = code; k < len; ++k, rest /= n) s += char(alphabet[rest % n]);
            if (validUtf8(s) != QString::fromUtf8(bytes(s)).toUtf8().toStdString() && repaired++ < 5) std::printf("      utf-8: %s\n", escaped(s).c_str());
        }
    }
    c.check(repaired == 0, "validUtf8 repairs every byte string of up to four bytes over the interesting bytes as QString::fromUtf8 does");
    return c.failures;
}

// ---- schema-parity ------------------------------------------------------------------------------------------------

namespace {

QJsonValue sampleFor(const QJsonObject& f) {
    const QJsonArray words = f.value("enum").toArray();
    if (!words.isEmpty()) return words.first();
    const QString type = f.value("type").toString();
    if (type == "string") return "abc";
    if (type == "integer") return 1;
    if (type == "number") return 0.5;
    if (type == "boolean") return true;
    if (type == "object") return QJsonObject();
    if (type != "array") return 1;
    const QJsonObject item = f.value("items").toObject();
    QJsonArray a;
    if (f.contains("minItems")) {
        for (int k = 0; k < f.value("minItems").toInt(); ++k) a.append(1.5);
    } else if (item.contains("enum")) {
        a.append(item.value("enum").toArray().first());
    } else if (item.value("type") == "integer") {
        a.append(1);
    } else if (item.value("type") == "array") {
        a.append(QJsonArray{1, 2, 3});
    }
    return a;
}

// The kinds of a wrong value to give a field: each is one way the check can say no, or fail to.
std::vector<std::pair<QString, QJsonValue>> wrongValues(const QJsonObject& f) {
    std::vector<std::pair<QString, QJsonValue>> w = {
        {"null", QJsonValue()}, {"true", true}, {"string", "str"}, {"fraction", 2.5}, {"negative", -3}, {"huge", 1e300}, {"whole", 3.0},
        {"list-empty", QJsonArray()}, {"list-one", QJsonArray{1}}, {"list-five", QJsonArray{1, 2, 3, 4, 5}}, {"list-item-string", QJsonArray{"x"}},
        {"list-three-one-wrong", QJsonArray{1, "x", 3}}, {"list-null-item", QJsonArray{QJsonValue()}}, {"object", QJsonObject()}, {"empty-string", ""},
        {"not-in-enum", "no-such-word"}, {"number-in-text", "1"}, {"big-integer", 99}, {"tiny", 1e-7}, {"large", 1234567.0}, {"fraction-list", QJsonArray{0.5}}};
    for (const QJsonValue& e : f.value("enum").toArray())
        if (e.isString()) w.push_back({"enum-case", e.toString().toUpper()}), w.push_back({"enum-prefix", e.toString() + "x"});
    for (const QJsonValue& e : f.value("items").toObject().value("enum").toArray())
        if (e.isString()) w.push_back({"item-enum-case", QJsonArray{e.toString().toUpper()}});
    return w;
}

struct Case {
    QString kind;
    QJsonObject args;
};

std::vector<Case> casesFor(const QJsonObject& schema) {
    const QJsonObject properties = schema.value("properties").toObject();
    QJsonObject base, all;
    std::vector<QString> required;
    for (const QJsonValue& r : schema.value("required").toArray()) required.push_back(r.toString()), base.insert(r.toString(), sampleFor(properties.value(r.toString()).toObject()));
    for (auto it = properties.begin(); it != properties.end(); ++it) all.insert(it.key(), sampleFor(it.value().toObject()));
    std::vector<Case> cases = {{"empty", {}}, {"valid-required", base}, {"valid-all", all}};
    const auto with = [](QJsonObject o, const QString& key, const QJsonValue& v) { return o.insert(key, v), o; };
    for (const QString& r : required) {
        QJsonObject o = base;
        o.remove(r);
        cases.push_back({"missing", o});
    }
    for (auto it = properties.begin(); it != properties.end(); ++it)
        for (const auto& [kind, v] : wrongValues(it.value().toObject())) cases.push_back({kind, with(base, it.key(), v)});
    cases.push_back({"unknown", with(base, "zzz", 1)});
    cases.push_back({"unknown-underscore", with(base, "_py", 1)});
    cases.push_back({"unknown-underscore-alone", with({}, "_", "x")});
    cases.push_back({"unknown-and-missing", with({}, "aaa", 1)});
    cases.push_back({"unknown-upper", with(base, "Zz", 1)});
    const QStringList names = properties.keys();
    if (names.size() >= 2) {
        const QString a = names.first(), z = names.last();
        cases.push_back({"two", with(with(base, z, QJsonValue()), a, "zz")});
        cases.push_back({"two-and-unknown", with(with(with({}, z, 2.5), a, QJsonArray{"q"}), "mmm", 1)});
        cases.push_back({"two-and-missing", with(with({}, z, true), "_ok", 1)});
    }
    return cases;
}

struct ParityRun {
    int schemas = 0, texts = 0, differences = 0, printed = 0;
    std::map<std::pair<std::string, std::string>, std::string> record; // (line, args as text) -> row; a map: the file's order is the key's
    std::set<std::pair<std::string, std::string>> seen;                // (line, kind of refusal): the record keeps the first of each
    bool recording = false;

    void differ(const std::string& what, const std::string& line, const std::string& qt, const std::string& core, const std::string& args = {}) {
        ++differences;
        if (printed++ < 12) std::printf("      %s: %s %s\n        Qt   %s\n        core %s\n", what.c_str(), escaped(line).c_str(), args.c_str(), escaped(qt).c_str(), escaped(core).c_str());
    }

    // The kinds of refusal a text holds: one record row for each kind on each line.
    static std::string kindsOf(const std::string& p) {
        std::string k;
        for (const auto& [needle, tag] : std::vector<std::pair<const char*, const char*>>{
                 {"аргументов нет", "A"}, {"нет поля", "B"}, {"нет обязательного", "C"}, {"числа", "D"}, {"в списке ожидалось", "E"}, {"не из списка", "F"}, {" вне ", "G"},
                 {"ожидалось целое", "H1"}, {"ожидалось число", "H2"}, {"ожидалось строка", "H3"}, {"ожидалось true/false", "H4"}, {"ожидалось список", "H5"}, {"ожидалось объект", "H6"}})
            if (p.find(needle) != std::string::npos) k += tag;
        return k;
    }

    void schema(const std::string& line, const QJsonObject& qtSchema, const Value& coreSchema) {
        ++schemas;
        const std::string qt = QJsonDocument(qtSchema).toJson(QJsonDocument::Compact).toStdString(), core = toJson(coreSchema);
        if (qt != core) differ("schema", line, qt, core);
    }

    void call(const std::string& line, const QJsonObject& qtSchema, const Value& coreSchema, const QJsonObject& args) {
        ++texts;
        const Value coreArgs = coreValue(args);
        const std::string qt = ::argsProblem(args, qtSchema).toStdString(), core = ke::argsProblem(coreArgs, coreSchema);
        const std::string text = toJson(coreArgs);
        if (qt != core) differ("problem", line, qt, core, text);
        if (recording && seen.insert({line, kindsOf(qt)}).second) {
            Value row = Value::map();
            row.set("line", line), row.set("args", coreArgs), row.set("problem", qt);
            record[{line, text}] = toJson(row);
        }
    }

    void line(const std::string& text, const std::vector<Case>& cases) {
        const QJsonObject qtSchema = ::argsSchema(QString::fromStdString(text));
        const Value coreSchema = ke::argsSchema(text);
        schema(text, qtSchema, coreSchema);
        for (const Case& c : cases) call(text, qtSchema, coreSchema, c.args);
    }

    void generated(const std::string& text) {
        std::vector<Case> cases = casesFor(::argsSchema(QString::fromStdString(text)));
        for (const char* fixed : {"{}", R"({"a": 1})", R"({"a": "x"})", R"({"a": null})", R"({"a": [1, 2, 3]})", R"({"a": 2.5, "b": "x"})", R"({"b": true, "a": [1]})", R"({"kind": "imu"})", R"({"ids": [1, 2]})"})
            cases.push_back({"fixed", QJsonDocument::fromJson(fixed).object()});
        line(text, cases);
    }
};

// Lines of the grammar the registry does not use, and the odd ones: spaces of every kind, brackets that do not close, quotes
// that do not end, a default with a point and nothing after it.
std::vector<std::string> grammarLines() {
    std::vector<std::string> l = {
        "", "{}", "{ }", "a: int", "{a: int}", "{ a : int , b ? : str }", "{a?: m (0.10), b?: s (-5), c?: number (3.), d?: x (1e5)}", "{a: 'x'|'y' (default)}", "{a?: 1|2|3}", "{a: 007|+5|-0}",
        "{a: id|b}", "{a: int|str}", "{a: number|float}", "{a: m|s}", "{a: 'x'|}", "{a: |x}", "{a: x|y|[x,y,z]}", "{a: [x,y,z], b: [x,y], c: [w,x,y,z], d: [v,w,x,y,z], e: [x]}", "{a: [int]}",
        "{a: [[3],[3]]}", "{a: [str]}", "{a: ['x'|'y']}", "{a: [1|2]}", "{a: [x,y,z}", "{a: [", "{a: ]x[}", "{a: {x: int}}", "{a: {}}", "{a?|b?}", "{face?|point?+normal?}", "{a: int | b: [int] | c: str}",
        "{a: int | b?: str}", "{a, b?, c}", "{a?: int} and b?: str", "{a: int}, c?: number (5)", "{a: int} x?: m", "{a: int} | {b: int} | {c: str}", "{a: int} | {a: str}", "{kind: 'a'|'b'} | {kind: 'b'|'c'}",
        "{kind: 1|2} | {kind: 2|3}", "{ids}", "{ids, ids2: [int]}", "{ids: int}", "{a: 'x'}", "{a: x}", "{a: 'x}", "{a: id}", "{a: the id of x}", "{a: valid idx}", "{a: ID}", "{a: INT}", "{a: Int}", "{a: M}",
        "{a: m/s}", "{a: N s}", "{a: str (it's)}", "{a: (5)}", "{a: int (-0.5)}", "{a: int ( 7 )}", "{a: int (7.)}", "{a: int (7) x}", "{a: int (1)(2)}", "{a: '+x'..'-z'}", "{a: tx..rz}", "{a: 'tx'..'rz'}",
        "{a: +x..-z}", "{a: x..z}", "{a: 'a'..'b'..'c'}", "{a: 'A'..'B'}", "{a: 1..3}", "{a: face '+x'..'-z'}", "{A: int}", "{a1: int, _b: int, 1a: int}", "{a-b: int}", "{a: int,, b: int}", "{a: int,}", "{,}", "{a: int}}",
        "{{a: int}}", "}{a: int}", "{a: int} {b: int}", "{a: int | | b: str}", "{a: int |}", "|", "{a: int}|", "|{a: int}", "{a: \xC3\xA9}", "{a: '\xC3\xA9'|'\xC3\xBC'}", "{\xC3\xA9: int}", "{a:\tint}", "{a: int\n}",
        "{a:\xC2\xA0int}", "{\xC2\xA0" "a: int}", "{a: int\xC2\xA0}", "{a: \xE2\x80\x83int}", "{a: int\xE3\x80\x80}", "{a: int}\t|\t{b: str}", "{a: int}\xC2\xA0|\xC2\xA0{b: str}", "{a: [\xC2\xA0x,y,z]}", "{a: x\xC2\xA0|\xC2\xA0y}",
        "{a: int (\xC2\xA0" "7\xC2\xA0)}", "{a: int\r\n, b: str}", "{a: \v int}", "{a: 'x' | 'y' | 'z'}", "{a: 'x' ..'y'}", "{a: 'x'.. 'y'}", "{a?: [int] (0)}", "{a: [x,y,z] (1)}", "{a: m (99999999999999999999)}",
        "{a: m (" + std::string(400, '9') + ")}", "{a: m (0." + std::string(400, '9') + ")}", "{a: m (1.5)}", "{a: m (-0)}", "{a: m (0.1000000000000000055511151231257827)}", "{a: 1|2147483647|2147483648}", "{a: -2147483648|-2147483649}",
        "{a: 1|x|3}", "{a: 'x'|2}", "{a: x|x}", "{a: x} | {a: x|y}", "{a: 'plus'}", "{a: 'plus' (1)}", "{a: 'a b'}", "{a: [number|str]}", "{a: [[x,y,z], ...]}", "{a: [x, y, z], ...}", "{a?: int} | {b?: int}",
        "{a: int | b: int} | {b: int | c: int}", "{a: int} | {b: int, a: int}", "{a?: int, a: str}", "{a: str, a?: int}", "{a: x|y, a: 'z'|'w'}", "{a: 'x'|'y', a: 3}", "{a: 1|2, a: 2|3}", "{ids?: int, ids: [int]}",
        "{a: int}, b?: str, c?: [x,y,z], d: int", "{a: int} b?: [int], c ?: m", "{a: int} e?:", "{a: int} ?: int", "{a: int} 9a?: int", "{a: int} a9?: int", "{a: int} _?: int", "{a: int} A?: int", "?", "??", "{a?b?: int}",
        "{a?: int | b?: str}", "{a?|b?: int}", "{a: int | b}", "{a: int | b c}", "{a: int | b:}", "{a: int |b?:int}", "{a: int|b:int}", "{a: int | 1b: int}", "{id: int, ids: [int], idx: int, a_id: int, b-id: int}", "{a: id 5}", "{a: 5 id}", "{a: _id}", "{a: id_}", "{a: ids}"};
    return l;
}

} // namespace

int runSchemaParityTests(EditorOps& ops) {
    Checker c;
    ParityRun run;
    run.recording = std::getenv("KE_RECORD") != nullptr && std::string(std::getenv("KE_RECORD")) == "1";

    std::vector<std::string> lines;
    for (const QJsonValue& o : ops.runNow("ops_list", {}).value("ops").toArray()) {
        const QJsonObject op = o.toObject();
        const std::string text = op.value("args").toString().toStdString();
        const QJsonObject own = op.value("schema").toObject();
        if (QJsonDocument(own).toJson(QJsonDocument::Compact).toStdString() != toJson(ke::argsSchema(text))) run.differ("ops_list schema", op.value("name").toString().toStdString(), "", text);
        lines.push_back(text);
    }
    const size_t registryLines = lines.size();
    for (const std::string& t : lines) run.generated(t);
    c.check(run.differences == 0 && registryLines > 100, "the core's schema of every operation's line of help in the live registry is the Qt copy's text, and the same refusals for the generated calls");
    std::printf("      %zu operations\n", registryLines);

    const int before = run.differences;
    for (const std::string& t : grammarLines()) run.generated(t);
    run.generated("{target: int, kind: 'imu'|'sonar'} | {joint: int, kind: 'wrench'|'effort'}");
    c.check(run.differences == before, "...and for the grammar's odd lines: spaces of every kind, brackets and quotes that do not close, defaults");

    // A schema the lines never make - bounds, which the check has and the grammar cannot yet write - checked on its own.
    const QJsonObject bounded{{"properties", QJsonObject{{"x", QJsonObject{{"type", "number"}, {"minimum", 0}, {"maximum", 1e-5}}}, {"n", QJsonObject{{"type", "integer"}, {"minimum", -2.5}}}}}, {"required", QJsonArray{"x"}}};
    const Value boundedCore = coreValue(bounded);
    const int beforeBounds = run.differences;
    const bool recording = std::exchange(run.recording, false); // not a line of the grammar: the test cannot replay it
    for (const double x : {-1.0, 0.0, 1e-6, 2.0, 1234567.0, 0.000123456789, 1e300, 100000.0, 999999.5})
        for (const double n : {-3.0, 5.0, 3.5}) run.call("bounded", bounded, boundedCore, QJsonObject{{"x", x}, {"n", n}});
    run.recording = recording;
    c.check(run.differences == beforeBounds, "a schema with bounds gives the same «вне» texts, numbers as %g");

    int extra = 0;
    if (const char* path = std::getenv("KE_SCHEMA_PARITY_EXTRA"); path && *path) {
        QFile file(path);
        const bool opened = file.open(QIODevice::ReadOnly);
        c.check(opened, "KE_SCHEMA_PARITY_EXTRA is readable");
        const int beforeExtra = run.differences;
        while (opened && !file.atEnd()) {
            const QJsonObject row = QJsonDocument::fromJson(file.readLine()).object();
            if (!row.contains("line")) continue;
            const std::string text = row.value("line").toString().toStdString();
            ++extra;
            if (row.value("generate").toBool()) run.generated(text);
            else run.line(text, {{"given", row.value("args").toObject()}});
        }
        c.check(run.differences == beforeExtra, "the extra lines of KE_SCHEMA_PARITY_EXTRA: the same schema text and the same refusals");
        std::printf("      %d extra lines\n", extra);
    }
    std::printf("      compared: %d schemas, %d refusal texts\n", run.schemas, run.texts);
    c.check(run.texts >= 2000, "at least 2000 refusal texts compared");

    if (run.recording) {
        std::string out = "[\n";
        for (const auto& [key, row] : run.record) out += (out.size() > 2 ? ",\n" : "") + row;
        out += "\n]\n";
        QFile file(QStringLiteral(KE_SOURCE_DIR) + "/tests/data/args-refusals.json");
        const bool written = file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(out.data(), qint64(out.size())) == qint64(out.size());
        std::printf("      recorded %zu rows, %zu bytes\n", run.record.size(), out.size());
        c.check(written && out.size() < 400 * 1024, "tests/data/args-refusals.json written, under 400 KB");
    }
    return c.failures;
}
