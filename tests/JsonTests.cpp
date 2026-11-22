// The core's JSON (core/Json.h, core/Format.h's numberTextShortest) checked without Qt: the expected texts are
// literal - they are what QJsonDocument::Compact writes, found by the window's group "wire-parity"
// (editor/SelfTestHosts.cpp), which keeps the two in step; this file keeps the borders where Qt is not around.
#include "TestRunner.h"

#include "core/Format.h"
#include "core/Json.h"

#include <clocale>
#include <cstring>
#include <limits>
#include <locale>
#include <stdexcept>

namespace {

std::string valid(const char* bytes) { return validUtf8(bytes); }

bool sameBits(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

std::string errorOf(const std::string& text) {
    Value v;
    std::string error;
    return parseJson(text, v, error) ? "" : error;
}

// A scope in which the C and the C++ locales write a comma for a decimal point (Qt does this to the window).
struct CommaLocale {
    std::string cLocale;
    std::locale global;
    bool active = false;
    CommaLocale() {
        const char* old = std::setlocale(LC_ALL, nullptr);
        cLocale = old ? old : "C";
        global = std::locale();
        if (!std::setlocale(LC_ALL, "ru_RU.utf8")) return;
        try {
            std::locale::global(std::locale("ru_RU.utf8"));
            active = true;
        } catch (const std::runtime_error&) {
            std::setlocale(LC_ALL, cLocale.c_str());
        }
    }
    ~CommaLocale() {
        std::setlocale(LC_ALL, cLocale.c_str());
        std::locale::global(global);
    }
};

} // namespace

void testJsonNumbers() {
    const struct { double v; const char* text; } cases[] = {
        {0.1, "0.1"}, {1.0 / 3, "0.3333333333333333"}, {-2.5, "-2.5"}, {123456789.125, "123456789.125"},
        {double(0.01f), "0.009999999776482582"}, {9.224468158208765e-06, "9.224468158208765e-06"},
        {5e-324, "5e-324"}, {1.7976931348623157e308, "1.7976931348623157e+308"},
        // Where the text turns to an exponent: the shorter of the two, a tie fixed (Qt's border, found by the parity group).
        {0.001, "0.001"}, {0.00012, "0.00012"}, {0.0001, "1e-04"}, {1e-5, "1e-05"}, {1e-6, "1e-06"}, {1e-7, "1e-07"},
        {2.5e-5, "2.5e-05"}, {0.00014999999999999999, "0.00015"}, {-0.0001, "-1e-04"},
        {1e15 + 0.5, "1000000000000000.5"}, {9007199254740994.0, "9007199254740994"}, {9.5e15, "9.5e+15"},
        {1e16, "1e+16"}, {2e16, "2e+16"}, {1e17, "1e+17"}, {1.2345678901234568e17, "123456789012345680"},
        {12345678901234567890.0, "12345678901234567000"}, {1e21, "1e+21"}, {1e22, "1e+22"}, {1e23, "1e+23"}, {-1e21, "-1e+21"},
        {0.0, "0"}, {-0.0, "0"}, {1.0, "1"}, {100.0, "100"}, {1e5, "1e+05"},
    };
    for (const auto& c : cases) {
        CHECK(numberTextShortest(c.v) == c.text, "%.17g: «%s», expected «%s»", c.v, numberTextShortest(c.v).c_str(), c.text);
        double back = 0;
        CHECK(parseNumber(numberTextShortest(c.v), back) && sameBits(back == 0 ? 0.0 : back, c.v == 0 ? 0.0 : c.v), "«%s» reads back", c.text);
    }
    {
        const CommaLocale comma;
        char probe[16];
        std::snprintf(probe, sizeof probe, "%g", 0.5);
        CHECK(!comma.active || std::strcmp(probe, "0,5") == 0, "the test's locale really writes a comma: «%s»", probe);
        CHECK(numberTextShortest(0.1) == "0.1" && numberTextShortest(1e-5) == "1e-05" && toJson(Value(2.5)) == "2.5", "the text of a number does not depend on the locale");
        CHECK(parseJson("[0.5,1e-5]").items()[0].number() == 0.5 && parseJson("[0.5,1e-5]").items()[1].number() == 1e-5,
              "the reader does not depend on the locale");
        double d = 0;
        float f = 0;
        CHECK(!comma.active || (!parseNumber("1,2", d) && !parseNumber("1,2", f) && parseNumber("1.25", d) && d == 1.25),
              "a word with the locale's comma is no number: the two hosts read a scene's words alike");
    }
}

void testJsonText() {
    std::string controls;
    for (int c = 0; c < 0x20; ++c) controls += char(c);
    CHECK(toJson(Value(controls)) == "\"\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007\\b\\t\\n\\u000b\\f\\r\\u000e\\u000f"
                                     "\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017\\u0018\\u0019\\u001a\\u001b\\u001c\\u001d\\u001e\\u001f\"",
          "every control character: the two-letter escapes, \\u00xx in lower case for the rest");
    CHECK(toJson(Value("\"\\/\x7f")) == "\"\\\"\\\\/\x7f\"", "the quote and the backslash are escaped, the slash and 0x7F are not");
    CHECK(toJson(Value("я\xF0\x9F\x98\x80\xE2\x80\xA8\xE2\x80\xA9")) == "\"я\xF0\x9F\x98\x80\xE2\x80\xA8\xE2\x80\xA9\"", "Cyrillic, an emoji, U+2028 and U+2029 pass through raw");
    // What QString::fromUtf8 gives, one U+FFFD per byte that is not in a well-formed sequence (found by the parity group).
    const std::string f = "\xEF\xBF\xBD";
    CHECK(valid("\xC3") == f, "a truncated two-byte sequence: one");
    CHECK(valid("\xC3\x28") == f + "(", "a lead and a byte that is not a continuation: one, the byte stays");
    CHECK(valid("\xE2\x82") == f + f, "a truncated three-byte sequence: one per byte");
    CHECK(valid("\xF0\x9F\x98") == f + f + f, "a truncated four-byte sequence: one per byte");
    CHECK(valid("\xE2\x82\x28") == f + f + "(", "a truncated sequence in the middle of a text");
    CHECK(valid("\xC0\x80") == f + f && valid("\xC1\xBF") == f + f, "an overlong two-byte form: one per byte");
    CHECK(valid("\xE0\x80\x80") == f + f + f && valid("\xF0\x80\x80\x80") == f + f + f + f, "an overlong longer form: one per byte");
    CHECK(valid("\xED\xA0\x80") == f + f + f, "a surrogate in UTF-8: one per byte");
    CHECK(valid("\xF4\x90\x80\x80") == f + f + f + f && valid("\xF5\x80\x80\x80") == f + f + f + f, "above U+10FFFF: one per byte");
    CHECK(valid("\xF8\x88\x80\x80\x80") == f + f + f + f + f && valid("\xFF") == f && valid("\x80") == f, "a byte that is no lead, a stray continuation");
    CHECK(valid("\xEF\xBF\xBF") == "\xEF\xBF\xBF" && valid("\xF4\x8F\xBF\xBF") == "\xF4\x8F\xBF\xBF" && valid("\xED\x9F\xBF") == "\xED\x9F\xBF",
          "U+FFFF, U+10FFFF and U+D7FF are well-formed");
    CHECK(valid("\xEF\xBB\xBF" "x") == "\xEF\xBB\xBF" "x", "a byte order mark is kept (Qt drops one at the start of a text)");
    CHECK(valid("a\0b") == "a" && validUtf8(std::string("a\0b", 3)).size() == 3, "a NUL is a character");
    CHECK(toJson(Value("\xFF" "x")) == "\"\xEF\xBF\xBD" "x\"", "the writer repairs what it is given");
    CHECK(parseJson("[\"\\u00e9\\u00E9\"]").items()[0].text() == "\xC3\xA9\xC3\xA9", "\\u escapes, either case");
    CHECK(parseJson("[\"\\ud83d\\ude00\"]").items()[0].text() == "\xF0\x9F\x98\x80", "a surrogate pair is one character");
    CHECK(parseJson("[\"\\ud800\"]").items()[0].text() == f && parseJson("[\"\\udc00\"]").items()[0].text() == f, "a lone surrogate is U+FFFD");
    CHECK(parseJson("[\"\\ud800\\u0041\"]").items()[0].text() == f + "A" && parseJson("[\"\\ud83d\\ude00\\ude00\"]").items()[0].text() == "\xF0\x9F\x98\x80" + f
              && parseJson("[\"\\ude00\\ud83d\"]").items()[0].text() == f + f && parseJson("[\"\\ud83dx\"]").items()[0].text() == f + "x",
          "a high surrogate without its low one, a low one alone");
    CHECK(parseJson("[\"\\b\\f\\n\\r\\t\\\\\\/\\\"\\u0000\"]").items()[0].text() == std::string("\b\f\n\r\t\\/\"\0", 9), "the escapes");
    CHECK(parseJson("[\"\\q\"]").items()[0].text() == "q" && parseJson("[\"a\tb\x01\"]").items()[0].text() == "a\tb\x01", "as Qt: an unknown escape is its character, a raw control character stays");
    CHECK(parseJson("[\"\xEF\xBF\xBF\xF4\x8F\xBF\xBF\"]").items()[0].text() == "\xEF\xBF\xBF\xF4\x8F\xBF\xBF", "raw well-formed UTF-8 is read as it is");
}

void testJsonWriter() {
    Value all = Value::map();
    all.set("zeta", Value());
    all.set("alpha", Value(true));
    all.set("é", Value("x"));
    all.set("Beta", Value(false));
    all.set("", Value(1));
    Value inner = Value::list();
    inner.push(Value::map());
    inner.push(Value::list());
    inner.push(Value(-1.5));
    all.set("list", inner);
    CHECK(toJson(all) == "{\"\":1,\"Beta\":false,\"alpha\":true,\"list\":[{},[],-1.5],\"zeta\":null,\"é\":\"x\"}", "keys in byte order, upper case before lower, a non-ASCII key last: %s", toJson(all).c_str());
    CHECK(toJson(Value()) == "null" && toJson(Value::list()) == "[]" && toJson(Value::map()) == "{}" && toJson(Value("")) == "\"\"", "the empty shapes");
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    CHECK(toJson(Value(nan)) == "null" && toJson(Value(inf)) == "null" && toJson(Value(-inf)) == "null", "NaN and the infinities are null");
    CHECK(toJson(Value(0)) == "0" && toJson(Value(-0.0)) == "0" && toJson(Value(2147483648.0)) == "2147483648" && toJson(Value(-1.0)) == "-1", "whole numbers");
    CHECK(toJson(Value(9007199254740991.0)) == "9007199254740991" && toJson(Value(9007199254740992.0)) == "9007199254740992" && toJson(Value(9e15)) == "9000000000000000"
              && toJson(Value(1e15)) == "1000000000000000", "whole numbers up to 2^53 are integers, 1e15 too");
    CHECK(toJson(Value(1e16)) == "1e+16" && toJson(Value(1e21)) == "1e+21" && toJson(Value(int64_t(1) << 60)) == "1152921504606847000", "above 2^53 the shortest text");
    CHECK(toJson(Value(1e-5)) == "1e-05" && toJson(Value(0.1)) == "0.1", "fractions");
    Value deep = Value::list();
    Value* at = &deep;
    for (int k = 0; k < 50; ++k) at = &at->push(Value::list());
    CHECK(toJson(deep) == std::string(51, '[') + std::string(51, ']'), "depth 50");
    Value twin = Value::map();
    twin.set("\xFF", Value(1));
    twin.set("\xFE", Value(2));
    CHECK(toJson(twin) == "{\"\xEF\xBF\xBD\":2}", "two keys that repair to one: the later wins");
}

void testJsonReader() {
    const Value v = parseJson(" {\"b\" : [1, 2.5, -0, 2.0, 9e15, 8999999999999999, 1e300, true, false, null, \"\"], \"a\":{}, \"a\":{\"x\":1} , \"é\":0.1e1}\n");
    CHECK(v.keys().size() == 3 && v.keys()[0] == "a" && v.keys()[1] == "b" && v.keys()[2] == "é", "members enter in key order");
    CHECK(v["a"].size() == 1 && v["a"]["x"].integer() == 1, "the last of a repeated key wins");
    const auto& b = v["b"].items();
    CHECK(b.size() == 11, "eleven items");
    CHECK(b[0].whole() && b[0].number() == 1, "1 is whole");
    CHECK(!b[1].whole() && b[1].number() == 2.5, "2.5 is not");
    CHECK(b[2].whole() && b[2].number() == 0 && !std::signbit(b[2].number()), "-0 is the whole number 0");
    CHECK(b[3].whole() && b[3].number() == 2, "2.0 is whole");
    CHECK(!b[4].whole() && b[4].number() == 9e15, "9e15 is not (the window's border: below 9e15)");
    CHECK(b[5].whole() && b[5].number() == 8999999999999999.0, "just below it is");
    CHECK(!b[6].whole() && b[6].number() == 1e300, "1e300");
    CHECK(b[7].truth() && !b[8].truth(true) && b[8].type() == Value::Type::Bool && b[9].isNull() && b[10].type() == Value::Type::Text && b[10].text().empty(), "the literals and the empty text");
    CHECK(v["é"].number() == 1 && v["é"].whole(), "0.1e1 is whole 1");
    CHECK(parseJson("[5e-324]").items()[0].number() == 5e-324 && parseJson("[1.7976931348623157e308]").items()[0].number() == 1.7976931348623157e308, "the extremes of a double");
    CHECK(parseJson("[.5,1.,-.5,1.e1]").items()[0].number() == 0.5 && parseJson("[1.]").items()[0].whole() && parseJson("[-.5]").items()[0].number() == -0.5 && parseJson("[1.e1]").items()[0].number() == 10, "as Qt: a mantissa may lack its integer or its fraction digits");
    CHECK(sameBits(parseJson("[9007199254740993]").items()[0].number(), 9007199254740992.0), "an integer beyond 2^53 rounds as a double does");
    CHECK(toJson(parseJson(toJson(v))) == toJson(v), "write, read, write: the same text");
    CHECK(parseJson("[]").size() == 0 && parseJson("{}").type() == Value::Type::Map && parseJson(" [ ] ").type() == Value::Type::List, "the empty shapes");
    CHECK(parseJson("7").integer() == 7 && parseJson("\"x\"").text() == "x" && parseJson("null").isNull(), "any value is a document (the host splits objects from the rest)");
    const auto nest = [](int n) { return std::string(size_t(n), '[') + std::string(size_t(n), ']'); };
    CHECK(errorOf(nest(200)).empty() && errorOf(nest(201)) == "не JSON: слишком глубокая вложенность (знак 200)", "depth 200 is read, 201 is not: %s", errorOf(nest(201)).c_str());
    std::string objects;
    for (int k = 0; k < 200; ++k) objects += "{\"a\":";
    CHECK(errorOf(objects + "1" + std::string(200, '}')).empty() && !errorOf("{\"a\":" + objects + "1" + std::string(201, '}')).empty(), "the same for objects");
    bool threw = false;
    try {
        parseJson("[");
    } catch (const std::invalid_argument& e) {
        threw = std::string(e.what()) == "не JSON: ожидалось значение (знак 1)";
    }
    CHECK(threw, "the throwing form throws the error text");
}

void testJsonErrors() {
    const struct { const char* text; const char* error; } cases[] = {
        {"", "не JSON: ожидалось значение (знак 0)"},
        {"   ", "не JSON: ожидалось значение (знак 3)"},
        {"[1,]", "не JSON: ожидалось значение (знак 3)"},
        {"[,1]", "не JSON: ожидалось значение (знак 1)"},
        {"{\"a\":1,}", "не JSON: ожидалось имя поля (знак 7)"},
        {"{1:2}", "не JSON: ожидалось имя поля (знак 1)"},
        {"{\"a\" 1}", "не JSON: ожидалось двоеточие (знак 5)"},
        {"{\"a\":}", "не JSON: ожидалось значение (знак 5)"},
        {"[1 2]", "не JSON: ожидалась запятая или ] (знак 3)"},
        {"{\"a\":1 \"b\":2}", "не JSON: ожидалась запятая или } (знак 7)"},
        {"[1] x", "не JSON: лишний текст после значения (знак 4)"},
        {"[1][2]", "не JSON: лишний текст после значения (знак 3)"},
        {"nul", "не JSON: ожидалось значение (знак 0)"},
        {"[True]", "не JSON: ожидалось значение (знак 1)"},
        {"[truex]", "не JSON: ожидалась запятая или ] (знак 5)"},
        {"[\"a", "не JSON: незакрытая строка (знак 3)"},
        {"[\"a\\", "не JSON: незакрытая строка (знак 4)"},
        {"[01]", "не JSON: ожидалась запятая или ] (знак 2)"},
        {"[-]", "не JSON: неверное число (знак 1)"},
        {"[.]", "не JSON: неверное число (знак 1)"},
        {"[+1]", "не JSON: ожидалось значение (знак 1)"},
        {"[1e]", "не JSON: неверное число (знак 1)"},
        {"[1e+]", "не JSON: неверное число (знак 1)"},
        {"[1e999]", "не JSON: число вне диапазона (знак 1)"},
        {"[-1e999]", "не JSON: число вне диапазона (знак 1)"},
        {"[1e-999]", "не JSON: число вне диапазона (знак 1)"},
        {"[2e-324]", "не JSON: число вне диапазона (знак 1)"},
        {"[\"\\u12\"]", "не JSON: неверная escape-последовательность (знак 6)"},
        {"[\"\\u00g0\"]", "не JSON: неверная escape-последовательность (знак 6)"},
        {"[\"\xC3\"]", "не JSON: неверный UTF-8 (знак 2)"},
        {"[\"\\\xC3\xA9\"]", "не JSON: неверный UTF-8 (знак 3)"},
        {"\xEF\xBB\xBF[1]", "не JSON: ожидалось значение (знак 0)"},
        {"/*c*/[1]", "не JSON: ожидалось значение (знак 0)"},
        {"[\x0b" "1]", "не JSON: ожидалось значение (знак 1)"},
    };
    for (const auto& c : cases) CHECK(errorOf(c.text) == c.error, "«%s»: «%s», expected «%s»", c.text, errorOf(c.text).c_str(), c.error);
    CHECK(errorOf("[4e-324]").empty(), "a denormal is a number");
}
