// An operation's line of arguments read as JSON Schema, and a call checked against it (ArgsSchema.h: the grammar).
#include "ArgsSchema.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace {

// The pieces of `text` between `separator`s that lie outside every bracket.
QStringList topLevel(const QString& text, QChar separator) {
    QStringList out;
    QString piece;
    int depth = 0;
    for (const QChar c : text) {
        if (c == '[' || c == '{' || c == '(') ++depth;
        else if (c == ']' || c == '}' || c == ')') --depth;
        if (c == separator && depth == 0) out << piece, piece.clear();
        else piece += c;
    }
    out << piece;
    return out;
}

// The line's outermost {…} groups (a call takes one of them: "{target…} | {joint…}"), and the words outside them.
void groupsOf(const QString& line, QStringList& groups, QString& outside) {
    QString piece;
    int depth = 0, inner = 0; // a group's own depth; inner: a field's {…} in the words outside
    for (const QChar c : line) {
        const QString before = outside.trimmed();
        if (c == '{' && depth == 0 && inner == 0 && (before.isEmpty() || before.endsWith('|'))) { depth = 1, piece.clear(); continue; }
        if (depth > 0) {
            if (c == '{') ++depth;
            if (c == '}' && --depth == 0) { groups << piece; continue; }
            piece += c;
            continue;
        }
        inner += c == '{' ? 1 : c == '}' ? -1 : 0;
        outside += c;
    }
}

const QRegularExpression& word() {
    static const QRegularExpression w(R"(^'?([A-Za-z0-9_+\-]+)'?$)");
    return w;
}

QJsonObject typed(QString t);

bool unitWord(const QString& w) {
    static const QSet<QString> units{"m", "s", "rad", "degrees", "deg", "px", "kg", "n", "hz", "v", "a", "ohm", "m/s", "lines", "number", "float"};
    return units.contains(w.toLower());
}

// A field's type from its words after the colon (the words kept as its description: units, defaults, what it means).
QJsonObject typeOf(QString t) {
    QJsonObject p = typed(t);
    // The words stay as the description only where they say more than the type: a unit, a default, a meaning.
    static const QRegularExpression plain(R"(^\s*(int|integer|number|str|string|bool|boolean|\[[a-z, ]+\]|'?[\w+\-]+'?(\s*(\||\.\.)\s*'?[\w+\-]+'?)+)\s*$)");
    if (p.contains("type") && plain.match(t).hasMatch()) p.remove("description");
    return p;
}

QJsonObject typed(QString t) {
    t = t.trimmed();
    QJsonObject p;
    if (t.isEmpty()) return p;
    p.insert("description", t);
    static const QRegularExpression fallback(R"(\(\s*(-?[0-9]+(?:\.[0-9]+)?)\s*\)\s*$)");
    if (const auto m = fallback.match(t); m.hasMatch()) p.insert("default", m.captured(1).toDouble()), t = t.left(int(m.capturedStart())).trimmed();
    static const QRegularExpression range(R"(^'?([+\-]?)([a-z]+)'?\.\.'?([+\-]?)([a-z]+)'?$)");
    if (const auto m = range.match(t); m.hasMatch()) { // '+x'..'-z': the six faces; 'tx'..'rz': the six freedoms
        const QStringList faces{"+x", "-x", "+y", "-y", "+z", "-z"}, dofs{"tx", "ty", "tz", "rx", "ry", "rz"};
        p.insert("type", "string"), p.insert("enum", QJsonArray::fromStringList(m.captured(1).isEmpty() ? dofs : faces));
        return p;
    }
    const QStringList either = topLevel(t, '|');
    if (either.size() > 1) {
        QStringList words;
        for (const QString& e : either)
            if (const auto m = word().match(e.trimmed()); m.hasMatch()) words << m.captured(1);
        if (words.size() != either.size()) { // words and other shapes: any of them ("x|y|z|[x,y,z]", "[x,y,z] | number")
            QJsonArray any;
            QStringList plain;
            for (const QString& e : either)
                if (const auto m = word().match(e.trimmed()); m.hasMatch() && !unitWord(m.captured(1))) plain << m.captured(1);
                else if (const QJsonObject one = typeOf(e); one.contains("type")) any.append(one);
                else return p;
            if (!plain.isEmpty()) any.append(QJsonObject{{"type", "string"}, {"enum", QJsonArray::fromStringList(plain)}});
            return p.insert("anyOf", any), p;
        }
        if (std::all_of(words.begin(), words.end(), unitWord)) return p.insert("type", "number"), p;
        static const QSet<QString> types{"int", "integer", "number", "float", "str", "string", "bool", "boolean"};
        if (std::all_of(words.begin(), words.end(), [](const QString& w) { return types.contains(w); })) { // "number|str": either type
            QJsonArray any;
            for (const QString& w : words) any.append(typed(w));
            return p.insert("anyOf", any), p;
        }
        if (words.contains("id")) return p.insert("type", "integer"), p;
        bool numbers = true;
        QJsonArray values;
        for (const QString& w : words) {
            bool isInt = false;
            const int v = w.toInt(&isInt);
            numbers = numbers && isInt;
            values.append(isInt ? QJsonValue(v) : QJsonValue(w));
        }
        if (!numbers) { // words: the list as strings
            values = QJsonArray::fromStringList(words);
            p.insert("type", "string");
        } else {
            p.insert("type", "integer");
        }
        p.insert("enum", values);
        return p;
    }
    if (t.startsWith('[')) {
        p.insert("type", "array");
        const QString inner = t.mid(1, t.lastIndexOf(']') - 1).trimmed();
        const QStringList items = topLevel(inner, ',');
        if (inner == "int") p.insert("items", QJsonObject{{"type", "integer"}});
        else if (inner.startsWith('[')) p.insert("items", QJsonObject{{"type", "array"}});
        else if (items.size() >= 2 && items.size() <= 4 &&
                 std::all_of(items.begin(), items.end(), [](const QString& x) { return QRegularExpression(R"(^\s*[a-z]+\s*$)").match(x).hasMatch(); }))
            p.insert("items", QJsonObject{{"type", "number"}}), p.insert("minItems", int(items.size())), p.insert("maxItems", int(items.size()));
        else if (items.size() == 1) {
            const QJsonObject one = typeOf(inner);
            if (one.contains("enum")) p.insert("items", QJsonObject{{"type", one.value("type")}, {"enum", one.value("enum")}});
        }
        return p;
    }
    if (t.startsWith('{')) return p.insert("type", "object"), p;
    const QString first = t.section(' ', 0, 0).toLower();
    if (first == "int" || first == "integer" || QRegularExpression(R"(\bid\b)").match(t).hasMatch()) p.insert("type", "integer");
    else if (first == "str" || first == "string") p.insert("type", "string");
    else if (first == "bool" || first == "boolean") p.insert("type", "boolean");
    else if (unitWord(first) || first == "m/s") p.insert("type", "number");
    else if (const auto m = word().match(t); m.hasMatch() && t.startsWith('\'')) p.insert("type", "string"), p.insert("enum", QJsonArray{m.captured(1)});
    return p;
}

struct Field {
    QString name;
    bool required = false;
    QJsonObject type;
};

// One piece between commas: "name?: type", "a: int | b: [int]" (either, neither required), "face?|point?+normal?".
void fieldsOf(const QString& piece, QList<Field>& out, bool inGroup) {
    static const QRegularExpression head(R"(^\s*([a-z_][a-z0-9_]*)(\?)?\s*(?::(.*))?$)", QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression nextField(R"(^\s*[a-z_][a-z0-9_]*\??\s*:)");
    QStringList alternatives;
    QString part;
    int depth = 0;
    for (int i = 0; i < piece.size(); ++i) { // split on a '|' that starts another field
        const QChar c = piece[i];
        if (c == '[' || c == '{' || c == '(') ++depth;
        else if (c == ']' || c == '}' || c == ')') --depth;
        if (c == '|' && depth == 0 && nextField.match(piece.mid(i + 1)).hasMatch()) alternatives << part, part.clear();
        else part += c;
    }
    alternatives << part;
    for (const QString& a : alternatives) {
        if (const auto m = head.match(a); m.hasMatch()) {
            out.append({m.captured(1), inGroup && alternatives.size() == 1 && m.captured(2).isEmpty() && m.captured(1) != "ids", typeOf(m.captured(3))});
            continue;
        }
        static const QRegularExpression named(R"(([a-z_][a-z0-9_]*)\?)");
        for (auto it = named.globalMatch(a); it.hasNext();) out.append({it.next().captured(1), false, {}});
    }
}

QString typeName(const QString& t) {
    return t == "integer" ? "целое" : t == "number" ? "число" : t == "string" ? "строка" : t == "boolean" ? "true/false" : t == "array" ? "список" : t == "object" ? "объект" : t;
}

bool fits(const QJsonValue& v, const QString& type) {
    return type == "boolean" ? v.isBool() : type == "string" ? v.isString() : type == "array" ? v.isArray() : type == "object" ? v.isObject()
         : type == "integer" ? v.isDouble() && v.toDouble() == std::floor(v.toDouble()) : type == "number" ? v.isDouble() : true;
}

} // namespace

QJsonObject argsSchema(const QString& argsLine) {
    QStringList groups;
    QString outside;
    groupsOf(argsLine, groups, outside);
    QJsonObject properties;
    QSet<QString> required;
    bool first = true;
    for (const QString& g : groups) {
        QList<Field> fields;
        for (const QString& piece : topLevel(g, ',')) fieldsOf(piece, fields, true);
        QSet<QString> here;
        for (const Field& f : fields) {
            QJsonObject known = properties.value(f.name).toObject();
            if (known.isEmpty()) {
                properties.insert(f.name, f.type);
            } else if (known.contains("enum") && f.type.contains("enum")) { // a word of either group: kind 'imu'|… | 'wrench'|…
                QJsonArray words = known.value("enum").toArray();
                for (const QJsonValue& w : f.type.value("enum").toArray())
                    if (!words.contains(w)) words.append(w);
                known.insert("enum", words), properties.insert(f.name, known);
            }
            if (f.required) here.insert(f.name);
        }
        required = first ? here : required.intersect(here); // either group: required only what both require
        first = false;
    }
    static const QRegularExpression named(R"(([a-z_][a-z0-9_]*)\?\s*:)"); // fields said in the words after the groups
    for (const QString& piece : topLevel(outside, ','))
        if (const auto m = named.match(piece); m.hasMatch() && !properties.contains(m.captured(1)))
            properties.insert(m.captured(1), typeOf(piece.mid(int(m.capturedEnd()))));
    if (properties.contains("ids") && properties.value("ids").toObject().isEmpty())
        properties.insert("ids", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "integer"}}}, {"description", "ids; none: the selection"}});
    QJsonObject schema{{"type", "object"}, {"properties", properties}, {"additionalProperties", false}};
    QStringList names(required.begin(), required.end());
    names.sort();
    if (!names.isEmpty()) schema.insert("required", QJsonArray::fromStringList(names));
    return schema;
}

namespace {

// What is wrong with one field's value against the schema (empty: nothing).
QString fieldProblem(const QString& key, const QJsonValue& v, const QJsonObject& properties) {
    if (key.startsWith('_')) return {};
    if (!properties.contains(key))
        return properties.isEmpty() ? "аргументов нет, а дано «" + key + "»" : "нет поля «" + key + "»; поля: " + QStringList(properties.keys()).join(", ");
    const QJsonObject f = properties.value(key).toObject();
    const QString type = f.value("type").toString();
    if (!fits(v, type)) return key + ": ожидалось " + typeName(type) + (f.contains("description") ? " (" + f.value("description").toString() + ")" : QString());
    if (f.contains("enum") && !f.value("enum").toArray().contains(v)) {
        QStringList allowed;
        for (const QJsonValue& e : f.value("enum").toArray()) allowed << (e.isString() ? e.toString() : QString::number(e.toDouble()));
        return key + ": «" + (v.isString() ? v.toString() : QString::number(v.toDouble())) + "» не из списка: " + allowed.join(", ");
    }
    if (v.isDouble() && ((f.contains("minimum") && v.toDouble() < f.value("minimum").toDouble()) ||
                         (f.contains("maximum") && v.toDouble() > f.value("maximum").toDouble())))
        return QString("%1: %2 вне %3…%4").arg(key).arg(v.toDouble()).arg(f.value("minimum").toDouble()).arg(f.value("maximum").toDouble());
    if (v.isArray() && f.contains("minItems") && (v.toArray().size() < f.value("minItems").toInt() || v.toArray().size() > f.value("maxItems").toInt()))
        return QString("%1: ожидалось %2 числа").arg(key).arg(f.value("minItems").toInt());
    if (v.isArray() && f.contains("items")) {
        const QJsonObject item = f.value("items").toObject();
        for (const QJsonValue& x : v.toArray())
            if (!fits(x, item.value("type").toString()) || (item.contains("enum") && !item.value("enum").toArray().contains(x)))
                return key + ": в списке ожидалось " + typeName(item.value("type").toString());
    }
    return {};
}

} // namespace

// Every field that is wrong, not only the first: a call with three slips is mended in one go.
QString argsProblem(const QJsonObject& args, const QJsonObject& schema) {
    const QJsonObject properties = schema.value("properties").toObject();
    QStringList problems;
    for (auto it = args.begin(); it != args.end(); ++it)
        if (const QString p = fieldProblem(it.key(), it.value(), properties); !p.isEmpty()) problems << p;
    for (const QJsonValue& r : schema.value("required").toArray())
        if (!args.contains(r.toString())) problems << "нет обязательного поля «" + r.toString() + "»";
    return problems.join("; ");
}
