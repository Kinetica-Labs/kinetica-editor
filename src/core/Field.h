#pragma once
// A field of a schema as data (docs/constitution.md, article 7): one description from which the window's form, the check
// at the boundary of an operation, the line of the scene file, the JSON Schema an MCP tool and a client get, Python's
// hints and the reference are all made - no hand-made copy of any of them. Its unit is a UCUM code (core/Units.h), its
// words for a person apart from it.
//
// A key says its unit in its suffix, as datasheets write them, and the suffix must agree with the unit (unitMatchesKey):
//   _hz Hz   _m m   _mm mm   _ms ms   _s s   _deg deg   _dps deg/s   _n N   _n_m N.m   _kg kg   _pct %   bits bit
//   _g [g]   _mg m[g]   _ug u[g]  - g is the standard gravity here, as in a datasheet's +-2 g (a mass is in kg)
//   _rthz  per square root of a hertz, after another suffix: _ug_rthz u[g]/[rtHz], _dps_rthz deg/s/[rtHz]
#include "core/Value.h"

#include <cmath>
#include <string>
#include <vector>

namespace ke {

struct Field {
    enum class Type { Number, Integer, Flag, Choice, Text };
    std::string key;                 // the file's, the API's and Python's name: ascii snake_case, its unit in its suffix
    Type type = Type::Number;
    std::string unit;                // UCUM, "" none
    double min = -HUGE_VAL, max = HUGE_VAL; // hard: a value outside is refused, never clamped
    double step = 0;                 // a slider's or a spin box's
    int decimals = 3;
    double value = 0;                // what it is when nothing gave it
    std::vector<std::string> options; // a choice's words
    std::string label, help;         // for a person, in Russian
};

// JSON Schema 2020-12 of an object of these fields: no other key allowed; a number's unit as "x-unit" (UCUM) and in its
// description with the person's words.
Value fieldsSchema(const std::vector<Field>& fields, const std::vector<std::string>& required = {});
// A value given for a field: false and why - "bits: 99 > 32 бит (max)", "rate_hz: expected a number".
bool checkField(const Field& f, const Value& v, std::string& error);
// Its unit parses and agrees with its key's suffix (above); false and why.
bool unitMatchesKey(const Field& f, std::string& error);

} // namespace ke
