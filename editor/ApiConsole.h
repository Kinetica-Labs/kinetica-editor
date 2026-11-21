#pragma once
// The API console - the editor's Info and Python line: every action is echoed as code. A typed line runs in a
// Python process of its own (kinetica.console of python/kinetica, a Python of the system): the whole object
// model - scene, scene.objects["Куб 1"].move(...), loops on one line - connected to this window over its
// API, so the window stays a client and needs no Python inside; with no Python, a line is one call. Every call of the operator registry (EditorOps)
// shows up as the Python line that makes it, whoever made it - an agent or a script over the network,
// the window, this console - with its answer under it: what an agent does is seen as code, and any
// line can be copied, changed and run again. The input line takes the same calls,
//   ed.assembly_sensor(target=166, kind="sonar", face="+x")      (the "ed." may be left out)
//   scene_list {"...": ...}                                        (or the arguments as JSON)
// Python's literals as in the client (tools/ke_editor.py): numbers, "strings", [lists], True, False, None.
// A call that brings its own line (`_py`, sent by the kinetica object model) is shown as that line:
// scene.objects["Шасси"].add_sonar(face="+x"), not the operation underneath.
// Up and Down walk the lines run before; Tab completes an operation's name.
#include <QJsonObject>
#include <QStringList>
#include <QWidget>

#include <functional>

class EditorOps;
class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QProcess;

class ApiConsole : public QWidget {
public:
    explicit ApiConsole(EditorOps* ops, QWidget* parent = nullptr);

    // The Python line of a call: ed.name(key=value, ...), values as Python literals.
    static QString pythonCall(const QString& name, const QJsonObject& args, int longest = 0); // longest > 0: longer strings cut with …
    // A typed line back into an operation and its arguments (false and `error`: not a call).
    static bool parseCall(const QString& text, QString& name, QJsonObject& args, QString& error);
    // The port of the window's API for the console's Python (MainWindow starts the server when it is not
    // serving; 0: none - a line is then one call).
    void setApiPort(std::function<quint16()> port) { apiPort_ = std::move(port); }
    // A line a plugin printed (its print(), its traceback): in the log as it comes, under the plugin's name.
    void pluginLine(const QString& plugin, const QString& line);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void show(const QString& source, const QString& name, const QJsonObject& args, const QJsonObject& answer, bool query);
    void submit();
    bool startPython(); // the console's Python running (started at the first line), or false: none to be had
    void readPython();  // its printed answer, line by line, to the log

    EditorOps* ops_;
    QPlainTextEdit* log_;
    QLineEdit* input_;
    QCheckBox* queries_;
    QStringList history_;
    int historyAt_ = 0;
    QProcess* python_ = nullptr;
    bool noPython_ = false;
    std::function<quint16()> apiPort_;
    QByteArray pending_;
};
