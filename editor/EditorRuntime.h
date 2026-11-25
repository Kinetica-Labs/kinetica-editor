#pragma once
// The Qt host of the core's bounded resource policy (core/RuntimePolicy.h): an agent's session holds a lease
// that intentional calls renew and status polls do not; a hidden editor (--agent-eval) whose lease runs out
// stops and quits. It owns one timer, never a worker of its own; the editor supplies the usage and performs
// the safe stop on its own threads (EditorOpsRuntime.cpp).
#include "core/RuntimePolicy.h"

#include <QJsonObject>
#include <QObject>
#include <functional>
#include <vector>
#include <atomic>
#include <memory>

class QWidget;

class EditorRuntime : public QObject {
public:
    using Done = std::function<void()>;
    using Stop = std::function<void(const QString&, Done)>;
    EditorRuntime(QWidget* window, QObject* parent);
    void setUsage(std::function<ke::RuntimePolicy::Usage()> usage) { usage_ = std::move(usage); }
    void setStop(Stop stop) { stop_ = std::move(stop); }
    void request(const QString& name, const QJsonObject& args, const QString& source);
    void stop(const QString& reason, Done done);
    ke::RuntimePolicy::Usage usage() const { return usage_ ? usage_() : ke::RuntimePolicy::Usage{}; }
    ke::RuntimePolicy& policy() { return policy_; }
    const ke::RuntimePolicy& policy() const { return policy_; }
    uint64_t epoch() const { return epoch_->load(); }
    std::shared_ptr<const std::atomic<uint64_t>> cancellationEpoch() const { return epoch_; }
    bool stopping() const { return stopping_; }
    bool hidden() const { return hidden_; }
    double externalIdleMs() const {
        return std::chrono::duration<double, std::milli>(ke::RuntimePolicy::Clock::now() - lastExternal_).count();
    }

private:
    void tick();
    QWidget* window_;
    ke::RuntimePolicy policy_;
    std::function<ke::RuntimePolicy::Usage()> usage_;
    Stop stop_;
    std::vector<Done> stopWaiters_;
    ke::RuntimePolicy::TimePoint lastExternal_ = ke::RuntimePolicy::Clock::now();
    std::shared_ptr<std::atomic<uint64_t>> epoch_ = std::make_shared<std::atomic<uint64_t>>(1);
    bool hidden_ = false, stopping_ = false;
};
