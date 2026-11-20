#pragma once
// A script: a Python file that builds or changes the scene and ends - an example as code, the way Blender runs a .py.
// It is a client of the same API as the console and the plugins (import kinetica as ke; scene = ke.connect()), started
// by the operation script_run {file}: every edit it makes is one step of Ctrl+Z, its print() comes to the API console,
// and a script that falls takes back what it had built and says its file and line. A plugin stays and gives the editor
// a new ability (PluginHost.h); a script runs and is gone. One script at a time.
#include "EditorOps.h"

#include <QObject>
#include <QStringList>

#include <functional>

class QProcess;
class SceneBuilder;

class ScriptRunner : public QObject {
    Q_OBJECT
public:
    ScriptRunner(EditorOps* ops, SceneBuilder* builder, std::function<quint16()> apiPort, QObject* parent = nullptr);

signals:
    void output(const QString& script, const QString& line); // its print() and its traceback, as they come

private:
    void run(const QString& file, const EditorOps::Reply& reply);
    void read();
    void done(bool ok, const QString& why, const EditorOps::Reply& reply);

    SceneBuilder* builder_;
    std::function<quint16()> apiPort_;
    QProcess* process_ = nullptr; // the script running now
    QString name_;                // its file's name: «cubes_row.py»
    QByteArray pending_;          // a line not ended yet
    QStringList tail_;            // its last lines: the answer's output, a fall's file and line
};
