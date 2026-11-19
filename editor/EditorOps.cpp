// The operator registry (EditorOps.h): running a call with the guard that keeps the window from saying
// an API edit twice, the journal and the console as listeners, one undo step around an edit, and the
// operations only the window has - a new scene, files, undo and redo. The scene's own operations come
// from the core (EditorOpsCore.cpp).
#include "EditorOps.h"

#include "ArgsSchema.h"
#include "EditorRuntime.h"

#include "SceneBuilder.h"
#include "core/Format.h"
#include "scene/Assembly.h"
#include "scene/SceneEdits.h"
#include "scene/SceneOps.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>

using namespace ke;

EditorOps::EditorOps(SceneBuilder* builder, Viewport* view, SimController* ctrl, QWidget* window)
    : QObject(builder), builder_(builder), view_(view), ctrl_(ctrl), window_(window) {
    addSceneOps();
    addCoreOps();
    addPreviewOps();
    addRuntimeOps(); // before the operations that ask it whether the session is stopping
    addSimOps();
    addViewOps();
    addNoteOp();
    addInputOps();
    addJournalOps();
    addStateOps();
    addSync("ops_list", "Every operation of the editor: name, help, arguments", "{}", [this](const QJsonObject&) {
        return ok({{"ops", list()}});
    });
}

QJsonObject EditorOps::ok(QJsonObject fields) {
    fields.insert("ok", true);
    return fields;
}

QJsonObject EditorOps::error(const QString& why) { return QJsonObject{{"ok", false}, {"error", why}}; }

void EditorOps::add(const QString& name, const QString& help, const QString& args, Handler run) {
    ops_[name] = Op{help, args, std::move(run), argsSchema(args)}; // the line a person reads is the schema a model's client builds from
}

void EditorOps::addSync(const QString& name, const QString& help, const QString& args, std::function<QJsonObject(const QJsonObject&)> run) {
    add(name, help, args, [run](const QJsonObject& a, const Reply& reply) { reply(run(a)); });
}

void EditorOps::addPluginOp(const QString& name, const QString& help, const QString& args, Handler run, const QJsonObject& schema) {
    add(name, help, args, std::move(run));
    ops_[name].schema = schema;
    pluginOps_.insert(name);
}

QString EditorOps::unknown(const QString& name) const {
    const QString hint = unknownHint_ ? unknownHint_(name) : QString();
    return hint.isEmpty() ? "нет операции «" + name + "» (ops_list называет все)" : hint;
}

void EditorOps::removePluginOp(const QString& name) {
    if (!pluginOps_.erase(name)) return;
    ops_.erase(name);
}

void EditorOps::run(const QString& name, const QJsonObject& args, const Reply& reply, const QString& source) {
    auto it = ops_.find(name);
    if (it == ops_.end()) return reply(error(unknown(name)));
    if (const QString why = argsProblem(args, it->second.schema); !why.isEmpty()) return reply(error(why)); // refused, not guessed
    // An edit's changes are its own line: the window does not say them again. A hand's input (ui_pointer,
    // ui_key, ui_trigger) is no edit: what the window does with it is said by the window, as for a hand.
    // A query changes nothing: it leaves the window's lines alone (a drag in progress stays one line).
    const bool query = isQuery(name);
    if (runtime_ && runtime_->stopping() && !query && name != "runtime_stop") return reply(error("runtime is stopping"));
    if (runtime_ && (!query || name == "view_screenshot" || name == "ui_pointer")) runtime_->request(name, args, source);
    const bool guarded = !name.startsWith("ui_") && !query;
    if (guarded) builder_->apiCallStarted();
    const ke::SceneOp* core = ke::findSceneOp(name.toStdString());
    const bool playing = core && core->edit && !builder_->editing(); // an edit of a running scene: Stop takes it back
    it->second.run(args, [this, source, name, args, reply, guarded, query, playing](QJsonObject answer) {
        if (guarded) builder_->apiCallFinished();
        if (playing && answer.value("ok").toBool())
            answer.insert("until_stop", "сцена играет: правка действует до ■ Стоп - sim_stop вернёт сцену, какой она была до ▶");
        const QJsonObject used = answer.contains("_args") ? answer.take("_args").toObject() : args; // what the window filled in
        if (!query && !pluginOps_.count(name)) journal(source, name, used, answer); // a plugin's command: its edits are the lines
        for (const Listener& l : listeners_) l(source, name, args, answer, query);
        reply(answer);
    });
}

void EditorOps::report(const QString& source, const QString& name, const QJsonObject& args) {
    if (runtime_ && !isQuery(name)) runtime_->request(name, args, source);
    const QJsonObject answer = ok();
    journal(source, name, args, answer);
    for (const Listener& l : listeners_) l(source, name, args, answer, isQuery(name));
}

QJsonObject EditorOps::runNow(const QString& name, const QJsonObject& args) {
    QJsonObject answer = error("'" + name + "' answers later: call it through run()");
    run(name, args, [&answer](const QJsonObject& a) { answer = a; }, "ui");
    return answer;
}

QJsonArray EditorOps::list() const {
    QJsonArray out;
    for (const auto& [name, op] : ops_) {
        QJsonObject entry{{"name", name}, {"help", op.help}, {"args", op.args}};
        entry.insert("schema", op.schema); // JSON Schema: a plugin's fields, or the core's line of arguments read (ArgsSchema.h)
        out.append(entry);
    }
    return out;
}

// One undo step around a change of the graph: its answer, or the error that undid it.
QJsonObject EditorOps::transact(const std::function<QJsonObject(SceneGraph&)>& change) {
    QJsonObject answer;
    std::string why;
    const bool done = builder_->transact([&](SceneGraph& g, std::string& err) {
        answer = change(g);
        err = answer.value("error").toString().toStdString();
        return answer.value("ok").toBool();
    }, why);
    return done ? answer : error(QString::fromStdString(why));
}

void EditorOps::addSceneOps() {
    addSync("scene_open", "A scene file opened (one undo step back to the scene before)", "{file: str}", [this](const QJsonObject& a) {
        QString err;
        return builder_->openFile(a.value("file").toString(), err) ? ok() : error(err);
    });
    addSync("scene_save", "The scene saved to a file", "{file: str}", [this](const QJsonObject& a) {
        QString err;
        return builder_->saveFile(a.value("file").toString(), err) ? ok({{"file", QFileInfo(a.value("file").toString()).absoluteFilePath()}}) : error(err);
    });
    addSync("scene_new", "A new scene: the floor and nothing else", "{}", [this](const QJsonObject&) {
        builder_->newScene();
        return ok();
    });
    // What a step of history changed, so a caller need not read the scene again to know: the objects, the world.
    const auto stepped = [this](bool undo) {
        const SceneGraph before = builder_->graph();
        const bool done = undo ? builder_->undoStep() : builder_->redoStep();
        QJsonArray changed;
        bool world = false;
        static const QRegularExpression idOf(R"(\bid (\d+))");
        for (const ke::GraphEdit& e : diffEdits(before, builder_->graph())) {
            if (e.kind == ke::GraphEdit::Kind::World) { world = true; continue; }
            const auto m = idOf.match(QString::fromStdString(e.text));
            const qint64 id = e.kind == ke::GraphEdit::Kind::Put && m.hasMatch() ? m.captured(1).toLongLong() : qint64(e.id);
            if (!changed.contains(id)) changed.append(id);
        }
        for (const auto* pair : {&before.joints, &builder_->graph().joints}) // a joint gone with its link: said too
            for (const ke::JointObject& j : *pair)
                if (!ke::findJoint(before, j.id) != !ke::findJoint(builder_->graph(), j.id) && !changed.contains(qint64(j.id))) changed.append(qint64(j.id));
        return ok({{undo ? "undone" : "redone", done}, {"changed", changed}, {"world", world}, {"undo_steps", qint64(builder_->undoSteps())}});
    };
    addSync("edit_undo", "Undo the last step. Answers what it changed: the objects' ids, whether the world did; undone false: nothing "
            "left to undo", "{}", [stepped](const QJsonObject&) { return stepped(true); });
    addSync("edit_redo", "Redo the step undone last. Answers what it changed, as edit_undo", "{}", [stepped](const QJsonObject&) { return stepped(false); });
}
