#pragma once
// An operation's arguments as JSON Schema, read off its line of help - "{joint: int, face?: '+x'..'-z', size?: [x,y,z]}" -
// and a call checked against it, in the core without Qt (ArgsSchema.cpp). One fact in one place: the line a person reads
// and the schema a model's client builds its call from (ops_list -> MCP inputSchema). The line's little grammar:
//
//   name: type      a field the call must give        name?: type   one it may give
//   a: int | b: [int]   either field (neither required)   {ids}         the selection's ids when none are given
//   int · number · str · bool · a unit word (m, s, rad, degrees, px, N s...) - a number
//   'a'|'b'|c       one of these words               '+x'..'-z'     a face (tx..rz: a freedom)
//   [x,y,z]         three numbers                     [int]          integers       {…}  an object
//   (0.10)          the default
//   {…} | {…}       a call takes one shape or the other
//
// What the line does not say is not guessed: such a field has no type, only its words as its description. Every field the
// line names is a property, and only those: a call with another is refused, naming the fields (argsProblem) - a typo said,
// not silently ignored. This is a port of the window's first copy (editor/ArgsSchema.cpp, Qt) rule for rule, odd ones
// included; the self-test group schema-parity ran the two side by side and tests/data/args-refusals.json holds the
// texts the Qt one gave. The window still calls that Qt copy; the next commit switches it and deletes it.
// Known differences from it, none in a real line: a type word spelled with U+212A (Kelvin sign) is no unit here (ASCII
// lower-casing); keys are ordered by UTF-8 bytes, Qt's by UTF-16 units (differs for astral vs U+E000..U+FFFF keys).
#include "core/Value.h"

#include <string>

namespace ke {

Value argsSchema(const std::string& argsLine);
// What is wrong with a call's arguments against the schema (empty: nothing): a field it does not have, a value not of
// its type, a word not of its list - every one, in key order, joined by "; ". Fields starting with '_' (the console's
// `_py`) are the transport's, never refused.
std::string argsProblem(const Value& args, const Value& schema);

} // namespace ke
