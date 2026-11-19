// The journal of the editor's operations (EditorOps.h): one JSON line per call that changed something -
// when, which session, who (the window or the API), the operation, the arguments it really used and how
// it went - appended to <app data>/ops-journal.jsonl and never rewritten. The queries (lists, blocks, the
// preview under the mouse, a pixel of a point) leave no line: they change nothing and come at every mouse
// move. Snapshots of the scene among the lines make it a way back (EditorJournalRestore.cpp): the core's
// edits are replayed from them; after anything else that changes the scene a snapshot is written, so a
// restore never has to replay what its arguments cannot say (a file's content, how long a scene played).
#include "EditorOps.h"

#include "SceneBuilder.h"
#include "scene/SceneOps.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QStandardPaths>

#include <algorithm>
#include <set>

namespace {

const std::set<QString>& queries() {
    static const std::set<QString> q = {"ops_list", "ops_journal", "scene_list", "scene_get", "assembly_snap", "assembly_suggest", "assembly_presets",
                                        "assembly_preview", "sim_state", "sim_measure", "view_project", "ui_actions", "ui_pointer", "view_screenshot", "journal_sessions"};
    return q;
}

constexpr size_t kRecent = 500;

} // namespace

// The window's own state and time: no change of the scene (selection_set, gizmo_set, view_look, ui_trigger,
// play, pause, a step - the scene played is not the scene authored; a stop is: it ends a play).
bool EditorOps::windowOnly(const QString& name) {
    for (const char* prefix : {"selection_", "gizmo_", "view_", "ui_", "ops_", "journal_", "runtime_"})
        if (name.startsWith(prefix)) return true;
    return name == "sim_play" || name == "sim_record" || name == "sim_pause" || name == "sim_step" || name == "sim_impulse" || name == "scene_save" || name == "session_start" ||
           name == "session_end";
}

bool EditorOps::isQuery(const QString& name) {
    const ke::SceneOp* core = ke::findSceneOp(name.toStdString());
    return name == "runtime_status" || queries().count(name) > 0 || (core && core->query) || name.endsWith("_get");
}

QString EditorOps::journalFile() {
    if (journalFile_.isEmpty()) {
        journalFile_ = qEnvironmentVariable("KE_JOURNAL");
        if (journalFile_.isEmpty()) {
            const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            QDir().mkpath(dir);
            journalFile_ = dir + "/ops-journal.jsonl";
        }
    }
    return journalFile_;
}

// A line of this session: when, the session, the rest as given; the last lines kept for ops_journal (a
// snapshot's scene left out there: it is the file's).
void EditorOps::writeLine(QJsonObject line, bool keep) {
    line.insert("t", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!session_.isEmpty()) line.insert("s", session_);
    QFile f(journalFile());
    if (f.open(QIODevice::Append | QIODevice::Text)) f.write(QJsonDocument(line).toJson(QJsonDocument::Compact) + "\n");
    if (!keep) line["args"] = QJsonObject{{"why", line["args"].toObject()["why"]}};
    recent_.push_back(line);
    if (recent_.size() > kRecent) recent_.pop_front();
}

void EditorOps::journal(const QString& source, const QString& name, const QJsonObject& args, const QJsonObject& answer) {
    if (isQuery(name) || replaying_) return;
    QJsonObject line{{"src", source}, {"op", name}, {"args", args}, {"ok", answer.value("ok").toBool()}};
    if (answer.contains("error")) line.insert("error", answer.value("error"));
    writeLine(line);
    if (session_.isEmpty()) return; // no session (a test's own window): lines only
    if (name == "sim_play" || (name == "sim_step" && answer.value("ok").toBool())) playing_ = true;
    if (name == "sim_stop") playing_ = false;
    if (!answer.value("ok").toBool() || playing_ || windowOnly(name)) return;
    const ke::SceneOp* core = ke::findSceneOp(name.toStdString());
    if (core && core->edit && ++editsSinceSnapshot_ < snapshotEvery_) return; // replayed from the snapshot before
    snapshot(core && core->edit ? "every" : name);
}

// The scene as it is, when it is the authored one (edit mode): a checkpoint of the session.
void EditorOps::snapshot(const QString& why) {
    if (session_.isEmpty() || !builder_->editing()) return;
    writeLine({{"src", "journal"}, {"op", "journal_snapshot"}, {"ok", true},
               {"args", QJsonObject{{"why", why}, {"scene", builder_->snapshotText()}, {"base", QString::fromStdString(builder_->graph().baseDirectory)},
                                    {"next_id", double(builder_->nextIdToGive())}}}},
              false);
    editsSinceSnapshot_ = 0;
}

void EditorOps::startSession() {
    if (!session_.isEmpty()) endSession();
    session_ = QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmsszzz") + "-" + QString::number(QCoreApplication::applicationPid());
    const QString dir = QFileInfo(journalFile()).absolutePath() + "/sessions";
    QDir().mkpath(dir);
    lock_ = std::make_shared<QLockFile>(dir + "/" + session_ + ".lock");
    lock_->setStaleLockTime(0); // held while the window lives; only a dead owner frees it
    lock_->tryLock(0);
    playing_ = false;
    writeLine({{"src", "journal"}, {"op", "session_start"}, {"ok", true}, {"args", QJsonObject{{"pid", double(QCoreApplication::applicationPid())}}}});
    snapshot("start");
}

void EditorOps::endSession() {
    if (session_.isEmpty()) return;
    writeLine({{"src", "journal"}, {"op", "session_end"}, {"ok", true}, {"args", QJsonObject{}}});
    lock_.reset(); // unlocked and removed
    session_.clear();
}

void EditorOps::addJournalOps() {
    addSync("ops_journal", "The journal's last lines: every change made from the window or the API (where the file is)", "{last?: int}",
            [this](const QJsonObject& a) {
        const int last = std::clamp(a.value("last").toInt(20), 1, int(kRecent));
        QJsonArray lines;
        for (size_t k = recent_.size() > size_t(last) ? recent_.size() - size_t(last) : 0; k < recent_.size(); ++k) lines.append(recent_[k]);
        return ok({{"lines", lines}, {"file", journalFile()}, {"session", session_}});
    });
    addSync("journal_sessions", "Every session of the journal: id, start, end, lines, edits, crashed (it did not end and no window holds it)", "{}",
            [this](const QJsonObject&) { return ok({{"sessions", sessions()}, {"current", session_}}); });
    addSync("journal_restore", "The scene of a session after one of its lines (none: its last) - its last snapshot before, the core edits "
                               "after it replayed; one undo step", "{session: str, line?: int}", [this](const QJsonObject& a) {
        const QString why = restore(a.value("session").toString(), a.value("line").toInt(-1));
        return why.isEmpty() ? ok() : error(why);
    });
}
