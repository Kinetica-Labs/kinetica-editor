// JSON text of a Value (Json.h): the writer, the reader and the UTF-8 check they share.
#include "core/Json.h"

#include "core/Format.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>

namespace ke {

namespace {

const char kReplacement[] = "\xEF\xBF\xBD";

// The length of the well-formed UTF-8 sequence at s[i] (no overlong form, no surrogate, nothing above U+10FFFF), 0 if none.
size_t sequenceLength(const std::string& s, size_t i) {
    const auto at = [&](size_t k) { return i + k < s.size() ? uint8_t(s[i + k]) : 0; };
    const auto within = [&](size_t k, int lo, int hi) { return at(k) >= lo && at(k) <= hi; };
    const uint8_t c = at(0);
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) return within(1, 0x80, 0xBF) ? 2 : 0;
    if (c >= 0xE0 && c <= 0xEF) {
        const int lo = c == 0xE0 ? 0xA0 : 0x80, hi = c == 0xED ? 0x9F : 0xBF;
        return within(1, lo, hi) && within(2, 0x80, 0xBF) ? 3 : 0;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        const int lo = c == 0xF0 ? 0x90 : 0x80, hi = c == 0xF4 ? 0x8F : 0xBF;
        return within(1, lo, hi) && within(2, 0x80, 0xBF) && within(3, 0x80, 0xBF) ? 4 : 0;
    }
    return 0;
}

void appendCodePoint(std::string& s, uint32_t c) {
    if (c < 0x80) {
        s += char(c);
    } else if (c < 0x800) {
        s += char(0xC0 | c >> 6);
        s += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        s += char(0xE0 | c >> 12);
        s += char(0x80 | (c >> 6 & 0x3F));
        s += char(0x80 | (c & 0x3F));
    } else {
        s += char(0xF0 | c >> 18);
        s += char(0x80 | (c >> 12 & 0x3F));
        s += char(0x80 | (c >> 6 & 0x3F));
        s += char(0x80 | (c & 0x3F));
    }
}

void writeText(std::string& out, const std::string& text) {
    static const char hex[] = "0123456789abcdef";
    out += '"';
    for (const char ch : validUtf8(text)) {
        const uint8_t c = uint8_t(ch);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 15];
            } else {
                out += ch;
            }
        }
    }
    out += '"';
}

void write(std::string& out, const Value& v) {
    switch (v.type()) {
    case Value::Type::Bool: out += v.truth() ? "true" : "false"; break;
    case Value::Type::Number: {
        const double d = v.number();
        if (!std::isfinite(d)) out += "null";
        else if (d == std::floor(d) && std::fabs(d) < 9007199254740992.0) out += std::to_string(int64_t(d));
        else out += numberTextShortest(d);
        break;
    }
    case Value::Type::Text: writeText(out, v.text()); break;
    case Value::Type::List: {
        out += '[';
        for (size_t k = 0; k < v.items().size(); ++k) {
            if (k) out += ',';
            write(out, v.items()[k]);
        }
        out += ']';
        break;
    }
    case Value::Type::Map: {
        std::map<std::string, const Value*> sorted; // two keys that validUtf8 makes equal: the later one wins, as in Qt
        for (size_t k = 0; k < v.keys().size(); ++k) sorted[validUtf8(v.keys()[k])] = &v.at(k);
        out += '{';
        for (auto it = sorted.begin(); it != sorted.end(); ++it) {
            if (it != sorted.begin()) out += ',';
            writeText(out, it->first);
            out += ':';
            write(out, *it->second);
        }
        out += '}';
        break;
    }
    default: out += "null";
    }
}

struct Fail {
    const char* what;
    size_t at;
};

constexpr int kMaxDepth = 200;

class Reader {
public:
    explicit Reader(const std::string& s) : s_(s) {}

    Value document() {
        skip();
        Value v = value(0);
        skip();
        if (i_ != s_.size()) throw Fail{"лишний текст после значения", i_};
        return v;
    }

private:
    const std::string& s_;
    size_t i_ = 0;

    bool more() const { return i_ < s_.size(); }
    char peek() const { return more() ? s_[i_] : '\0'; }
    void skip() {
        while (more() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }
    static bool digit(char c) { return c >= '0' && c <= '9'; }
    bool word(const char* w) {
        const std::string t = w;
        if (s_.compare(i_, t.size(), t) != 0) return false;
        i_ += t.size();
        return true;
    }

    Value value(int depth) {
        if (!more()) throw Fail{"ожидалось значение", i_};
        const char c = s_[i_];
        if (c == '{') return object(depth + 1);
        if (c == '[') return list(depth + 1);
        if (c == '"') return Value(text());
        if (c == '-' || c == '.' || digit(c)) return number();
        if (word("true")) return Value(true);
        if (word("false")) return Value(false);
        if (word("null")) return Value();
        throw Fail{"ожидалось значение", i_};
    }

    Value list(int depth) {
        if (depth > kMaxDepth) throw Fail{"слишком глубокая вложенность", i_};
        Value l = Value::list();
        ++i_;
        skip();
        if (peek() == ']') return ++i_, l;
        for (;;) {
            skip();
            l.push(value(depth));
            skip();
            if (peek() == ']') return ++i_, l;
            if (peek() != ',') throw Fail{"ожидалась запятая или ]", i_};
            ++i_;
        }
    }

    Value object(int depth) {
        if (depth > kMaxDepth) throw Fail{"слишком глубокая вложенность", i_};
        std::map<std::string, Value> members;
        ++i_;
        skip();
        if (peek() == '}') return ++i_, Value::map();
        for (;;) {
            skip();
            if (peek() != '"') throw Fail{"ожидалось имя поля", i_};
            std::string key = text();
            skip();
            if (peek() != ':') throw Fail{"ожидалось двоеточие", i_};
            ++i_;
            skip();
            members[std::move(key)] = value(depth);
            skip();
            if (peek() == '}') break;
            if (peek() != ',') throw Fail{"ожидалась запятая или }", i_};
            ++i_;
        }
        ++i_;
        Value m = Value::map();
        for (auto& [key, member] : members) m.set(key, std::move(member));
        return m;
    }

    uint32_t hex4() {
        uint32_t u = 0;
        for (int k = 0; k < 4; ++k, ++i_) {
            const char c = peek();
            const int d = digit(c) ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) throw Fail{"неверная escape-последовательность", i_};
            u = u << 4 | uint32_t(d);
        }
        return u;
    }

    // After "\u": a code unit; a high surrogate takes the low one that follows it, a lone one is U+FFFD.
    uint32_t codePoint() {
        const uint32_t u = hex4();
        if (u >= 0xDC00 && u <= 0xDFFF) return 0xFFFD;
        if (u < 0xD800 || u > 0xDBFF) return u;
        if (s_.compare(i_, 2, "\\u") != 0) return 0xFFFD;
        const size_t after = i_;
        i_ += 2;
        const uint32_t low = hex4();
        if (low >= 0xDC00 && low <= 0xDFFF) return 0x10000 + ((u - 0xD800) << 10) + (low - 0xDC00);
        i_ = after;
        return 0xFFFD;
    }

    std::string text() {
        std::string out;
        ++i_;
        for (;;) {
            if (!more()) throw Fail{"незакрытая строка", i_};
            const char c = s_[i_];
            if (c == '"') return ++i_, out;
            if (c != '\\') {
                const size_t n = sequenceLength(s_, i_);
                if (!n) throw Fail{"неверный UTF-8", i_};
                out.append(s_, i_, n);
                i_ += n;
                continue;
            }
            ++i_;
            if (!more()) throw Fail{"незакрытая строка", i_};
            const char e = s_[i_];
            if (uint8_t(e) >= 0x80) throw Fail{"неверный UTF-8", i_};
            ++i_;
            switch (e) {
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': appendCodePoint(out, codePoint()); break;
            default: out += e; // Qt reads "\q" as "q"; '"', '\\' and '/' are the cases that matter
            }
        }
    }

    // Qt's grammar: an optional minus, a mantissa of digits with an optional point (at least one digit in all),
    // an optional exponent with digits; a zero is not followed by more digits (that is the next token's error).
    Value number() {
        const size_t start = i_;
        size_t digits = 0;
        bool nonzero = false;
        const auto scan = [&] {
            for (; digit(peek()); ++i_, ++digits) nonzero |= peek() != '0';
        };
        if (peek() == '-') ++i_;
        if (peek() == '0') ++i_, ++digits;
        else scan();
        if (peek() == '.') ++i_, scan();
        if (!digits) throw Fail{"неверное число", start};
        if (peek() == 'e' || peek() == 'E') {
            ++i_;
            if (peek() == '+' || peek() == '-') ++i_;
            if (!digit(peek())) throw Fail{"неверное число", start};
            while (digit(peek())) ++i_;
        }
        double d = 0;
        // Qt refuses a number that overflows or that underflows to zero; a denormal is a number.
        if (!parseNumber(s_.substr(start, i_ - start), d) || (d == 0 && nonzero)) throw Fail{"число вне диапазона", start};
        return d == std::floor(d) && std::fabs(d) < 9e15 ? Value(int64_t(d)) : Value(d);
    }
};

} // namespace

std::string toJson(const Value& v) {
    std::string out;
    write(out, v);
    return out;
}

bool parseJson(const std::string& text, Value& out, std::string& error) {
    try {
        out = Reader(text).document();
        return true;
    } catch (const Fail& f) {
        error = std::string("не JSON: ") + f.what + " (знак " + std::to_string(f.at) + ")";
        return false;
    }
}

Value parseJson(const std::string& text) {
    Value v;
    std::string error;
    if (!parseJson(text, v, error)) throw std::invalid_argument(error);
    return v;
}

std::string validUtf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const size_t n = sequenceLength(s, i);
        if (n) out.append(s, i, n);
        else out += kReplacement;
        i += n ? n : 1;
    }
    return out;
}

} // namespace ke
