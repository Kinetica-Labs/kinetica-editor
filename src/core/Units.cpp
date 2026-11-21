// Physical units as data (Units.h): the atoms and prefixes of the UCUM subset, the parser and the words.
#include "core/Units.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace ke {

namespace {

const double kPiUnits = 3.14159265358979323846;

// An atom: its code, its scale to SI, its dimension twice (L M T I O N J), its Russian words, and whether it takes a prefix.
struct Atom {
    const char* code;
    double scale;
    int8_t twice[7];
    const char* text;
    bool prefixed;
};
const Atom kAtoms[] = {
    {"m", 1, {2, 0, 0, 0, 0, 0, 0}, "м", true},          {"g", 1e-3, {0, 2, 0, 0, 0, 0, 0}, "г", true},
    {"s", 1, {0, 0, 2, 0, 0, 0, 0}, "с", true},          {"A", 1, {0, 0, 0, 2, 0, 0, 0}, "А", true},
    {"K", 1, {0, 0, 0, 0, 2, 0, 0}, "К", true},          {"mol", 1, {0, 0, 0, 0, 0, 2, 0}, "моль", true},
    {"cd", 1, {0, 0, 0, 0, 0, 0, 2}, "кд", true},        {"rad", 1, {}, "рад", true},
    {"deg", kPiUnits / 180, {}, "°", false},             {"%", 0.01, {}, "%", false},
    {"bit", 1, {}, "бит", false},                        {"Hz", 1, {0, 0, -2, 0, 0, 0, 0}, "Гц", true},
    {"N", 1, {2, 2, -4, 0, 0, 0, 0}, "Н", true},         {"Pa", 1, {-2, 2, -4, 0, 0, 0, 0}, "Па", true},
    {"J", 1, {4, 2, -4, 0, 0, 0, 0}, "Дж", true},        {"W", 1, {4, 2, -6, 0, 0, 0, 0}, "Вт", true},
    {"V", 1, {4, 2, -6, -2, 0, 0, 0}, "В", true},        {"Ohm", 1, {4, 2, -6, -4, 0, 0, 0}, "Ом", true},
    {"C", 1, {0, 0, 2, 2, 0, 0, 0}, "Кл", true},         {"min", 60, {0, 0, 2, 0, 0, 0, 0}, "мин", false},
    {"h", 3600, {0, 0, 2, 0, 0, 0, 0}, "ч", false},      {"[g]", 9.80665, {2, 0, -4, 0, 0, 0, 0}, "g", true},
    {"[rtHz]", 1, {0, 0, -1, 0, 0, 0, 0}, "√Гц", false}, {"1", 1, {}, "", false}};

struct Prefix {
    char code;
    double scale;
    const char* text;
};
const Prefix kPrefixes[] = {{'G', 1e9, "Г"}, {'M', 1e6, "М"}, {'k', 1e3, "к"}, {'c', 1e-2, "с"},
                            {'m', 1e-3, "м"}, {'u', 1e-6, "мк"}, {'n', 1e-9, "н"}, {'p', 1e-12, "п"}};

// One component: [prefix] atom [exponent]; the atom alone tried first ("m" a metre, "min" a minute, "mm" a millimetre).
struct Component {
    const Atom* atom = nullptr;
    const Prefix* prefix = nullptr;
    int exponent = 1;
};

bool component(const std::string& word, Component& c) {
    size_t digits = word.size(); // a signed integer at the end is the exponent; all digits: the atom "1"
    while (digits > 0 && std::isdigit(static_cast<unsigned char>(word[digits - 1]))) --digits;
    if (digits > 0 && digits < word.size() && word[digits - 1] == '-') --digits;
    if (digits == 0) digits = word.size();
    const std::string name = word.substr(0, digits);
    c.exponent = digits < word.size() ? std::atoi(word.c_str() + digits) : 1;
    if (c.exponent == 0) return false;
    for (const Atom& a : kAtoms)
        if (name == a.code) return c.atom = &a, true;
    for (const Prefix& p : kPrefixes)
        if (name.size() > 1 && name[0] == p.code)
            for (const Atom& a : kAtoms)
                if (a.prefixed && name.compare(1, std::string::npos, a.code) == 0) return c.prefix = &p, c.atom = &a, true;
    return false;
}

// The code's components with their signs: a / turns the next one's exponent over, read left to right.
template <class Each> bool components(const std::string& code, Each each, std::string& bad) {
    size_t i = 0;
    int sign = 1;
    if (!code.empty() && code[0] == '/') sign = -1, i = 1;
    while (i <= code.size()) {
        const size_t end = code.find_first_of("./", i);
        const std::string word = code.substr(i, end == std::string::npos ? std::string::npos : end - i);
        Component c;
        if (word.empty() || !component(word, c)) return bad = word, false;
        each(c, sign);
        if (end == std::string::npos) break;
        sign = code[end] == '/' ? -1 : 1, i = end + 1;
    }
    return true;
}

} // namespace

bool parseUnit(const std::string& code, Unit& out, std::string& error) {
    out = Unit();
    if (code.empty()) return true;
    std::string bad;
    const bool ok = components(code, [&out](const Component& c, int sign) {
        const int e = sign * c.exponent;
        out.scale *= std::pow((c.prefix ? c.prefix->scale : 1) * c.atom->scale, e);
        for (int k = 0; k < 7; ++k) out.dimension.twice[size_t(k)] = int8_t(out.dimension.twice[size_t(k)] + e * c.atom->twice[k]);
    }, bad);
    if (!ok) error = "unit «" + code + "»: «" + bad + "» is not a unit of the subset (Units.h)";
    return ok;
}

std::string unitText(const std::string& code) {
    std::string out, bad;
    const char* superscripts[] = {"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
    bool first = true;
    const bool ok = components(code, [&](const Component& c, int sign) {
        if (!first || sign < 0) out += sign < 0 ? "/" : "·";
        first = false;
        out += std::string(c.prefix ? c.prefix->text : "") + c.atom->text;
        const int e = std::abs(c.exponent);
        if (c.exponent < 0) out += "⁻";
        if (e != 1) out += superscripts[std::min(e, 9)];
    }, bad);
    return ok ? out : code;
}

std::string dimensionText(const Dimension& d) {
    const char* base[] = {"m", "kg", "s", "A", "K", "mol", "cd"};
    std::string out;
    for (int k = 0; k < 7; ++k) {
        const int t = d.twice[size_t(k)];
        if (t == 0) continue;
        out += (out.empty() ? "" : ".") + std::string(base[k]) + (t == 2 ? "" : t % 2 ? std::to_string(t) + "/2" : std::to_string(t / 2));
    }
    return out.empty() ? "1" : out;
}

} // namespace ke
