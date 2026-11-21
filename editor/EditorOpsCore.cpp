// The core's operations (ke::sceneOps - the API's core in kecore, src/scene/SceneOps.h) registered as the
// editor's own: the scene's queries, its edits, the actions and the assembly. One name means one thing in
// the window, in the console's Python (python/kinetica) and for an agent. The editor adds only what a window has: objects a call does not
// name are the selection (an action's `ids`), a light goes over the selected object, a camera stands at
// the view's eye; after an action the window selects what it answered (the new shape, the copies) and
// says on its status line what the action switched on besides. Each edit is one undo step (transact).
#include "EditorOps.h"

#include "SceneBuilder.h"
#include "Viewport.h"
#include "scene/SceneOps.h"

#include <QMainWindow>
#include <QStatusBar>

#include <cmath>

using namespace ke;

namespace {

// The editor's JSON as the core's values and back (a whole number stays whole: an id).
Value valueOf(const QJsonValue& v) {
    switch (v.type()) {
    case QJsonValue::Bool: return Value(v.toBool());
    case QJsonValue::Double: {
        const double d = v.toDouble();
        return d == std::floor(d) && std::fabs(d) < 9e15 ? Value(int64_t(d)) : Value(d);
    }
    case QJsonValue::String: return Value(v.toString().toStdString());
    case QJsonValue::Array: {
        Value l = Value::list();
        for (const QJsonValue& x : v.toArray()) l.push(valueOf(x));
        return l;
    }
    case QJsonValue::Object: {
        Value m = Value::map();
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) m.set(it.key().toStdString(), valueOf(it.value()));
        return m;
    }
    default: return Value();
    }
}

QJsonValue jsonOf(const Value& v) {
    switch (v.type()) {
    case Value::Type::Bool: return v.truth();
    case Value::Type::Number: return v.number();
    case Value::Type::Text: return QString::fromStdString(v.text());
    case Value::Type::List: {
        QJsonArray a;
        for (const Value& x : v.items()) a.append(jsonOf(x));
        return a;
    }
    case Value::Type::Map: {
        QJsonObject o;
        for (size_t k = 0; k < v.keys().size(); ++k) o.insert(QString::fromStdString(v.keys()[k]), jsonOf(v.at(k)));
        return o;
    }
    default: return QJsonValue();
    }
}

QJsonObject answerJson(const Value& answer) {
    QJsonObject o = jsonOf(answer).toObject();
    o.insert("ok", true);
    return o;
}

} // namespace

QJsonObject coreAnswerJson(const Value& answer) { return answerJson(answer); }
Value coreValue(const QJsonValue& v) { return valueOf(v); }
QJsonValue coreJson(const Value& v) { return jsonOf(v); }

// What the window adds to a call: its selection as `ids` (the active object last), a light over the
// selected object, a camera at the view's eye.
Value EditorOps::withWindow(const SceneOp& op, const QJsonObject& a) {
    Value args = valueOf(QJsonValue(a));
    if (op.takesIds && !args.has("ids")) args.set("ids", idsValue(builder_->orderedSelection()));
    if (op.name == "scene_add_light" && !args.has("at")) args.set("at", vectorValue(builder_->spawnTarget()));
    if (op.name == "scene_add_camera" && !args.has("eye")) {
        const QVector3D eye = view_->camera().eye();
        args.set("eye", vectorValue(Vector3(eye.x(), eye.y(), eye.z())));
    }
    return args;
}

void EditorOps::afterAction(const Value& answer) {
    if (answer.has("ids")) builder_->selectMany(idsArg(answer));
    if (auto* w = qobject_cast<QMainWindow*>(window_); w && answer.has("note")) w->statusBar()->showMessage(QString::fromStdString(answer["note"].text("")), 6000);
}

void EditorOps::addCoreOps() {
    for (const SceneOp& op : sceneOps()) {
        const QString name = QString::fromStdString(op.name), help = QString::fromStdString(op.help), args = QString::fromStdString(op.args);
        if (op.query) {
            addSync(name, help, args, [this, &op](const QJsonObject& a) {
                Value answer = Value::map();
                std::string why;
                return op.query(builder_->graph(), withWindow(op, a), answer, why) ? answerJson(answer) : error(QString::fromStdString(why));
            });
            continue;
        }
        addSync(name, help, args, [this, &op](const QJsonObject& a) {
            const Value args = withWindow(op, a);
            Value answer = Value::map();
            const bool moves = op.name == "scene_move" || op.name == "scene_turn"; // a goal moved: its solve for its card
            const SceneGraph before = moves ? builder_->graph() : SceneGraph();
            const QJsonObject reply = transact([&](SceneGraph& g) {
                std::string why;
                return op.edit(g, args, answer, why) ? answerJson(answer) : error(QString::fromStdString(why));
            });
            if (reply.value("ok").toBool()) afterAction(answer);
            if (reply.value("ok").toBool() && moves) builder_->noteGoalSolves(before);
            QJsonObject out = reply;
            out.insert("_args", jsonOf(args)); // the journal keeps the ids and places the window filled in: a replay needs no window
            return out;
        });
    }
}
