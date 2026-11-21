// A field of a schema as data (Field.h): its JSON Schema, the check of a value, its unit against its key.
#include "core/Field.h"

#include "core/Format.h"
#include "core/Units.h"

namespace ke {

namespace {

// The suffixes of keys and the units they say, longest first.
const std::pair<const char*, const char*> kSuffixes[] = {
    {"_ug_rthz", "u[g]/[rtHz]"}, {"_dps_rthz", "deg/s/[rtHz]"}, {"_n_m", "N.m"}, {"_dps", "deg/s"}, {"_pct", "%"},
    {"_deg", "deg"}, {"_hz", "Hz"}, {"_mm", "mm"}, {"_ms", "ms"}, {"_mg", "m[g]"}, {"_ug", "u[g]"}, {"_kg", "kg"},
    {"_bits", "bit"}, {"_m", "m"}, {"_s", "s"}, {"_n", "N"}, {"_g", "[g]"}};

const char* typeWord(Field::Type t) {
    switch (t) {
    case Field::Type::Integer: return "integer";
    case Field::Type::Flag: return "boolean";
    case Field::Type::Text: return "string";
    case Field::Type::Choice: return "string";
    case Field::Type::Number: break;
    }
    return "number";
}

} // namespace

Value fieldsSchema(const std::vector<Field>& fields, const std::vector<std::string>& required) {
    Value properties = Value::map();
    for (const Field& f : fields) {
        Value p = Value::map();
        p.set("type", typeWord(f.type));
        if (!f.label.empty()) p.set("title", f.label);
        const std::string words = unitText(f.unit);
        p.set("description", f.help.empty() ? words : words.empty() ? f.help : f.help + " [" + words + "]");
        if (!f.unit.empty()) p.set("x-unit", f.unit);
        if (f.type == Field::Type::Number || f.type == Field::Type::Integer) {
            if (std::isfinite(f.min)) p.set("minimum", f.min);
            if (std::isfinite(f.max)) p.set("maximum", f.max);
            p.set("default", f.type == Field::Type::Integer ? Value(int(f.value)) : Value(f.value));
        } else if (f.type == Field::Type::Flag) {
            p.set("default", f.value != 0);
        } else if (f.type == Field::Type::Choice) {
            Value options = Value::list();
            for (const std::string& o : f.options) options.push(o);
            p.set("enum", options);
        }
        properties.set(f.key, p);
    }
    Value schema = Value::map(), needed = Value::list();
    for (const std::string& r : required) needed.push(r);
    schema.set("type", "object");
    schema.set("properties", properties);
    if (needed.size() > 0) schema.set("required", needed);
    schema.set("additionalProperties", false);
    return schema;
}

bool checkField(const Field& f, const Value& v, std::string& error) {
    const std::string unit = f.unit.empty() ? std::string() : " " + unitText(f.unit);
    switch (f.type) {
    case Field::Type::Flag: return v.type() == Value::Type::Bool || (error = f.key + ": expected true or false", false);
    case Field::Type::Text: return v.type() == Value::Type::Text || (error = f.key + ": expected a text", false);
    case Field::Type::Choice:
        for (const std::string& o : f.options)
            if (v.text("\x01") == o) return true;
        error = f.key + ": «" + v.text("?") + "» is not one of";
        for (const std::string& o : f.options) error += " " + o;
        return false;
    case Field::Type::Number:
    case Field::Type::Integer: break;
    }
    if (v.type() != Value::Type::Number) return error = f.key + ": expected a number" + (unit.empty() ? "" : " in" + unit), false;
    const double x = v.number();
    if (f.type == Field::Type::Integer && x != std::floor(x)) return error = f.key + ": expected a whole number, got " + numberText(x), false;
    if (x < f.min) return error = f.key + ": " + numberText(x) + " < " + numberText(f.min) + unit + " (min)", false;
    if (x > f.max) return error = f.key + ": " + numberText(x) + " > " + numberText(f.max) + unit + " (max)", false;
    return true;
}

bool unitMatchesKey(const Field& f, std::string& error) {
    Unit declared, said;
    if (!parseUnit(f.unit, declared, error)) return error = f.key + ": " + error, false;
    const std::string key = "_" + f.key;
    for (const auto& [suffix, code] : kSuffixes) {
        const std::string s = suffix;
        if (key.size() < s.size() || key.compare(key.size() - s.size(), s.size(), s) != 0) continue;
        parseUnit(code, said, error);
        if (said.dimension == declared.dimension && std::fabs(said.scale / declared.scale - 1) < 1e-9) return true;
        return error = f.key + ": its suffix " + s + " says " + code + ", its unit is «" + f.unit + "» (" + dimensionText(declared.dimension) + ")", false;
    }
    return true; // no suffix that says a unit: nothing to agree with
}

} // namespace ke
