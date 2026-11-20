// Scripts run by the editor (see ScriptRunner.h): the process, its lines, its one undo step.
#include "ScriptRunner.h"

#include "PythonRuntime.h"
#include "SceneBuilder.h"
#include "UiLanguage.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>

ScriptRunner::ScriptRunner(EditorOps* ops, SceneBuilder* builder, std::function<quint16()> apiPort, QObject* parent)
    : QObject(parent), builder_(builder), apiPort_(std::move(apiPort)) {
    ops->addOp("script_run", "Run a Python script that builds or changes the scene (import kinetica as ke; scene = ke.connect()) and wait for "
                             "its end: its edits are one undo step, its print() is the answer's output; a script that falls changes "
                             "nothing and says its file and line. A script runs and is gone - what stays and extends the editor is a plugin",
               "{file: str}", [this](const QJsonObject& a, const EditorOps::Reply& reply) { run(a.value("file").toString(), reply); });
    ops->addOp("script_stop", "Stop the script that is running (script_run): its process is ended and what it did is taken back, as when a "
                              "script falls", "{}", [this](const QJsonObject&, const EditorOps::Reply& reply) {
        if (!process_) return reply(EditorOps::error("no script is running"));
        const QString name = name_;
        stopped_ = true;
        process_->kill(); // its end is said to script_run's caller (done)
        reply(EditorOps::ok({{"script", name}}));
    });
}

void ScriptRunner::run(const QString& file, const EditorOps::Reply& reply) {
    const QFileInfo info(file);
    if (!info.isFile()) return reply(EditorOps::error("no such script: " + file));
    if (process_) return reply(EditorOps::error("a script is running: " + name_));
    name_ = info.fileName();
    pending_.clear(), tail_.clear(), stopped_ = false;
    process_ = new QProcess(this);
    process_->setProcessEnvironment(pythonEnvironment(apiPort_()));
    process_->setWorkingDirectory(info.absolutePath());
    process_->setProcessChannelMode(QProcess::MergedChannels); // its prints and its traceback, one tail
    builder_->beginUndoGroup(); // every edit the script makes: one step of Ctrl+Z
    connect(process_, &QProcess::readyReadStandardOutput, this, &ScriptRunner::read);
    connect(process_, &QProcess::finished, this, [this, reply](int code, QProcess::ExitStatus how) {
        read();
        const bool ok = how == QProcess::NormalExit && code == 0 && !stopped_;
        QString why = ok ? QString() : stopped_ ? QString(uiText("Kinetica", "остановлен")) : pythonError(tail_);
        if (!ok && why.isEmpty()) why = QString("exit code %1").arg(code);
        done(ok, why, reply);
    });
    connect(process_, &QProcess::errorOccurred, this, [this, reply](QProcess::ProcessError what) {
        if (what == QProcess::FailedToStart) done(false, uiText("Kinetica", "Python 3 не запустился: поставьте его (python.org, «Add to PATH»)"), reply);
    });
    process_->start(pythonInterpreter(), {"-u", info.absoluteFilePath()});
    emit running(true);
}

void ScriptRunner::read() {
    pending_ += process_->readAllStandardOutput();
    for (qsizetype at = pending_.indexOf('\n'); at >= 0; at = pending_.indexOf('\n')) {
        QByteArray raw = pending_.left(at);
        pending_.remove(0, at + 1);
        while (raw.endsWith('\r')) raw.chop(1); // Windows' Python ends a line with \r\n
        const QString line = QString::fromUtf8(raw);
        if (line.trimmed().isEmpty()) continue;
        tail_ << line;
        if (tail_.size() > 40) tail_.removeFirst();
        emit output(name_, line);
    }
}

// The script ended: its edits kept as one step, or - fallen - taken back; the answer; the next script may run.
void ScriptRunner::done(bool ok, const QString& why, const EditorOps::Reply& reply) {
    if (!process_) return;
    process_->deleteLater();
    process_ = nullptr;
    emit running(false);
    if (!ok && !builder_->editing()) builder_->stop(); // it left the scene playing: back as authored, then its edits taken back
    const bool undone = builder_->endUndoGroup(ok);
    if (ok) return reply(EditorOps::ok({{"script", name_}, {"output", QJsonArray::fromStringList(tail_)}}));
    reply(EditorOps::error((why.startsWith(name_) ? QString() : name_ + ": ") + why + // «falls.py:5: ZeroDivisionError: ...» names it already
                           (undone ? QString(uiText("Kinetica", ". Правки этого вызова отменены: сцена как до него")) : QString())));
}
