#pragma once
// An operation's arguments as JSON Schema, read off its line of help - "{joint: int, face?: '+x'..'-z', size?: [x,y,z]}"
// (EditorOps::add): one fact in one place, the line a person reads and the schema a model's client builds its call from
// (ops_list -> MCP inputSchema). The line's little grammar:
//
//   name: type      a field the call must give        name?: type   one it may give
//   a: int | b: [int]   either field (neither required)   {ids}         the selection's ids when none are given
//   int · number · str · bool · a unit word (m, s, rad, degrees, px, N s...) - a number
//   'a'|'b'|c       one of these words               '+x'..'-z'     a face (tx..rz: a freedom)
//   [x,y,z]         three numbers                     [int]          integers       {…}  an object
//   (0.10)          the default
//
// What the line does not say is not guessed: such a field has no type, only its words as its description. Every field the
// line names is a property, and only those: a call with another is refused, naming the fields (argsProblem) - a typo said,
// not silently ignored.
#include <QJsonObject>
#include <QString>

QJsonObject argsSchema(const QString& argsLine);
// What is wrong with a call's arguments against the schema (empty: nothing): a field it does not have, a value not of
// its type, a word not of its list - every one, joined by "; ". Fields starting with '_' (the console's `_py`) are the transport's, never refused.
QString argsProblem(const QJsonObject& args, const QJsonObject& schema);
