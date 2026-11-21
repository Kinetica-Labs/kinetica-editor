// The two hosts' JSON agree: the window's server speaks Qt's QJsonDocument, the windowless host speaks the
// core's ke::toJson / ke::parseJson (core/Json.h). This group (wire-parity) is the guard of that second copy:
// a corpus of values written by both to the same bytes, read by both to the same trees, a list of malformed
// texts refused by both or listed here as a difference, and a sweep of doubles and of byte strings. It
// needs no window. tests/JsonTests.cpp pins what it found, so the core holds the borders without Qt.
#include "SelfTestSupport.h"

#include "EditorOps.h"
#include "core/Format.h"
#include "core/Json.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>

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
