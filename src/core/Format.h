#pragma once
// Numbers as text, in one place.
//   format("%.2f м/с", v)  - a readout for people: printf into a std::string, in whatever locale the
//                            program chose (a Russian user may see "9,81").
//   numberText / parseNumber - numbers in files: always the classic locale ('.' as the decimal
//                            point), whatever locale the program's C library is in. A program that
//                            switched to its user's locale (Qt does on Linux, at start) would
//                            otherwise write "9,81" and fail to read everyone else's "9.81".
#include <cstdarg>
#include <cstdio>
#include <string>
#include <type_traits>

namespace ke {

inline std::string format(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

// The shortest decimal that reads back as the same float: 0.02 stays "0.02" (not the exact
// "0.0199999996"), yet text -> number -> text is exact. Nine digits always suffice for a float.
std::string numberText(float v);
// Scientific observations retain all double digits; float scene serialization stays unchanged.
std::string numberText(double v);
template<class T, std::enable_if_t<std::is_integral<T>::value, int> = 0>
std::string numberText(T v) { return std::to_string(v); }
// The whole word as a number ("1.5x", "" and "1,5" are not); false leaves `out` unspecified.
bool parseNumber(const std::string& word, float& out);
bool parseNumber(const std::string& word, double& out);

} // namespace ke
