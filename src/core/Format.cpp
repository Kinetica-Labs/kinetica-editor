// Numbers in files (Format.h): standard streams imbued with the classic locale, so neither the
// program's C locale (printf, strtof) nor its global C++ locale can change the text.
#include "core/Format.h"

#include <algorithm>
#include <charconv>
#include <iomanip>
#include <locale>
#include <limits>
#include <sstream>

namespace ke {

template <class T>
static bool parse(const std::string& word, T& out) {
    std::istringstream in(word);
    in.imbue(std::locale::classic());
    in >> out;
    return !in.fail() && in.peek() == std::char_traits<char>::eof();
}

bool parseNumber(const std::string& word, float& out) { return parse(word, out); }
bool parseNumber(const std::string& word, double& out) { return parse(word, out); }

std::string numberText(float v) {
    if (v == 0) v = 0; // -0 is written as 0
    std::ostringstream out;
    out.imbue(std::locale::classic());
    for (int digits = 6; digits <= 9; ++digits) {
        out.str({});
        out << std::setprecision(digits) << v; // the default notation: printf's %g
        float back = 0;
        if (parseNumber(out.str(), back) && back == v) break;
    }
    return out.str();
}

std::string numberText(double v) {
    if (v == 0) v = 0; // -0 is written as 0
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << v;
    return out.str();
}

// std::to_chars gives the shortest digits whatever the locale. Its own choice between fixed and exponent text is
// not taken: for a large number it writes fixed text with every digit of the exact value (53165205877497296),
// where Qt pads the shortest digits with zeros (53165205877497300).
std::string numberTextShortest(double v) {
    if (v == 0) return "0";
    char text[32]; // "-1.7976931348623157e+308" is 24
    const char* const end = std::to_chars(text, text + sizeof text, v, std::chars_format::scientific).ptr;
    const char* const first = text + (v < 0 ? 1 : 0);
    const char* const e = std::find(first, end, 'e');
    std::string digits(first, e);
    digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
    int exponent = 0;
    std::from_chars(e + (e[1] == '+' ? 2 : 1), end, exponent); // from_chars reads "-05", not "+16"
    const int n = int(digits.size());
    const std::string fixed = exponent < 0 ? "0." + std::string(size_t(-exponent - 1), '0') + digits
                              : n <= exponent + 1 ? digits + std::string(size_t(exponent + 1 - n), '0')
                                                  : digits.substr(0, size_t(exponent + 1)) + "." + digits.substr(size_t(exponent + 1));
    return (v < 0 ? "-" : "") + (fixed.size() <= size_t(end - first) ? fixed : std::string(first, end));
}

} // namespace ke
