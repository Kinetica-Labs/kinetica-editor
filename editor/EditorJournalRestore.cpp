// The journal as a way back (EditorOps.h): the sessions it holds (their edits: what changed a scene), and the scene of any moment of one of
// them - its last snapshot before that moment, then the core's edits after the snapshot replayed through
// the operations (deterministic: the same ids, the same bits). An edit made while the scene played is not
// replayed (a stop takes the scene back to before the play); what else changed the scene is followed by a
// snapshot, so a restore starts after it. A session crashed when it has no last line and its lock is free:
// the window that held it is gone.
#include "EditorOps.h"

#include "Resources.h"
#include "SceneBuilder.h"
#include "scene/SceneOps.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>

#include <map>

QString EditorOps::journalAt(const QString& file) {
    if (file.isEmpty()) return journalFile();
    const QString path = QFileInfo(file).isAbsolute() ? file : resourceDir("examples") + "/films/" + file;
    return QFileInfo(path).isFile() ? path : QString();
}

std::vector<QJsonObject> EditorOps::sessionLines(const QString& session, const QString& journal) {
    std::vector<QJsonObject> out;
    QFile f(journal.isEmpty() ? journalFile() : journal);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    const QByteArray key = "\"s\":\"" + session.toUtf8() + "\"";
    while (!f.atEnd()) {
        const QByteArray raw = f.readLine();
        if (raw.contains(key)) out.push_back(QJsonDocument::fromJson(raw).object()); // only this session's lines are parsed
    }
    return out;
}

QJsonArray EditorOps::sessions(const QString& journal) {
    struct Seen { QString start, end; int lines = 0, edits = 0; };
    std::map<QString, Seen> by;
    std::vector<QString> order;
    QFile f(journal.isEmpty() ? journalFile() : journal);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        while (!f.atEnd()) {
            const QByteArray raw = f.readLine();
            const int at = raw.indexOf("\"s\":\"");
            if (at < 0) continue; // a line from before the sessions
            const QString s = QString::fromUtf8(raw.mid(at + 5, raw.indexOf('"', at + 5) - at - 5));
            Seen& seen = by[s];
            if (seen.lines++ == 0) order.push_back(s);
            const QJsonObject line = QJsonDocument::fromJson(raw.contains("journal_snapshot") ? QByteArray("{}") : raw).object();
            if (seen.start.isEmpty()) seen.start = QJsonDocument::fromJson(raw).object().value("t").toString();
            if (line.value("op").toString() == "session_end") seen.end = line.value("t").toString();
            seen.edits += line.value("src").toString() != "journal" && !raw.contains("journal_snapshot") && !windowOnly(line.value("op").toString());
        }
    QJsonArray out;
    const QString dir = QFileInfo(journalFile()).absolutePath() + "/sessions/";
    for (const QString& s : order) {
        const Seen& seen = by[s];
        bool crashed = false;
        if (seen.end.isEmpty() && s != session_ && journal.isEmpty()) { // a film's sessions are closed books
            QLockFile probe(dir + s + ".lock");
            probe.setStaleLockTime(0);
            crashed = probe.tryLock(0); // free: no living window holds the session
            if (crashed) probe.unlock();
        }
        out.append(QJsonObject{{"session", s}, {"start", seen.start}, {"end", seen.end}, {"lines", seen.lines}, {"edits", seen.edits},
                               {"crashed", crashed}});
    }
    return out;
}

QString EditorOps::restore(const QString& session, int line, const QString& journal) {
    const std::vector<QJsonObject> lines = sessionLines(session, journal);
    if (lines.empty()) return "no session '" + session + "' in the journal (journal_sessions lists them)";
    int cut = line < 0 || line >= int(lines.size()) ? int(lines.size()) - 1 : line;
    if (cut + 1 < int(lines.size()) && lines[size_t(cut) + 1].value("op").toString() == "journal_snapshot") ++cut; // the change and its snapshot
    int from = cut;
    while (from >= 0 && lines[size_t(from)].value("op").toString() != "journal_snapshot") --from;
    if (from < 0) return "the session has no snapshot up to line " + QString::number(cut);
    const QJsonObject snap = lines[size_t(from)].value("args").toObject();
    QString why;
    if (!builder_->restoreSnapshot(snap.value("scene").toString(), snap.value("base").toString(), uint32_t(snap.value("next_id").toDouble()), why))
        return why;
    replaying_ = true;
    bool playing = false;
    int replayed = 0, failed = 0;
    for (int k = from + 1; k <= cut; ++k) {
        const QJsonObject& l = lines[size_t(k)];
        const QString op = l.value("op").toString();
        if (op == "sim_play" || (op == "sim_step" && l.value("ok").toBool())) playing = true;
        if (op == "sim_stop") playing = false;
        const ke::SceneOp* core = ke::findSceneOp(op.toStdString());
        if (playing || !l.value("ok").toBool() || !core || !core->edit) continue;
        (runNow(op, l.value("args").toObject()).value("ok").toBool() ? replayed : failed)++;
    }
    replaying_ = false;
    writeLine({{"src", "journal"}, {"op", "journal_restore"}, {"ok", failed == 0},
               {"args", QJsonObject{{"session", session}, {"line", cut}, {"from", from}, {"replayed", replayed}, {"failed", failed}}}});
    snapshot("restore");
    return failed ? QString::number(failed) + " edits did not replay" : QString();
}
