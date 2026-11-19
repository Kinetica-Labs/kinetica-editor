// Runtime resource limits against exact reference counts, and lease expiry against an injected
// monotonic clock. No wall sleeps, worker threads, GUI processes, or performance thresholds.
#include "TestRunner.h"
#include "core/RuntimePolicy.h"

#include <limits>

using namespace ke;

void testRuntimePolicyBounds() {
    RuntimePolicy policy;
    std::string error;
    RuntimePolicy::Usage usage;
    usage.streams = usage.armTasks = usage.perceptionWorkers = 1;
    usage.pendingJobs = 2, usage.streamFps = 60;
    CHECK(policy.allowsUsage(usage, error), "the one-camera arm pipeline must fit defaults: %s", error.c_str());
    for (double fps : {0.0, 60.01, 120.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        CHECK(!policy.allowsFps(fps, error), "unbounded fps accepted: %g", fps);
    CHECK(policy.allowsFps(1, error) && policy.allowsFps(60, error), "valid boundary fps rejected");
    auto tooMany = usage;
    tooMany.streams = 2;
    CHECK(!policy.allowsUsage(tooMany, error), "two streams must not bypass the session quota");
    tooMany = usage, tooMany.armTasks = 2;
    CHECK(!policy.allowsUsage(tooMany, error), "two arms must not bypass the session quota");
    tooMany = usage, tooMany.pendingJobs = 3;
    CHECK(!policy.allowsUsage(tooMany, error), "unbounded pending jobs accepted");
    tooMany = usage, tooMany.perceptionWorkers = 2;
    CHECK(!policy.allowsUsage(tooMany, error), "two mapper workers accepted");
    tooMany = usage, tooMany.streams = 0;
    CHECK(!policy.allowsUsage(tooMany, error), "a mapper without its stream accepted");
    Value limits = Value::map();
    limits.set("max_background_tasks", 1);
    CHECK(!policy.configure(limits, usage, error), "a running arm+camera must not be admitted into one slot");
    CHECK(policy.limits().maxBackgroundTasks == 2, "failed reconfiguration modified quota");
}

void testRuntimePolicyTransactional() {
    RuntimePolicy policy;
    const RuntimePolicy::Usage empty;
    std::string error;
    for (const auto& bad : {Value(), Value(false), Value("60"), Value(0), Value(61), Value(std::numeric_limits<double>::quiet_NaN())}) {
        Value args = Value::map();
        args.set("max_fps", bad);
        CHECK(!policy.configure(args, empty, error), "invalid max_fps accepted");
        CHECK(policy.limits().maxFps == 60, "failed setting changed max_fps");
    }
    Value args = Value::map();
    args.set("max_fps", 10), args.set("idle_timeout_ms", 2000), args.set("max_background_tasks", 1.5);
    CHECK(!policy.configure(args, empty, error), "fractional task count accepted");
    CHECK(policy.limits().maxFps == 60 && policy.limits().idleTimeoutMs == 300000, "partial configuration leaked before validation");
    args.set("max_background_tasks", 1);
    CHECK(policy.configure(args, empty, error), "valid bounded settings rejected: %s", error.c_str());
    CHECK(policy.limits().maxFps == 10 && policy.limits().idleTimeoutMs == 2000 && policy.limits().maxBackgroundTasks == 1,
          "valid bounded settings did not apply together");
    RuntimePolicy::Usage running;
    running.streams = 1, running.streamFps = 10;
    args = Value::map(), args.set("max_fps", 5);
    CHECK(!policy.configure(args, running, error) && policy.limits().maxFps == 10, "lowering limit silently changed live usage");
    args = Value::map(), args.set("disable_limits", true);
    CHECK(!policy.configure(args, empty, error), "unknown setting silently accepted");
    args = Value::map(), args.set("idle_timeout_ms", 3600001);
    CHECK(!policy.configure(args, empty, error), "unbounded lease accepted");
    args.set("idle_timeout_ms", 999);
    CHECK(!policy.configure(args, empty, error), "invalid short lease accepted");
}

void testRuntimePolicyLease() {
    RuntimePolicy::TimePoint now{};
    RuntimePolicy policy([&] { return now; });
    const RuntimePolicy::Usage usage;
    const auto advance = [&](int ms) { now += std::chrono::milliseconds(ms); };
    advance(299999);
    const Value before = policy.status(usage);
    CHECK(!policy.expired() && before["lease_remaining_ms"].number() == 1, "lease expired before exact deadline");
    advance(1);
    CHECK(policy.expired(), "status query renewed the lease");
    policy.activity();
    advance(1000);
    CHECK(!policy.expired() && policy.status(usage)["idle_ms"].number() == 1000, "intentional activity did not renew the monotonic lease");
    policy.stop("client_disconnected");
    advance(300000);
    const Value stopped = policy.status(usage);
    CHECK(!policy.expired() && !stopped["lease_active"].truth() && stopped["lease_remaining_ms"].number() == 0,
          "released work remains leased");
    CHECK(stopped["stop_reason"].text() == "client_disconnected" && !stopped["hard_cpu_limit"].truth(), "stop reason or quota scope is misleading");
    policy.activity();
    CHECK(!policy.expired() && policy.status(usage)["lease_active"].truth(), "new explicit activity cannot restart a released session");
}
