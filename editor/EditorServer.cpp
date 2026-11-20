// JSON lines over TCP on 127.0.0.1 into EditorOps (EditorServer.h).
#include "EditorServer.h"

#include "EditorOps.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

EditorServer::EditorServer(EditorOps* ops, quint16 port, QObject* parent)
    : QObject(parent), ops_(ops), server_(new QTcpServer(this)) {
    connect(server_, &QTcpServer::newConnection, this, &EditorServer::accept);
    server_->listen(QHostAddress::LocalHost, port);
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
        socket->setReadBufferSize(1024 * 1024 + 1);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { sockets_.remove(socket); });
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readLines(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

// Every whole line is one request; a line not yet ended waits for the rest. The answer goes back to
// the socket that asked, if it is still there when an operation that waits is done.
// An operation may run an event loop of its own (a menu opened through the API waits for its
// release), and the socket can go away inside it: the loop asks again whether the socket lives.
void EditorServer::readLines(QTcpSocket* socket) {
    QPointer<QTcpSocket> alive(socket);
    constexpr qint64 maxLine = 1024 * 1024;
    int remaining = 16;
    while (alive && alive->canReadLine() && remaining-- > 0) {
        const QByteArray raw = alive->readLine(maxLine + 1);
        if (raw.size() > maxLine || !raw.endsWith('\n')) { alive->disconnectFromHost(); return; }
        const QByteArray line = raw.trimmed();
        if (line.isEmpty()) continue;
        QJsonParseError parseError;
        const QJsonObject request = QJsonDocument::fromJson(line, &parseError).object();
        const QString src = request.value("src").toString();
        const bool console = src == "console", plugin = src.startsWith("plugin:");
        // The editor's own console and its plugins are not an agent driving, nor a hand and an eye (ui_*, view_*): a tester
        // acting as a person through them saw the plate and lost the gizmo, which a person never does.
        const QString op = request.value("op").toString();
        const bool hand = op.startsWith("ui_") || op == "view_screenshot" || op == "view_project";
        if (onActivity_ && !console && !plugin && !hand) onActivity_();
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
        const bool stopping = request.value("op").toString() == "runtime_stop";
        const bool waits = request.value("op").toString() == "plugin_wait"; // one per plugin, held until a call: no slot of the queue
        if ((stopping && stopPending_) || (!stopping && !waits && pendingRequests_ >= 8)) {
            reply(EditorOps::error("API request queue is full; wait for pending answers"));
            continue;
        }
        if (!waits) ++pendingRequests_;
        if (stopping) stopPending_ = true; // a reserved ninth slot keeps stop available under load
        QPointer<EditorServer> owner(this);
        ops_->run(request.value("op").toString(), request.value("args").toObject(), [owner, stopping, waits, reply](const QJsonObject& answer) {
            if (owner && !waits) --owner->pendingRequests_;
            if (owner && stopping) owner->stopPending_ = false;
            reply(answer);
        }, console ? QString("console") : plugin ? src : QString("api"));
    }
    if (!alive) return;
    if (!alive->canReadLine() && alive->bytesAvailable() >= maxLine) {
        alive->disconnectFromHost();
        return;
    }
    if (alive->canReadLine()) QTimer::singleShot(0, this, [this, alive] { if (alive) readLines(alive); });
}
