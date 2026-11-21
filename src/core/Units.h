#pragma once
// Physical units as data (docs/constitution.md, article 4): a unit's code in UCUM - the Unified Code for Units of
// Measure (https://ucum.org), case-sensitive - parsed into its scale to SI and its dimension, and written in words for
// a person ("u[g]/[rtHz]" -> "мкg/√Гц"). Every field of a schema carries one, so a number crosses the boundary of the
// file, the API and Python with its unit, and a mismatch of dimensions is found when the schema loads, not by a robot.
//
// The subset read: the SI base units (m, g, s, A, K, mol, cd), rad, deg, %, Hz, N, Pa, J, W, V, Ohm, C, min, h, bit,
// [g] (the standard acceleration of gravity, 9.80665 m/s^2), the prefixes G M k c m u n p, integer exponents (m2, s-2)
// and the operators . and / read left to right. One atom of our own: [rtHz], the square root of a hertz - the unit of
// a noise density (a datasheet's ug/sqrt(Hz)), which UCUM, having integer exponents only, cannot write.
#include <array>
#include <cstdint>
#include <string>

namespace ke {

// The exponents of length, mass, time, current, temperature, amount and luminosity, each times 2 (a half: [rtHz]).
struct Dimension {
    std::array<int8_t, 7> twice{};
    bool operator==(const Dimension& o) const { return twice == o.twice; }
    bool operator!=(const Dimension& o) const { return !(*this == o); }
    bool none() const { return *this == Dimension(); }
};

struct Unit {
    double scale = 1; // the value in SI for 1 of the unit: km -> 1000, deg -> pi / 180
    Dimension dimension;
};

// The code's unit; false and why for a code outside the subset ("" is 1, no unit).
bool parseUnit(const std::string& code, Unit& out, std::string& error);
// The code in Russian words for a field's suffix: "m/s2" -> "м/с²", "N.m" -> "Н·м", "deg/s" -> "°/с" ("" for none).
std::string unitText(const std::string& code);
// The dimension as SI base units, for a message: "m.s-2", "1" when it has none.
std::string dimensionText(const Dimension& d);

} // namespace ke
