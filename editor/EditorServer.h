#pragma once
// The editor's API over the network: a TCP server on 127.0.0.1 only (another machine cannot reach
// it), one JSON object per line each way. A request {"id": 1, "op": "scene_put", "args": {...}} is
// answered by {"id": 1, "result": {"ok": true, ...}} - the answer of EditorOps under the request's
// id (as JSON-RPC does), so several requests may be in flight; an operation that waits for the
// simulation answers when it is done.
// The Python client (tools/ke_editor.py) and the MCP server for agents (tools/ke_editor_mcp.py)
// speak this; the editor starts it with --api [port].
// Another user of the same computer reaches 127.0.0.1 too, and an operation runs code (script_run, plugin_new) - so a
// connection is served once a request of it carries the token: the
// window's own (KE_API_TOKEN when the program that started it gave one, else made at random), in the file
// <app data>/api/<port>.token only its user may read and in the environment of the programs the editor starts.
#include <QObject>
#include <QSet>

#include <functional>

class EditorOps;
class QTcpServer;
class QTcpSocket;

class EditorServer : public QObject {
public:
    EditorServer(EditorOps* ops, quint16 port, QObject* parent);
    ~EditorServer() override;
    bool listening() const;
    quint16 port() const;
    static QString token(); // this run's: the same for every server and every program the editor starts
    // Called at every request: the window shows that something outside drives it (MainWindowApi.cpp).
    void setOnActivity(std::function<void()> f) { onActivity_ = std::move(f); }

private:
    void accept();
    void readLines(QTcpSocket* socket);

    QSet<QTcpSocket*> sockets_, trusted_; // trusted: a request of it carried the token
    QString tokenFile_;
    int pendingRequests_ = 0;
    quint64 answers_ = 0; // the requests answered so far: one answered at once may have its next on the wire already
    bool stopPending_ = false;
    EditorOps* ops_;
    QTcpServer* server_;
    std::function<void()> onActivity_;
};
