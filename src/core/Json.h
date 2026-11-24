#pragma once
// JSON text of a ke::Value, in the core without Qt: the wire of the windowless host and the Python client
// read and write it. The window's own server speaks Qt's QJsonDocument; the self-test group "wire-parity"
// (editor/SelfTestHosts.cpp) holds the two to the same bytes and the same trees, and tests/JsonTests.cpp
// pins what it found without Qt. Where this reader differs from Qt's on purpose, it says so below.
//   Writer: compact; object keys in byte order (Qt compares UTF-16 units - the two differ only for keys
//   that mix characters above the BMP with U+E000..U+FFFF); a whole number below 2^53 as an integer, any
//   other finite number as numberTextShortest, NaN and infinities as null; text raw UTF-8 after validUtf8.
//   Reader: as Qt's, which is more lenient than RFC 8259 (".5", "1.", "\q" as "q", a raw control character
//   in a text are read); a whole number below 9e15 becomes Value(int64_t) as the window's valueOf does;
//   members enter a map in key order, the last of a repeated key wins; a lone surrogate escape is U+FFFD
//   (the window reads it as '?' through toStdString, so the hosts differ there; declared in wire-parity); nesting over 200 is an error (Qt's
//   limit is 1024); a byte order mark and any other text outside the value are errors (Qt skips a mark at
//   the start of the document and one at the start of a text).
#include "core/Value.h"

#include <string>

namespace ke {

std::string toJson(const Value& v);
// false: `error` is "не JSON: <что> (знак N)", N the byte where it was found; `out` is unspecified.
bool parseJson(const std::string& text, Value& out, std::string& error);
// Throws std::invalid_argument(error): for examples and tests, where an error is a mistake of the author.
Value parseJson(const std::string& text);
// Each byte that is not part of a well-formed UTF-8 sequence becomes U+FFFD, one for one, as QString::fromUtf8 does;
// a leading byte order mark is kept (Qt drops it).
std::string validUtf8(const std::string& s);

} // namespace ke
