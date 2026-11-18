#pragma once
// The editor's API over the network: a TCP server on 127.0.0.1 only (another machine cannot reach
// it), one JSON object per line each way. A request {"id": 1, "op": "scene_put", "args": {...}} is
// answered by {"id": 1, "result": {"ok": true, ...}} - the answer of EditorOps under the request's
// id (as JSON-RPC does), so several requests may be in flight; an operation that waits for the
// simulation answers when it is done.
// The Python client (tools/ke_editor.py) and the MCP server for agents (tools/ke_editor_mcp.py)
// speak this; the editor starts it with --api [port].
#include <QObject>
#include <QSet>

#include <functional>

class EditorOps;
class QTcpServer;
class QTcpSocket;

class EditorServer : public QObject {
public:
    EditorServer(EditorOps* ops, quint16 port, QObject* parent);
    bool listening() const;
    quint16 port() const;
    // Called at every request: the window shows that something outside drives it (MainWindowApi.cpp).
    void setOnActivity(std::function<void()> f) { onActivity_ = std::move(f); }

private:
    void accept();
    void readLines(QTcpSocket* socket);

    QSet<QTcpSocket*> sockets_;
    int pendingRequests_ = 0;
    bool stopPending_ = false;
    EditorOps* ops_;
    QTcpServer* server_;
    std::function<void()> onActivity_;
};
