#pragma once
// Bounded resource and idle-lease policy shared by native hosts. This object owns no thread or
// process: a host supplies current usage, renews intentional activity, and executes safe shutdown.
// The lease uses monotonic time; an injected clock makes expiry independent of test-machine load.
// Calls are serialized by the host's control thread; no physics or worker thread accesses this policy.
#include "core/Value.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace ke {

class RuntimePolicy {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Now = std::function<TimePoint()>;

    struct Limits {
        double maxFps = 60;
        double idleTimeoutMs = 60000;
        uint32_t maxBackgroundTasks = 2;
    };
    struct Usage {
        uint32_t streams = 0, armTasks = 0, perceptionWorkers = 0, pendingJobs = 0;
        double streamFps = 0;
        bool following = false; // an arm follows an IK goal a person moves (arm_control move {goal}): waiting is its work
    };

    explicit RuntimePolicy(Now now = {});
    const Limits& limits() const { return limits_; }
    bool configure(const Value& args, const Usage& usage, std::string& error);
    bool allowsFps(double fps, std::string& error) const;
    bool allowsUsage(const Usage& usage, std::string& error) const;
    void activity(); // intentional use, not a status query
    void stop(std::string reason = "explicit_stop");
    bool expired() const;
    Value status(const Usage& usage) const;

private:
    bool allows(const Limits& limits, const Usage& usage, std::string& error) const;
    double idleMs() const;
    Limits limits_;
    Now now_;
    TimePoint lastActivity_;
    bool leaseActive_ = true;
    std::string stopReason_;
};

extern const char* const kRuntimeStatusHelp;
extern const char* const kRuntimeConfigureHelp;
extern const char* const kRuntimeConfigureArgs;
extern const char* const kRuntimeStopHelp;

} // namespace ke
