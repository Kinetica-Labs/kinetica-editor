#pragma once
// Plugins as processes (docs/contracts/editor-plugin.md, «Плагин-процесс»; the Python side: clients/python/kinetica/plugin.py).
// A folder with a plugin.py in a plugins' folder - a plugins' repository added once (the project's kinetica-plugins:
// Плагины → Добавить папку плагинов…), the user's own (<app data>/plugins), KE_PLUGINS - is started as a Python process
// of its own, given the API's port and a token; plugins/ of the editor itself holds the ones that come with it (URDF). It
// registers its commands (plugin_register) and waits for calls (plugin_wait), answering each (plugin_reply); its hooks of a
// run - on_play, on_tick, on_stop - go over their own wire, apart from the window's thread (RunHooks.h). A command is then an operation of
// the editor like any: a button in the «Плагины» menu with its key, a line in Ctrl+K, a call from the console and from a
// script, a tool of an AI agent over MCP; its fields make the form the button opens. What it changes through the API
// is one undo step. A saved plugin.py - or any .py beside it, its modules - restarts its process - the loop of an edit,
// about a second; a plugin that falls
// over shows its error with its file and line on a banner with «Перезапустить», and the editor goes on.
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "EditorOps.h"

class QAction;
class QFileSystemWatcher;
class QMenu;
class QProcess;
class QWidget;
class RunHooks;
class SceneBuilder;

class PluginHost : public QObject {
    Q_OBJECT
public:
    // `place`: a plugin's panel put into the window (true) or taken out (false) - a tab of the inspector or the bottom.
    // `hooks`: the run's wire (RunHooks.h) - the window's, alive as long as this host starts and stops plugins.
    PluginHost(EditorOps* ops, SceneBuilder* builder, QWidget* window, QMenu* menu, RunHooks* hooks, std::function<quint16()> apiPort,
               std::function<void(class PluginPanel*, bool)> place);
    ~PluginHost() override;
    // Every plugin of the folders started: the added ones and the user's - or, given (a test), only KE_PLUGINS.
    void start(bool onlyGiven);
    QStringList lookedThrough() const;        // the folders that start looks through, in its order
    QStringList folders() const;              // the plugins' folders added (a repository's checkout)
    void addFolder(const QString& folder);    // kept for the next starts, its plugins started now
    QString userFolder() const;                         // where «Новый плагин…» makes one
    // A plugin from a template, started: its file. kind: "command" (a command with a field), "controller" (a robot's
    // on_tick: sensors in, commands out), "world" (an extension of the physics: bodies in, forces out).
    QString create(const QString& name, QString& error, const QString& kind = "command");
    QStringList names() const;
    bool ticking(bool world = false) const; // a plugin ticks (world: one that extends the physics, it gets the bodies)
    void runStarted();                      // ▶: a hook's error is said again, once a run
    // A plugin's tool (@command(tool=True)) used in the view: its command with the click's point, normal and object, and
    // the values of its panel the command has (the tool's options); its shelf button lit while it is the view's tool.
    void useTool(const QString& op, const QJsonObject& click);
    // The files plugins open (@command(opens=".urdf")): the file dialog's filters ("URDF — робот (*.urdf)"), the command
    // that opens a file (its extension; empty: none), and the call with its path - an import is one undo step.
    QStringList formats() const;
    QString openerFor(const QString& file) const;
    void openFile(const QString& op, const QString& path);
    void setToolChecked(const QString& op);

signals:
    void changed(); // a plugin came, went, or fell
    void output(const QString& plugin, const QString& line); // a line it printed (print(), a traceback), or a word about it
    // Its shelf button pressed (empty op: pressed again, left); drags: its command takes `to` - two points, not one.
    void toolPicked(const QString& op, const QString& label, bool drags);

private:
    struct Command {
        QString name, label, key, help, op;
        bool tool = false; // a tool of the shelf: a click in the view calls it
        QStringList opens; // the file extensions it opens (".urdf"): «Файл → Импорт…», a file dropped on the view
        QJsonObject args; // field -> {type, default, label, unit, minimum, maximum, enum}
        QJsonArray order; // the fields as the function lists them (the form's rows)
        QAction* action = nullptr;
    };
    struct Plugin {
        QString name, file, token, state, error;
        QProcess* process = nullptr;
        class QMenu* menu = nullptr; // «Плагины → <name>»: its commands and «Открыть plugin.py», while it runs
        std::vector<Command> commands;
        QStringList stderrTail;
        QByteArray unfinished; // the start of a line whose end has not come yet: Python writes a traceback's last line in pieces
        std::vector<std::pair<int, std::pair<QString, QJsonObject>>> queued; // calls waiting for the plugin to ask
        EditorOps::Reply waiting;                                         // its plugin_wait held open
        std::map<int, EditorOps::Reply> open;                             // calls sent, not answered yet
        QElapsedTimer since;                                              // a restart's clock: the loop of an edit
        bool restarting = false; // its process was stopped to start again: the exit it makes is ours, not a fall
        bool reloaded = false;   // started again after a save: its registering says how long it took
        class PluginPanel* panel = nullptr; // its panel (PluginPanel.h), while it runs
        QJsonObject panelValues;            // what its panel held when it stopped: given back when it comes again
        QString hookError;                  // a hook's last error, said once a run
        bool watches = false;               // it has an @on_change: told when its components in the scene change
        bool changeOpen = false;            // told, not answered yet
        QByteArray componentsSeen;          // its components as it was last told of them
    };
    void addOps();
    void scan(const QString& folder);
    void launch(Plugin& p);
    void stop(Plugin& p);
    void forget(const QString& file); // its file gone: the plugin out of the editor
    void onExit(Plugin& p, int code);
    void unregister(Plugin& p);
    void call(Plugin& p, const QString& command, const QJsonObject& args, const EditorOps::Reply& reply);
    void deliver(Plugin& p);
    void invoke(const QString& plugin, const Command& c); // a button: the form of its fields, then the call
    QJsonArray refObjects(const QString& kind, QString* why = nullptr) const; // a field of a scene object: what it may be
    QString refProblem(const QJsonObject& fields, const QJsonObject& args) const;
    void hookDrew(const QString& plugin, const QString& event, const QJsonArray& pictures); // a hook's answer, after the fact:
    void hookFailed(const QString& plugin, const QString& event, const QString& error);     // its pictures, its error (RunHooks.h)
    void hookShowed(const QString& plugin, const QString& event, double time, const QJsonObject& values); // its panel's values
    Plugin* find(const QString& name);
    void componentsChanged(); // @on_change: each plugin that asked is told when its components are no longer what it was told of
    Plugin* authorised(const QJsonObject& a, QJsonObject& refusal);
    void notice(const QString& plugin, const QString& text); // a fall on the window's status bar, with «Перезапустить»; empty: gone

    EditorOps* ops_;
    SceneBuilder* builder_;
    QWidget* window_;
    QMenu* menu_;
    RunHooks* hooks_;
    QAction* commandsEnd_ = nullptr; // the menu's separator: the commands above it
    std::function<quint16()> apiPort_;
    std::function<void(class PluginPanel*, bool)> place_;
    QFileSystemWatcher* watcher_;
    class QFrame* notice_ = nullptr;
    class QLabel* noticeText_ = nullptr;
    QString noticeFor_;
    std::vector<std::unique_ptr<Plugin>> plugins_;
    class QTimer* changeTimer_ = nullptr; // the scene's edits settle before the plugins are told
    int nextCall_ = 0;
    bool onlyGiven_ = true; // the last start's: plugin_scan looks through the same folders
};
