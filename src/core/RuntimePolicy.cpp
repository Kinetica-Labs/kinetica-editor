// Runtime limits are transactional and admission precedes starting a worker. The idle lease is a
// decision, not a hidden thread or a hard CPU quota: hosts perform hold/pause/release on expiry.
#include "core/RuntimePolicy.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ke {

const char* const kRuntimeStatusHelp =
    "Read active API resource usage, limits and monotonic idle lease without renewing it. "
    "Limits bound managed work, not operating-system CPU or memory usage.";
const char* const kRuntimeConfigureHelp =
    "Configure bounded API resources transactionally: max_fps in 1..60, idle_timeout_ms in 1000..3600000, "
    "max_background_tasks in 1..2. An arm with its camera consumes two tasks. Stop work before lowering "
    "a limit below its current use. Status polling does not renew the idle lease.";
const char* const kRuntimeConfigureArgs =
    "{max_fps?: number (60), idle_timeout_ms?: number (300000), max_background_tasks?: int (2)}";
const char* const kRuntimeStopHelp =
    "Hold managed arms at measured coordinates, stop managed stereo/perception, and pause the host without "
    "resetting the authored scene. Releases API work; never kills unrelated applications.";

RuntimePolicy::RuntimePolicy(Now now) : now_(now ? std::move(now) : Now([] { return Clock::now(); })), lastActivity_(now_()) {}

namespace {

bool readLimit(const Value& args, const char* name, double low, double high, double& out, std::string& error) {
    if (std::find(args.keys().begin(), args.keys().end(), name) == args.keys().end()) return true;
    const Value& value = args[name];
    if (value.type() != Value::Type::Number || !std::isfinite(value.number()) || value.number() < low || value.number() > high)
        return error = std::string(name) + " must be finite and in " + std::to_string(low) + ".." + std::to_string(high), false;
    out = value.number();
    return true;
}

} // namespace

bool RuntimePolicy::configure(const Value& args, const Usage& usage, std::string& error) {
    if (args.type() != Value::Type::Map) return error = "runtime configuration is an object", false;
    for (const std::string& key : args.keys())
        if (key != "max_fps" && key != "idle_timeout_ms" && key != "max_background_tasks")
            return error = "unknown runtime setting: " + key, false;
    Limits next = limits_;
    double tasks = next.maxBackgroundTasks;
    if (!readLimit(args, "max_fps", 1, 60, next.maxFps, error) ||
        !readLimit(args, "idle_timeout_ms", 1000, 3600000, next.idleTimeoutMs, error) ||
        !readLimit(args, "max_background_tasks", 1, 2, tasks, error)) return false;
    if (tasks != 1 && tasks != 2) return error = "max_background_tasks is an integer", false;
    next.maxBackgroundTasks = uint32_t(tasks);
    if (!allows(next, usage, error)) return false;
    limits_ = next;
    return true;
}

bool RuntimePolicy::allows(const Limits& limits, const Usage& usage, std::string& error) const {
    if (usage.streams > 1 || usage.armTasks > 1 || usage.perceptionWorkers > 1 || usage.pendingJobs > 2)
        return error = "runtime pipeline quota exceeded (one stream, one arm, one mapper, two pending jobs)", false;
    if (uint64_t(usage.streams) + uint64_t(usage.armTasks) > limits.maxBackgroundTasks)
        return error = "max_background_tasks exceeded: stop existing API work first", false;
    if (usage.perceptionWorkers && (!usage.streams || !usage.armTasks))
        return error = "arm perception requires a managed stream and arm task", false;
    if (usage.streams && (!std::isfinite(usage.streamFps) || usage.streamFps < 1 || usage.streamFps > limits.maxFps))
        return error = "stream fps exceeds runtime max_fps", false;
    return true;
}

bool RuntimePolicy::allowsFps(double fps, std::string& error) const {
    if (!std::isfinite(fps) || fps < 1 || fps > limits_.maxFps)
        return error = "fps must be finite and in 1..runtime.max_fps (at most 60)", false;
    return true;
}

bool RuntimePolicy::allowsUsage(const Usage& usage, std::string& error) const { return allows(limits_, usage, error); }

void RuntimePolicy::activity() {
    lastActivity_ = now_();
    leaseActive_ = true;
    stopReason_.clear();
}

void RuntimePolicy::stop(std::string reason) { leaseActive_ = false, stopReason_ = std::move(reason); }

double RuntimePolicy::idleMs() const {
    return std::max(0.0, std::chrono::duration<double, std::milli>(now_() - lastActivity_).count());
}

bool RuntimePolicy::expired() const { return leaseActive_ && idleMs() >= limits_.idleTimeoutMs; }

Value RuntimePolicy::status(const Usage& usage) const {
    Value out = Value::map(), limits = Value::map(), resources = Value::map();
    limits.set("max_fps", limits_.maxFps);
    limits.set("idle_timeout_ms", limits_.idleTimeoutMs);
    limits.set("max_background_tasks", limits_.maxBackgroundTasks);
    limits.set("max_streams", 1), limits.set("max_arm_tasks", 1), limits.set("max_perception_workers", 1), limits.set("max_pending_jobs", 2);
    resources.set("streams", usage.streams), resources.set("arm_tasks", usage.armTasks);
    resources.set("perception_workers", usage.perceptionWorkers), resources.set("pending_jobs", usage.pendingJobs);
    resources.set("background_tasks", int64_t(uint64_t(usage.streams) + uint64_t(usage.armTasks)));
    resources.set("stream_fps", usage.streams ? usage.streamFps : 0);
    out.set("limits", limits), out.set("resources", resources);
    out.set("lease_active", leaseActive_), out.set("idle_ms", idleMs());
    out.set("idle_expired", expired());
    out.set("lease_remaining_ms", leaseActive_ ? std::max(0.0, limits_.idleTimeoutMs - idleMs()) : 0);
    out.set("stop_reason", stopReason_), out.set("hard_cpu_limit", false);
    return out;
}

} // namespace ke
