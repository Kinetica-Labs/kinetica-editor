// The lease of an agent's session (EditorRuntime.h): intentional external activity renews a monotonic lease;
// status polling never keeps an orphaned hidden editor alive, and its expiry stops the session before the
// application's event loop exits. A visible window is never stopped while it has nothing running beside it.
#include "EditorRuntime.h"

#include <QCoreApplication>
#include <QPointer>
#include <QTimer>
#include <QWidget>

EditorRuntime::EditorRuntime(QWidget* window, QObject* parent) : QObject(parent), window_(window) {
    hidden_ = QCoreApplication::instance()->property("keAgentEval").toBool();
    auto* timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this] { tick(); });
    timer->start();
}

void EditorRuntime::request(const QString& name, const QJsonObject& args, const QString& source) {
    (void)args;
    const bool status = name == "runtime_status" || name == "sim_state" || name == "sim_measure";
    if (status || stopping_) return;
    if (source == "api" || source == "console") {
        lastExternal_ = ke::RuntimePolicy::Clock::now();
        policy_.activity();
    } else if (source == "ui" && !hidden_ && window_ && window_->isVisible() && !window_->isMinimized()) policy_.activity();
}

void EditorRuntime::stop(const QString& reason, Done done) {
    if (done) stopWaiters_.push_back(std::move(done));
    if (stopping_) return;
    ++*epoch_;
    policy_.stop(reason.toStdString());
    stopping_ = true;
    QPointer<EditorRuntime> self(this);
    auto finished = [self] {
        if (!self) return;
        self->stopping_ = false;
        auto waiters = std::move(self->stopWaiters_);
        self->stopWaiters_.clear();
        for (auto& waiter : waiters) if (waiter) waiter();
    };
    if (stop_) stop_(reason, std::move(finished));
    else finished();
}

void EditorRuntime::tick() {
    if (stopping_) return;
    const double externalIdle = externalIdleMs();
    const bool hiddenExpired = hidden_ && externalIdle >= policy_.limits().idleTimeoutMs;
    if (!hiddenExpired && !policy_.expired()) return;
    const auto usage = usage_ ? usage_() : ke::RuntimePolicy::Usage{};
    if (!hidden_ && !usage.streams && !usage.armTasks && !usage.perceptionWorkers) return;
    if (!hidden_ && usage.following) return; // a person's hand is the work it waits for
    const bool quit = hidden_;
    stop("idle_timeout", [quit] { if (quit) QCoreApplication::exit(0); });
}
