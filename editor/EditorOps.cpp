// The operator registry (EditorOps.h): running a call with the guard that keeps the window from saying
// an API edit twice, the journal and the console as listeners, one undo step around an edit, and the
// operations only the window has - a new scene, files, undo and redo. The scene's own operations come
// from the core (EditorOpsCore.cpp).
#include "EditorOps.h"

#include "EditorRuntime.h"

#include "SceneBuilder.h"
#include "core/Format.h"
#include "scene/ArgsSchema.h"
#include "scene/Assembly.h"
#include "scene/SceneOps.h"

#include <QFileInfo>
#include <QJsonArray>

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
    addSync("ops_list", "Every operation of the editor: name, help, arguments, their JSON Schema, and kind - query (reads), edit (the "
                        "scene, one undo step), window (the selection, the view, play and pause: never the authored scene) or "
                        "action (files, plugins, the program). families: only the operations whose names begin so - "
                        "[\"scene\", \"sim\"] gives scene_* and sim_*", "{families?: [str]}", [this](const QJsonObject& a) {
        QStringList families;
        for (const QJsonValue& f : a.value("families").toArray()) families << f.toString();
        return ok({{"ops", list(families)}});
    });
}

QJsonObject EditorOps::ok(QJsonObject fields) {
    fields.insert("ok", true);
    return fields;
}

QJsonObject EditorOps::error(const QString& why) { return QJsonObject{{"ok", false}, {"error", why}}; }

void EditorOps::add(const QString& name, const QString& help, const QString& args, Handler run) {
    ops_[name] = Op{help, args, std::move(run), ke::argsSchema(args.toStdString())}; // the line a person reads is the schema a model's client builds from
}

void EditorOps::addSync(const QString& name, const QString& help, const QString& args, std::function<QJsonObject(const QJsonObject&)> run) {
    add(name, help, args, [run](const QJsonObject& a, const Reply& reply) { reply(run(a)); });
}

void EditorOps::addPluginOp(const QString& name, const QString& help, const QString& args, Handler run, const QJsonObject& schema) {
    add(name, help, args, std::move(run));
    ops_[name].schema = coreValue(schema);
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
    if (const std::string why = ke::argsProblem(coreValue(args), it->second.schema); !why.empty()) return reply(error(QString::fromStdString(why))); // refused, not guessed
    // An edit's changes are its own line: the window does not say them again. A hand's input (ui_pointer,
    // ui_key, ui_trigger) is no edit: what the window does with it is said by the window, as for a hand.
    // A query changes nothing: it leaves the window's lines alone (a drag in progress stays one line).
    const bool query = isQuery(name);
    if (runtime_ && runtime_->stopping() && !query && name != "runtime_stop") return reply(error("runtime is stopping"));
    if (runtime_ && (!query || name == "view_screenshot" || name == "ui_pointer")) runtime_->request(name, args, source);
    const bool guarded = !name.startsWith("ui_") && !query;
    if (guarded) builder_->apiCallStarted();
    const ke::SceneOp* core = ke::findSceneOp(name.toStdString());
    const bool playing = ((core && core->edit) || name == "edit_undo" || name == "edit_redo") && !builder_->editing(); // an edit of a running scene
    it->second.run(args, [this, source, name, args, reply, guarded, query, playing](QJsonObject answer) {
        if (guarded) builder_->apiCallFinished();
        if (playing && answer.value("ok").toBool()) { // decided after the edit: the one the world could not take stopped it, and is kept
            if (builder_->editing()) answer.insert("play_stopped", true), answer.insert("reload", ke::reloadWord(builder_->lastReload()));
            else answer.insert("until_stop", "сцена играет: правка действует до ■ Стоп - sim_stop вернёт сцену, какой она была до ▶");
        }
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

QJsonArray EditorOps::list(const QStringList& families) const {
    QJsonArray out;
    for (const auto& [name, op] : ops_) {
        if (wiring(name)) continue; // a plugin process's own, not an operation anyone calls
        if (!families.isEmpty() && !families.contains(name.section('_', 0, 0))) continue;
        QJsonObject entry{{"name", name}, {"help", op.help}, {"args", op.args}, {"kind", kind(name)}};
        entry.insert("schema", coreJson(op.schema)); // JSON Schema: a plugin's fields, or the core's line of arguments read (scene/ArgsSchema.h)
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
        return coreAnswerJson(ke::historyAnswer(before, builder_->graph(), undo, done, builder_->undoSteps()));
    };
    addSync("edit_undo", "Undo the last step. Answers what it changed: the objects' ids, whether the world did; undone false: nothing "
            "left to undo", "{}", [stepped](const QJsonObject&) { return stepped(true); });
    addSync("edit_redo", "Redo the step undone last. Answers what it changed, as edit_undo", "{}", [stepped](const QJsonObject&) { return stepped(false); });
}
