// The operator registry (EditorOps.h): running a call with the guard that keeps the window from saying
// an API edit twice, the journal and the console as listeners, one undo step around an edit, and the
// operations only the window has - a new scene, files, undo and redo. The scene's own operations come
// from the core (EditorOpsCore.cpp).
#include "EditorOps.h"
#include "EditorRuntime.h"

#include "SceneBuilder.h"
#include "core/Format.h"
#include "scene/Assembly.h"
#include "scene/SceneEdits.h"

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
    ops_[name] = Op{help, args, std::move(run)};
}

void EditorOps::addSync(const QString& name, const QString& help, const QString& args, std::function<QJsonObject(const QJsonObject&)> run) {
    add(name, help, args, [run](const QJsonObject& a, const Reply& reply) { reply(run(a)); });
}

void EditorOps::addPluginOp(const QString& name, const QString& help, const QString& args, Handler run, const QJsonObject& schema) {
    add(name, help, args, std::move(run));
    ops_[name].schema = schema;
    pluginOps_.insert(name);
}

void EditorOps::removePluginOp(const QString& name) {
    if (!pluginOps_.erase(name)) return;
    ops_.erase(name);
}

void EditorOps::run(const QString& name, const QJsonObject& args, const Reply& reply, const QString& source) {
    auto it = ops_.find(name);
    if (it == ops_.end()) return reply(error("no operation '" + name + "' (ops_list names them)"));
    // An edit's changes are its own line: the window does not say them again. A hand's input (ui_pointer,
    // ui_key, ui_trigger) is no edit: what the window does with it is said by the window, as for a hand.
    // A query changes nothing: it leaves the window's lines alone (a drag in progress stays one line).
    const bool query = isQuery(name);
    if (runtime_ && runtime_->stopping() && !query && name != "runtime_stop") return reply(error("runtime is stopping"));
    if (runtime_ && (!query || name == "view_screenshot" || name == "ui_pointer")) runtime_->request(name, args, source);
    const bool guarded = !name.startsWith("ui_") && !query;
    if (guarded) builder_->apiCallStarted();
    it->second.run(args, [this, source, name, args, reply, guarded, query](QJsonObject answer) {
        if (guarded) builder_->apiCallFinished();
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
        if (!op.schema.isEmpty()) entry.insert("schema", op.schema); // a plugin's command: its fields as JSON Schema
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
        return builder_->saveFile(a.value("file").toString(), err) ? ok() : error(err);
    });
    addSync("scene_new", "A new scene: the floor and nothing else", "{}", [this](const QJsonObject&) {
        builder_->newScene();
        return ok();
    });
    addSync("edit_undo", "Undo the last step", "{}", [this](const QJsonObject&) { builder_->undoStep(); return ok(); });
    addSync("edit_redo", "Redo the step undone last", "{}", [this](const QJsonObject&) { builder_->redoStep(); return ok(); });
}
