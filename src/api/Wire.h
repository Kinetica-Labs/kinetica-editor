#pragma once
// The wire of the windowless host: JSON lines on a pair of streams, one request a line and one answer a line, in
// order. kinetica-core puts stdin and stdout on it and Python's ke.Scene() speaks it; the window's own TCP server
// (Qt) speaks the same envelope, and the transcript in tests/data holds the two to it (docs/contracts/operations.md).
//   request  {"id": <any JSON>, "op": "name", "args": {..}, "src": ..}   src is ignored
//   answer   {"id": <as it came>, "result": <the operation's answer>}    no id in the request, none in the answer
// A top-level list is the operation "" (the unknown-operation answer); text that is not JSON, or JSON that is
// neither an object nor a list, answers {"result": {"ok": false, "error": "не JSON: ..."}} with no id. An "args" that
// is not an object acts as {}. A blank line is skipped, a trailing '\r' dropped, a line has no length limit.
#include "api/Scene.h"

#include <iosfwd>

namespace ke {

// Reads `in` to its end, answering each line on `out` and flushing after each answer.
void serve(std::istream& in, std::ostream& out, Scene& scene);

} // namespace ke
