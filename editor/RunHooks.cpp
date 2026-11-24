// The run's hooks on their own wire and thread (RunHooks.h): the listener, the hello, the rounds of an event.
#include "RunHooks.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>
#include <cmath>

namespace {

void say(QTcpSocket* socket, const QJsonObject& what) {
    socket->write(QJsonDocument(what).toJson(QJsonDocument::Compact) + "\n");
    socket->flush();
}

} // namespace

RunHooks::RunHooks() {
    moveToThread(&thread_);
    thread_.setObjectName("run-hooks");
    thread_.start();
    QMetaObject::invokeMethod(this, [this] { // the listener belongs to this thread; the port is known when this returns
        server_ = new QTcpServer(this);
        connect(server_, &QTcpServer::newConnection, this, &RunHooks::accept);
        server_->listen(QHostAddress::LocalHost, 0);
        port_ = server_->serverPort();
    }, Qt::BlockingQueuedConnection);
}

RunHooks::~RunHooks() {
    QMetaObject::invokeMethod(this, [this] { // the sockets end on their thread; the object goes back to the one that deletes it
        for (Wire& w : wires_) delete w.socket;
        wires_.clear(), rounds_.clear();
        delete server_;
        moveToThread(QCoreApplication::instance()->thread());
    }, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
}

void RunHooks::expect(const QString& plugin, const QString& token) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (token.isEmpty()) tokens_.erase(plugin);
        else tokens_[plugin] = token;
    }
    if (!token.isEmpty()) return;
    QMetaObject::invokeMethod(this, [this, plugin] {
        for (Wire& w : wires_)
            if (w.plugin == plugin) return close(w.socket);
    }, Qt::QueuedConnection);
}

bool RunHooks::ticking(bool world) const { return (world ? worlds_ : tickers_) > 0; }

void RunHooks::send(const QString& event, const QJsonObject& args, std::function<void(QJsonObject)> done) {
    QMetaObject::invokeMethod(this, [this, event, args, done = std::move(done)] { tell(event, args, done); }, Qt::QueuedConnection);
}

void RunHooks::accept() {
    while (QTcpSocket* socket = server_->nextPendingConnection()) {
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1); // a tick's line goes at once, not with the next
        socket->setReadBufferSize(1024 * 1024 + 1);
        wires_.push_back({socket, {}, {}, {}});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { close(socket); });
    }
}

RunHooks::Wire* RunHooks::find(QTcpSocket* socket) {
    for (Wire& w : wires_)
        if (w.socket == socket) return &w;
    return nullptr;
}

void RunHooks::read(QTcpSocket* socket) {
    constexpr qint64 maxLine = 1024 * 1024;
    while (Wire* w = find(socket)) { // an answer may close the wire: looked up again every line
        if (!socket->canReadLine()) {
            if (socket->bytesAvailable() >= maxLine) close(socket);
            return;
        }
        const QJsonObject line = QJsonDocument::fromJson(socket->readLine(maxLine + 1)).object();
        if (w->plugin.isEmpty()) hello(*w, line);
        else answered(*w, line);
    }
}

void RunHooks::hello(Wire& w, const QJsonObject& said) {
    const QString plugin = said.value("name").toString();
    bool known = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        const auto it = tokens_.find(plugin);
        known = it != tokens_.end() && it->second == said.value("token").toString();
    }
    if (!known) {
        say(w.socket, {{"ok", false}, {"error", "a plugin speaks with the name and the token the editor started it with"}});
        return close(w.socket);
    }
    QTcpSocket* socket = w.socket;
    for (Wire& old : wires_) // started again before its old process went: the new one's wire takes its place
        if (old.plugin == plugin) {
            close(old.socket);
            break;
        }
    Wire* mine = find(socket); // close() moved the wires
    mine->plugin = plugin;
    for (const QJsonValue& h : said.value("hooks").toArray()) mine->hooks << h.toString();
    count();
    say(socket, {{"ok", true}});
}

void RunHooks::count() {
    int tickers = 0, worlds = 0;
    for (const Wire& w : wires_) tickers += w.hooks.contains("tick"), worlds += w.hooks.contains("tick:world");
    tickers_ = tickers, worlds_ = worlds;
}

void RunHooks::tell(const QString& event, const QJsonObject& args, std::function<void(QJsonObject)> done) {
    const int call = ++nextCall_;
    Round round{event, args.value("time").toDouble(std::nan("")), 0, {}, std::move(done)};
    for (Wire& w : wires_) {
        if (!w.hooks.contains(event) || w.owed.size() >= kOwedMost) continue;
        QJsonObject given = args; // the truth of the bodies only to a physics plugin; a controller sees its sensors
        if (!w.hooks.contains("tick:world")) given.remove("bodies");
        say(w.socket, {{"call", call}, {"event", event}, {"args", given}});
        w.owed.push_back(call);
        ++round.waiting;
    }
    if (round.waiting == 0) {
        if (round.done) round.done({});
        return;
    }
    rounds_.emplace(call, std::move(round));
}

void RunHooks::answered(Wire& w, const QJsonObject& answer) {
    const int call = answer.value("call").toInt();
    const auto owed = std::find(w.owed.begin(), w.owed.end(), call);
    const auto round = rounds_.find(call);
    if (owed == w.owed.end() || round == rounds_.end()) return;
    w.owed.erase(owed);
    const QString event = round->second.event;
    if (answer.contains("error")) {
        emit failed(w.plugin, event, answer.value("error").toString());
    } else {
        const QJsonObject commands = answer.value("commands").toObject();
        for (auto it = commands.begin(); it != commands.end(); ++it) round->second.commands.insert(it.key(), it.value());
        if (const QJsonArray pictures = answer.value("draw").toArray(); !pictures.isEmpty()) emit drew(w.plugin, event, pictures);
        if (const QJsonObject values = answer.value("show").toObject(); !values.isEmpty()) emit showed(w.plugin, event, round->second.time, values);
    }
    settle(call);
}

void RunHooks::settle(int call) {
    const auto round = rounds_.find(call);
    if (round == rounds_.end() || --round->second.waiting > 0) return;
    const Round over = std::move(round->second);
    rounds_.erase(round);
    if (over.done) over.done(over.commands);
}

void RunHooks::close(QTcpSocket* socket) {
    const auto it = std::find_if(wires_.begin(), wires_.end(), [socket](const Wire& w) { return w.socket == socket; });
    if (it == wires_.end()) return;
    const std::vector<int> owed = std::move(it->owed);
    wires_.erase(it);
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
    count();
    for (int call : owed) settle(call); // the rounds it was in do not wait for it
}
