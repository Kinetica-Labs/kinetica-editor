// JSON lines over TCP on 127.0.0.1 into EditorOps (EditorServer.h).
#include "EditorServer.h"

#include "EditorOps.h"

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QPointer>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

EditorServer::EditorServer(EditorOps* ops, quint16 port, QObject* parent)
    : QObject(parent), ops_(ops), server_(new QTcpServer(this)) {
    connect(server_, &QTcpServer::newConnection, this, &EditorServer::accept);
    if (!server_->listen(QHostAddress::LocalHost, port)) return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/api";
    QDir().mkpath(dir);
    QFile file(dir + "/" + QString::number(server_->serverPort()) + ".token");
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        // Before the token is in it. Windows keeps a user's AppData to that user by its own rights, whatever this sets.
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        file.write(token().toUtf8());
        tokenFile_ = file.fileName();
    }
}

// The token goes with the server: a file left behind would name a port someone else may take next.
EditorServer::~EditorServer() {
    if (!tokenFile_.isEmpty()) QFile::remove(tokenFile_);
}

QString EditorServer::token() {
    static const QString made = [] {
        const QString given = qEnvironmentVariable("KE_API_TOKEN");
        if (!given.isEmpty()) return given;
        QString out;
        for (int k = 0; k < 4; ++k) out += QString::number(QRandomGenerator::system()->generate64(), 16).rightJustified(16, '0');
        return out;
    }();
    return made;
}

// Compared in a time that does not depend on where the first wrong character is.
static bool sameToken(const QString& given, const QString& token) {
    const QByteArray a = given.toUtf8(), b = token.toUtf8();
    int diff = a.size() ^ b.size();
    for (int k = 0; k < b.size(); ++k) diff |= (k < a.size() ? a[k] : 0) ^ b[k];
    return diff == 0;
}

bool EditorServer::listening() const { return server_->isListening(); }
quint16 EditorServer::port() const { return server_->serverPort(); }

void EditorServer::accept() {
    while (QTcpSocket* socket = server_->nextPendingConnection()) {
        if (sockets_.size() >= 32) { // the console, MCP, scripts and a connection per plugin
            socket->write("{\"result\":{\"ok\":false,\"error\":\"connection limit is 32\"}}\n");
            socket->disconnectFromHost();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            continue;
        }
        sockets_.insert(socket);
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1); // an answer goes at once: several in a row waited 40 ms
        socket->setReadBufferSize(1024 * 1024 + 1);                  // for the first one's acknowledgement (Nagle)
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { sockets_.remove(socket), trusted_.remove(socket); });
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readLines(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

// A client that calls in a row - a plugin's command, a script's loop - is answered in a row: its next call is on the wire a
// fraction of a millisecond after an answer, and going back to the event loop first would paint the window before
// reading it - a painted frame a call (half of an API edit's time, 37 of 70 samples). So after an answer the socket is
// listened to for a millisecond; 16 calls a turn at most (readLines), then the window paints.
static bool nextInARow(QTcpSocket* socket) { return socket->waitForReadyRead(1) && socket->canReadLine(); }

// Every whole line is one request; a line not yet ended waits for the rest. The answer goes back to
// the socket that asked, if it is still there when an operation that waits is done.
// An operation may run an event loop of its own (a menu opened through the API waits for its
// release), and the socket can go away inside it: the loop asks again whether the socket lives.
void EditorServer::readLines(QTcpSocket* socket) {
    QPointer<QTcpSocket> alive(socket);
    constexpr qint64 maxLine = 1024 * 1024;
    int remaining = 16;
    bool atOnce = true; // every request of this turn was answered before the next was read
    while (alive && remaining-- > 0 && (alive->canReadLine() || (atOnce && remaining < 15 && nextInARow(alive)))) {
        const QByteArray raw = alive->readLine(maxLine + 1);
        if (raw.size() > maxLine || !raw.endsWith('\n')) { alive->disconnectFromHost(); return; }
        const QByteArray line = raw.trimmed();
        if (line.isEmpty()) continue;
        QJsonParseError parseError;
        const QJsonObject request = QJsonDocument::fromJson(line, &parseError).object();
        const QJsonValue id = request.value("id");
        QPointer<QTcpSocket> to(socket);
        auto reply = [to, id](const QJsonObject& answer) {
            if (!to) return;
            const QJsonObject out{{"id", id}, {"result", answer}}; // the answer's own fields (an "id" too) stay apart
            const QByteArray payload = QJsonDocument(out).toJson(QJsonDocument::Compact) + "\n";
            constexpr qint64 maxQueuedReply = 8 * 1024 * 1024;
            if (to->bytesToWrite() > maxQueuedReply - payload.size()) { to->abort(); return; }
            to->write(payload);
            to->flush();
        };
        if (parseError.error != QJsonParseError::NoError) { reply(EditorOps::error("not JSON: " + parseError.errorString())); continue; }
        if (!trusted_.contains(socket)) {
            if (!sameToken(request.value("token").toString(), token())) {
                reply(EditorOps::error("the API asks for its token: \"token\" in a connection's first request - KE_API_TOKEN in a "
                                       "program the editor started, else the file " + (tokenFile_.isEmpty() ? QString("<app data>/api/<port>.token") : tokenFile_) +
                                       " (its user's only)"));
                continue;
            }
            trusted_.insert(socket);
        }
        const QString src = request.value("src").toString();
        const bool console = src == "console", plugin = src.startsWith("plugin:");
        // The editor's own console and its plugins are not an agent driving, nor a hand and an eye (ui_*, view_*): a tester
        // acting as a person through them saw the plate and lost the gizmo, which a person never does.
        const QString op = request.value("op").toString();
        const bool hand = op.startsWith("ui_") || op == "view_screenshot" || op == "view_project";
        if (onActivity_ && !console && !plugin && !hand) onActivity_();
        const bool stopping = request.value("op").toString() == "runtime_stop";
        const bool waits = request.value("op").toString() == "plugin_wait"; // one per plugin, held until a call: no slot of the queue
        if ((stopping && stopPending_) || (!stopping && !waits && pendingRequests_ >= 8)) {
            reply(EditorOps::error("API request queue is full; wait for pending answers"));
            continue;
        }
        if (!waits) ++pendingRequests_;
        if (stopping) stopPending_ = true; // a reserved ninth slot keeps stop available under load
        QPointer<EditorServer> owner(this);
        const quint64 answered = answers_;
        ops_->run(request.value("op").toString(), request.value("args").toObject(), [owner, stopping, waits, reply](const QJsonObject& answer) {
            if (owner && !waits) --owner->pendingRequests_;
            if (owner && stopping) owner->stopPending_ = false;
            if (owner) ++owner->answers_;
            reply(answer);
        }, console ? QString("console") : plugin ? src : QString("api"));
        atOnce = answers_ != answered;
    }
    if (!alive) return;
    if (!alive->canReadLine() && alive->bytesAvailable() >= maxLine) {
        alive->disconnectFromHost();
        return;
    }
    if (alive->canReadLine()) QTimer::singleShot(0, this, [this, alive] { if (alive) readLines(alive); });
}
