// Numbers in files (Format.h): standard streams imbued with the classic locale, so neither the
// program's C locale (printf, strtof) nor its global C++ locale can change the text.
#include "core/Format.h"

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

} // namespace ke
