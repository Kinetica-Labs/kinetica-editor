#pragma once
// The plugins' hooks of a run - on_play, on_tick, on_stop (clients/python/kinetica/plugin.py) - over a wire and a thread of their
// own, apart from the window's. The lockstep is a loop of the simulation and the controllers: the simulation's thread
// hands a stepped frame's packet here (SimController::setLockstep), this thread writes it to every plugin's wire and
// reads the answers, and their merged commands go back to the simulation's thread. The window's thread is not in the
// loop: it waits for the screen at every paint, and a tick that woke it three times cost 33-61 ms a frame (measured,
// UR5e, an empty on_tick). What the window does with an answer - its pictures over the view, its panel's values, its
// error on the banner - comes to it as signals, after the fact. The commands of a plugin (plugin_wait, PluginHost.h) stay on the API's wire.
//
// The wire, JSON lines on 127.0.0.1: a plugin with a hook connects to port() (KE_RUN_PORT of its process) and says who
// it is - {"name", "token", "hooks": ["play", "tick", "tick:world", "stop"]} -, answered {"ok": true}. Then one line each
// way an event: {"call": n, "event": "tick", "args": {time, dt, frame, sensors, bodies?}} out, and back
// {"call": n, "commands": {id: {target | velocity | duty | force, torque}}, "draw": [...], "show": {name: value}?} or
// {"call": n, "error": "plugin.py:12: ..."}. One plugin's events go down one wire in order: play, every tick, stop.
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QThread>

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

class QTcpServer;
class QTcpSocket;

class RunHooks : public QObject {
    Q_OBJECT
public:
    RunHooks();
    ~RunHooks() override;
    quint16 port() const { return port_; }
    // A plugin's process was started with this token: the hello of that name is taken with it only. Empty: the plugin
    // is gone, its wire closed. From any thread.
    void expect(const QString& plugin, const QString& token);
    bool ticking(bool world = false) const; // a plugin on the wire has an on_tick (world: one that extends the physics)
    // An event to every plugin on the wire with a hook for it - "play", "tick" {time, dt, frame, sensors, bodies?},
    // "stop" - and `done`, on this object's thread, once all of them answered or went, with their commands merged
    // ({id: {target | velocity | duty}}); none to tell: at once. From any thread; events sent from one thread keep
    // their order.
    void send(const QString& event, const QJsonObject& args = {}, std::function<void(QJsonObject commands)> done = {});

signals:
    void drew(const QString& plugin, const QString& event, const QJsonArray& pictures); // what a hook drew (kinetica.plugin.draw)
    // What a hook showed on its plugin's panel (kinetica.plugin.show), and its frame's time (NaN: not a frame's hook).
    void showed(const QString& plugin, const QString& event, double time, const QJsonObject& values);
    void failed(const QString& plugin, const QString& event, const QString& error);     // the author's mistake, with its line

private:
    struct Wire {
        QTcpSocket* socket = nullptr;
        QString plugin;         // empty until its hello is taken
        QStringList hooks;
        std::vector<int> owed;  // the calls sent to it, not answered yet
    };
    struct Round {
        QString event;
        double time = 0; // of the frame it is about (NaN: none)
        int waiting = 0;
        QJsonObject commands;
        std::function<void(QJsonObject)> done;
    };
    void accept();
    void read(QTcpSocket* socket);
    void hello(Wire& w, const QJsonObject& said);
    void answered(Wire& w, const QJsonObject& answer);
    void settle(int call); // one of a round's plugins answered or went
    void close(QTcpSocket* socket);
    void tell(const QString& event, const QJsonObject& args, std::function<void(QJsonObject)> done);
    void count();
    Wire* find(QTcpSocket* socket);

    static constexpr size_t kOwedMost = 240; // a plugin that owes this many answers hangs: no more events until it answers

    QThread thread_;
    quint16 port_ = 0;
    QTcpServer* server_ = nullptr; // these four on thread_
    std::vector<Wire> wires_;
    std::map<int, Round> rounds_;  // the events sent, not answered by all yet
    int nextCall_ = 0;
    mutable std::mutex mtx_;
    std::map<QString, QString> tokens_; // under mtx_
    std::atomic<int> tickers_{0}, worlds_{0};
};
