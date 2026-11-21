// An operation's line of arguments read as JSON Schema, and a call checked against it (ArgsSchema.h: the grammar).
// One cursor over the line instead of regular expressions: no std::regex, nothing that depends on a locale.
// Text is bytes: every character the grammar looks at is ASCII, whitespace included.
#include "scene/ArgsSchema.h"

#include "core/Format.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <set>
#include <sstream>

namespace ke {

namespace {

using Strings = std::vector<std::string>;

bool isSpace(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); } // PCRE's \s
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isLower(char c) { return c >= 'a' && c <= 'z'; }
bool isWord(char c) { return isLower(c) || isDigit(c) || c == '_' || (c >= 'A' && c <= 'Z'); }
bool isNameRest(char c) { return isLower(c) || isDigit(c) || c == '_'; }
bool isWordish(char c) { return isWord(c) || c == '+' || c == '-'; }

struct Scan {
    const std::string& s;
    size_t i = 0;
    bool end() const { return i >= s.size(); }
    char peek() const { return end() ? '\0' : s[i]; }
    bool eat(char c) { return !end() && s[i] == c ? ++i, true : false; }
    bool eat(const char* lit) {
        const std::string l(lit);
        return s.compare(i, l.size(), l) == 0 ? i += l.size(), true : false;
    }
    void ws() { while (!end() && isSpace(s[i])) ++i; }
    size_t run(bool (*in)(char)) {
        const size_t from = i;
        while (!end() && in(s[i])) ++i;
        return i - from;
    }
};

std::string trim(const std::string& s) {
    size_t from = 0, to = s.size();
    while (from < to && isSpace(s[from])) ++from;
    while (to > from && isSpace(s[to - 1])) --to;
    return s.substr(from, to - from);
}

std::string lower(std::string s) {
    for (char& c : s) c = c >= 'A' && c <= 'Z' ? char(c + 32) : c;
    return s;
}

// QString::number(double): %g with six digits, whatever the program's locale.
std::string numberG(double d) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(6) << d;
    return out.str();
}

Value textList(const Strings& words) {
    Value l = Value::list();
    for (const std::string& w : words) l.push(w);
    return l;
}

Value onlyType(const char* type) {
    Value v = Value::map();
    v.set("type", type);
    return v;
}

Value without(const Value& v, const char* key) {
    Value out = Value::map();
    for (const std::string& k : v.keys())
        if (k != key) out.set(k, v[k]);
    return out;
}

bool sameJson(const Value& a, const Value& b) {
    if (a.type() != b.type()) return false;
    switch (a.type()) {
    case Value::Type::Null: return true;
    case Value::Type::Bool: return a.truth() == b.truth();
    case Value::Type::Number: return a.number() == b.number();
    case Value::Type::Text: return a.text() == b.text();
    default: return false;
    }
}

bool listHas(const Value& list, const Value& v) {
    return std::any_of(list.items().begin(), list.items().end(), [&](const Value& x) { return sameJson(x, v); });
}

// A key present, null or not (QJsonObject::contains); Value::has says "present and not null".
bool present(const Value& map, const std::string& key) {
    return map.type() == Value::Type::Map && std::find(map.keys().begin(), map.keys().end(), key) != map.keys().end();
}

Strings sortedKeys(const Value& map) {
    Strings k = map.type() == Value::Type::Map ? map.keys() : Strings();
    std::sort(k.begin(), k.end());
    return k;
}

// The pieces of `text` between `separator`s that lie outside every bracket.
Strings topLevel(const std::string& text, char separator) {
    Strings out;
    std::string piece;
    int depth = 0;
    for (const char c : text) {
        if (c == '[' || c == '{' || c == '(') ++depth;
        else if (c == ']' || c == '}' || c == ')') --depth;
        if (c == separator && depth == 0) out.push_back(piece), piece.clear();
        else piece += c;
    }
    out.push_back(piece);
    return out;
}

// The line's outermost {…} groups (a call takes one of them: "{target…} | {joint…}"), and the words outside them.
void groupsOf(const std::string& line, Strings& groups, std::string& outside) {
    std::string piece;
    int depth = 0, inner = 0; // a group's own depth; inner: a field's {…} in the words outside
    for (const char c : line) {
        if (c == '{' && depth == 0 && inner == 0) {
            const std::string before = trim(outside);
            if (before.empty() || before.back() == '|') {
                depth = 1, piece.clear();
                continue;
            }
        }
        if (depth > 0) {
            if (c == '{') ++depth;
            if (c == '}' && --depth == 0) {
                groups.push_back(piece);
                continue;
            }
            piece += c;
            continue;
        }
        inner += c == '{' ? 1 : c == '}' ? -1 : 0;
        outside += c;
    }
}

// 'word' or word, nothing else: the word without its quotes.
bool wordOf(const std::string& s, std::string& out) {
    Scan c{s};
    c.eat('\'');
    const size_t from = c.i;
    if (!c.run(isWordish)) return false;
    out = s.substr(from, c.i - from);
    c.eat('\'');
    return c.end();
}

bool unitWord(const std::string& w) {
    static const std::set<std::string> units{"m", "s", "rad", "degrees", "deg", "px", "kg", "n", "hz", "v", "a", "ohm", "m/s", "lines", "number", "float"};
    return units.count(lower(w)) > 0;
}

bool inList(const Strings& l, const char* w) { return std::find(l.begin(), l.end(), w) != l.end(); }

// A whole type word, or a list of letters, or words between '|' and '..': what a type says by itself, so that its words
// need not stay as the description.
bool plainType(const std::string& raw) {
    size_t from = 0, to = raw.size();
    while (from < to && isSpace(raw[from])) ++from;
    while (to > from && isSpace(raw[to - 1])) --to;
    const std::string u = raw.substr(from, to - from);
    static const std::set<std::string> single{"int", "integer", "number", "str", "string", "bool", "boolean"};
    if (single.count(u)) return true;
    if (u.size() >= 3 && u.front() == '[' && u.back() == ']' &&
        std::all_of(u.begin() + 1, u.end() - 1, [](char c) { return isLower(c) || c == ',' || c == ' '; }))
        return true;
    Scan c{u};
    int words = 0;
    for (;;) {
        c.eat('\'');
        if (!c.run(isWordish)) return false;
        c.eat('\'');
        ++words;
        c.ws();
        if (c.end()) return words > 1;
        if (!c.eat('|') && !c.eat("..")) return false;
        c.ws();
    }
}

// "(0.10)" at the end of a type's words: where it opens, and the number.
bool defaultOf(const std::string& t, size_t& open, double& number) {
    if (t.empty() || t.back() != ')') return false;
    open = t.rfind('(', t.size() - 1);
    if (open == std::string::npos) return false;
    const std::string inner = t.substr(open + 1, t.size() - open - 2);
    Scan c{inner};
    c.ws();
    const size_t from = c.i;
    c.eat('-');
    if (!c.run(isDigit)) return false;
    if (c.eat('.') && !c.run(isDigit)) return false;
    const std::string word = inner.substr(from, c.i - from);
    c.ws();
    if (!c.end()) return false;
    if (!parseNumber(word, number)) number = word[0] == '-' ? -HUGE_VAL : HUGE_VAL; // only a number too big to read fails; QString::toDouble says infinity
    return true;
}

// 'a'..'b' or +a..-b between two optional quotes and signs: whether the first has a sign (the faces) or not (the freedoms).
bool rangeOf(const std::string& t, bool& faces) {
    Scan c{t};
    c.eat('\'');
    faces = c.eat('+') || c.eat('-');
    if (!c.run(isLower)) return false;
    c.eat('\'');
    if (!c.eat("..")) return false;
    c.eat('\'');
    if (!c.eat('+')) c.eat('-');
    if (!c.run(isLower)) return false;
    c.eat('\'');
    return c.end();
}

// As QString::toInt reads: a sign and digits, the whole of an int.
bool toInt(const std::string& w, int& out) {
    Scan c{w};
    const bool negative = c.peek() == '-';
    if (c.peek() == '-' || c.peek() == '+') ++c.i;
    const size_t from = c.i;
    if (!c.run(isDigit) || !c.end()) return false;
    long long v = 0;
    for (size_t k = from; k < w.size(); ++k) {
        v = v * 10 + (w[k] - '0');
        if (v > 2147483648LL) return false;
    }
    v = negative ? -v : v;
    if (v > std::numeric_limits<int>::max() || v < std::numeric_limits<int>::min()) return false;
    return out = int(v), true;
}

bool hasIdWord(const std::string& t) {
    for (size_t at = t.find("id"); at != std::string::npos; at = t.find("id", at + 1))
        if ((at == 0 || !isWord(t[at - 1])) && (at + 2 >= t.size() || !isWord(t[at + 2]))) return true;
    return false;
}

Value typed(std::string t);

// A field's type from its words after the colon (the words kept as its description: units, defaults, what it means).
Value typeOf(const std::string& t) {
    Value p = typed(t);
    // The words stay as the description only where they say more than the type: a unit, a default, a meaning.
    return p.has("type") && plainType(t) ? without(p, "description") : p;
}

Value typed(std::string t) {
    t = trim(t);
    Value p = Value::map();
    if (t.empty()) return p;
    p.set("description", t);
    size_t open = 0;
    double d = 0;
    if (defaultOf(t, open, d)) p.set("default", d), t = trim(t.substr(0, open));
    bool faces = false;
    if (rangeOf(t, faces)) { // '+x'..'-z': the six faces; 'tx'..'rz': the six freedoms
        p.set("type", "string");
        p.set("enum", textList(faces ? Strings{"+x", "-x", "+y", "-y", "+z", "-z"} : Strings{"tx", "ty", "tz", "rx", "ry", "rz"}));
        return p;
    }
    const Strings either = topLevel(t, '|');
    if (either.size() > 1) {
        Strings words;
        for (const std::string& e : either)
            if (std::string w; wordOf(trim(e), w)) words.push_back(w);
        if (words.size() != either.size()) { // words and other shapes: any of them ("x|y|z|[x,y,z]", "[x,y,z] | number")
            Value any = Value::list();
            Strings plain;
            for (const std::string& e : either) {
                std::string w;
                if (wordOf(trim(e), w) && !unitWord(w)) {
                    plain.push_back(w);
                    continue;
                }
                const Value one = typeOf(e);
                if (!one.has("type")) return p;
                any.push(one);
            }
            if (!plain.empty()) {
                Value strings = onlyType("string");
                strings.set("enum", textList(plain));
                any.push(strings);
            }
            return p.set("anyOf", any), p;
        }
        if (std::all_of(words.begin(), words.end(), unitWord)) return p.set("type", "number"), p;
        static const std::set<std::string> types{"int", "integer", "number", "float", "str", "string", "bool", "boolean"};
        if (std::all_of(words.begin(), words.end(), [](const std::string& w) { return types.count(w) > 0; })) { // "number|str": either type
            Value any = Value::list();
            for (const std::string& w : words) any.push(typed(w));
            return p.set("anyOf", any), p;
        }
        if (inList(words, "id")) return p.set("type", "integer"), p;
        bool numbers = true;
        Value values = Value::list();
        for (const std::string& w : words) {
            int v = 0;
            const bool isInt = toInt(w, v);
            numbers = numbers && isInt;
            values.push(isInt ? Value(v) : Value(w));
        }
        if (!numbers) { // words: the list as strings
            values = textList(words);
            p.set("type", "string");
        } else {
            p.set("type", "integer");
        }
        p.set("enum", values);
        return p;
    }
    if (t[0] == '[') {
        p.set("type", "array");
        const size_t close = t.rfind(']');
        const std::string inner = trim(close == std::string::npos ? t.substr(1) : t.substr(1, close - 1));
        const Strings items = topLevel(inner, ',');
        const auto letters = [](const std::string& x) {
            Scan c{x};
            c.ws();
            if (!c.run(isLower)) return false;
            c.ws();
            return c.end();
        };
        if (inner == "int") {
            p.set("items", onlyType("integer"));
        } else if (!inner.empty() && inner[0] == '[') {
            p.set("items", onlyType("array"));
        } else if (items.size() >= 2 && items.size() <= 4 && std::all_of(items.begin(), items.end(), letters)) {
            p.set("items", onlyType("number")), p.set("minItems", int(items.size())), p.set("maxItems", int(items.size()));
        } else if (items.size() == 1) {
            const Value one = typeOf(inner);
            if (one.has("enum")) {
                Value item = Value::map();
                item.set("type", one["type"]), item.set("enum", one["enum"]);
                p.set("items", item);
            }
        }
        return p;
    }
    if (t[0] == '{') return p.set("type", "object"), p;
    const std::string first = lower(t.substr(0, t.find(' ')));
    std::string w;
    if (first == "int" || first == "integer" || hasIdWord(t)) p.set("type", "integer");
    else if (first == "str" || first == "string") p.set("type", "string");
    else if (first == "bool" || first == "boolean") p.set("type", "boolean");
    else if (unitWord(first)) p.set("type", "number");
    else if (t[0] == '\'' && wordOf(t, w)) p.set("type", "string"), p.set("enum", textList({w}));
    return p;
}

struct Field {
    std::string name;
    bool required = false;
    Value type = Value::map();
};

// Every "name?" of `s` - its name and where it ends (after the colon that follows it, when `colon`) - in order.
std::vector<std::pair<std::string, size_t>> optionalNames(const std::string& s, bool colon) {
    std::vector<std::pair<std::string, size_t>> out;
    for (size_t q = s.find('?'); q != std::string::npos; q = s.find('?', q + 1)) {
        size_t from = q;
        while (from > 0 && isNameRest(s[from - 1])) --from;
        while (from < q && isDigit(s[from])) ++from; // a name starts with a letter or '_'
        if (from == q) continue;
        size_t end = q + 1;
        if (colon) {
            while (end < s.size() && isSpace(s[end])) ++end;
            if (end >= s.size() || s[end] != ':') continue;
            ++end;
        }
        out.emplace_back(s.substr(from, q - from), end);
    }
    return out;
}

// name, then '?', then spaces, then ':' (or the end when `colon` is false): where the name's own part ends.
bool nameHead(Scan& c, std::string& name, bool& optional) {
    c.ws();
    if (!(isLower(c.peek()) || c.peek() == '_')) return false;
    const size_t from = c.i++;
    c.run(isNameRest);
    name = c.s.substr(from, c.i - from);
    optional = c.eat('?');
    c.ws();
    return true;
}

// One piece between commas: "name?: type", "a: int | b: [int]" (either, neither required), "face?|point?+normal?".
void fieldsOf(const std::string& piece, std::vector<Field>& out, bool inGroup) {
    Strings alternatives;
    std::string part;
    int depth = 0;
    for (size_t i = 0; i < piece.size(); ++i) { // split on a '|' that starts another field
        const char c = piece[i];
        if (c == '[' || c == '{' || c == '(') ++depth;
        else if (c == ']' || c == '}' || c == ')') --depth;
        bool another = false;
        if (c == '|' && depth == 0) {
            const std::string rest = piece.substr(i + 1);
            Scan n{rest};
            std::string name;
            bool optional;
            another = nameHead(n, name, optional) && n.peek() == ':';
        }
        if (another) alternatives.push_back(part), part.clear();
        else part += c;
    }
    alternatives.push_back(part);
    for (const std::string& a : alternatives) {
        Scan c{a};
        std::string name;
        bool optional;
        if (nameHead(c, name, optional) && (c.end() || c.peek() == ':')) {
            out.push_back({name, inGroup && alternatives.size() == 1 && !optional && name != "ids", typeOf(c.end() ? std::string() : a.substr(c.i + 1))});
            continue;
        }
        for (const auto& [n, end] : optionalNames(a, false)) out.push_back({n, false, Value::map()});
    }
}

std::string typeName(const std::string& t) {
    return t == "integer" ? "целое" : t == "number" ? "число" : t == "string" ? "строка" : t == "boolean" ? "true/false" : t == "array" ? "список" : t == "object" ? "объект" : t;
}

bool fits(const Value& v, const std::string& type) {
    using T = Value::Type;
    return type == "boolean" ? v.type() == T::Bool : type == "string" ? v.type() == T::Text : type == "array" ? v.type() == T::List : type == "object" ? v.type() == T::Map
         : type == "integer" ? v.type() == T::Number && v.number() == std::floor(v.number()) : type == "number" ? v.type() == T::Number : true;
}

std::string wordOrNumber(const Value& v) { return v.type() == Value::Type::Text ? v.text() : numberG(v.number()); }

} // namespace

Value argsSchema(const std::string& argsLine) {
    Strings groups;
    std::string outside;
    groupsOf(argsLine, groups, outside);
    Value properties = Value::map();
    std::set<std::string> required;
    bool first = true;
    for (const std::string& g : groups) {
        std::vector<Field> fields;
        for (const std::string& piece : topLevel(g, ',')) fieldsOf(piece, fields, true);
        std::set<std::string> here;
        for (const Field& f : fields) {
            Value known = properties[f.name];
            if (known.size() == 0) {
                properties.set(f.name, f.type);
            } else if (known.has("enum") && f.type.has("enum")) { // a word of either group: kind 'imu'|… | 'wrench'|…
                Value words = known["enum"];
                for (const Value& w : f.type["enum"].items())
                    if (!listHas(words, w)) words.push(w);
                known.set("enum", words), properties.set(f.name, known);
            }
            if (f.required) here.insert(f.name);
        }
        if (first) {
            required = here;
        } else { // either group: required only what both require
            std::set<std::string> both;
            std::set_intersection(required.begin(), required.end(), here.begin(), here.end(), std::inserter(both, both.begin()));
            required = both;
        }
        first = false;
    }
    for (const std::string& piece : topLevel(outside, ',')) // fields said in the words after the groups
        if (const auto named = optionalNames(piece, true); !named.empty() && !present(properties, named[0].first))
            properties.set(named[0].first, typeOf(piece.substr(named[0].second)));
    if (present(properties, "ids") && properties["ids"].size() == 0) {
        Value ids = onlyType("array");
        ids.set("items", onlyType("integer")), ids.set("description", "ids; none: the selection");
        properties.set("ids", ids);
    }
    Value schema = onlyType("object");
    schema.set("properties", properties), schema.set("additionalProperties", false);
    if (!required.empty()) schema.set("required", textList(Strings(required.begin(), required.end())));
    return schema;
}

namespace {

// What is wrong with one field's value against the schema (empty: nothing).
std::string fieldProblem(const std::string& key, const Value& v, const Value& properties) {
    if (!key.empty() && key[0] == '_') return {};
    if (!present(properties, key)) {
        if (properties.size() == 0) return "аргументов нет, а дано «" + key + "»";
        std::string names;
        for (const std::string& k : sortedKeys(properties)) names += (names.empty() ? "" : ", ") + k;
        return "нет поля «" + key + "»; поля: " + names;
    }
    const Value& f = properties[key];
    const std::string type = f["type"].text("");
    if (!fits(v, type)) return key + ": ожидалось " + typeName(type) + (present(f, "description") ? " (" + f["description"].text("") + ")" : std::string());
    if (present(f, "enum") && !listHas(f["enum"], v)) {
        std::string allowed;
        for (const Value& e : f["enum"].items()) allowed += (allowed.empty() ? "" : ", ") + wordOrNumber(e);
        return key + ": «" + wordOrNumber(v) + "» не из списка: " + allowed;
    }
    const bool low = present(f, "minimum"), high = present(f, "maximum");
    if (v.type() == Value::Type::Number && ((low && v.number() < f["minimum"].number()) || (high && v.number() > f["maximum"].number())))
        return key + ": " + numberG(v.number()) + " вне " + numberG(f["minimum"].number()) + "…" + numberG(f["maximum"].number());
    if (v.type() == Value::Type::List && present(f, "minItems") && (int(v.items().size()) < f["minItems"].integer() || int(v.items().size()) > f["maxItems"].integer()))
        return key + ": ожидалось " + std::to_string(f["minItems"].integer()) + " числа";
    if (v.type() == Value::Type::List && present(f, "items")) {
        const Value& item = f["items"];
        for (const Value& x : v.items())
            if (!fits(x, item["type"].text("")) || (present(item, "enum") && !listHas(item["enum"], x)))
                return key + ": в списке ожидалось " + typeName(item["type"].text(""));
    }
    return {};
}

} // namespace

// Every field that is wrong, not only the first: a call with three slips is mended in one go.
std::string argsProblem(const Value& args, const Value& schema) {
    const Value& properties = schema["properties"];
    std::string problems;
    const auto add = [&](const std::string& p) { problems += (problems.empty() ? "" : "; ") + p; };
    for (const std::string& key : sortedKeys(args))
        if (const std::string p = fieldProblem(key, args[key], properties); !p.empty()) add(p);
    for (const Value& r : schema["required"].items())
        if (!present(args, r.text(""))) add("нет обязательного поля «" + r.text("") + "»");
    return problems;
}

} // namespace ke
