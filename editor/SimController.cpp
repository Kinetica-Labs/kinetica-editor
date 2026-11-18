// The simulation thread (see SimController.h): a loop that runs the posted commands, steps a frame
// when running (or when one step is asked for), publishes a fresh snapshot, and waits so that a
// scene does not run faster than the wall clock.
#include "SimController.h"

#include <algorithm>

#include <chrono>

SimController::SimController() {
    sim_ = std::make_unique<ke::Simulation>();
    publish(false);
    thread_ = std::thread([this] { loop(); });
}

SimController::~SimController() {
    quit_ = true;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void SimController::post(Command cmd, const char* label) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        queue_.emplace_back(std::move(cmd), label);
    }
    cv_.notify_all();
}

void SimController::setSceneName(std::string name) {
    std::lock_guard<std::mutex> lk(snapMtx_);
    sceneName_ = std::move(name);
}

void SimController::setRunning(bool on) {
    running_ = on;
    cv_.notify_all();
}

void SimController::requestStep() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        ++stepRequests_;
    }
    cv_.notify_all();
}

std::shared_ptr<const ke::Frame> SimController::snapshot() const {
    std::lock_guard<std::mutex> lk(snapMtx_);
    return snap_;
}

void SimController::publish(bool stepped) {
    auto s = std::make_shared<ke::Frame>();
    sim_->fill(*s);
    std::lock_guard<std::mutex> lk(snapMtx_);
    s->sceneName = sceneName_;
    if (stepped) readings_.push_back({sim_->frame(), double(sim_->time()), s->sceneName, s->probe.channels, s->measurements});
    if (readings_.size() > kReadingsKept) readings_.pop_front();
    published_.push_back(s);
    if (published_.size() > kPublishedKept) published_.pop_front();
    snap_ = std::move(s);
    ++serial_;
}

std::vector<SimController::Reading> SimController::takeReadings() {
    std::lock_guard<std::mutex> lk(snapMtx_);
    std::vector<Reading> out(std::make_move_iterator(readings_.begin()), std::make_move_iterator(readings_.end()));
    readings_.clear();
    return out;
}

std::vector<std::shared_ptr<const ke::Frame>> SimController::takePublished() {
    std::lock_guard<std::mutex> lk(snapMtx_);
    std::vector<std::shared_ptr<const ke::Frame>> out(published_.begin(), published_.end());
    published_.clear();
    return out;
}

void SimController::loop() {
    using clock = std::chrono::steady_clock;
    while (!quit_) {
        std::deque<std::pair<Command, const char*>> cmds;
        bool doStep = false;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait_for(lk, std::chrono::milliseconds(50),
                         [&] { return quit_ || !queue_.empty() || running_ || stepRequests_ > 0; });
            if (quit_) break;
            cmds.swap(queue_);
            if (running_) doStep = true;
            else if (stepRequests_ > 0) { --stepRequests_; doStep = true; }
        }
        const auto c0 = clock::now();
        double slowest = 0;
        const char* slowLabel = "";
        for (auto& c : cmds) {
            const auto s0 = clock::now();
            c.first(*sim_);
            const double ms = std::chrono::duration<double, std::milli>(clock::now() - s0).count();
            if (ms > slowest) slowest = ms, slowLabel = c.second;
        }
        if (!doStep && cmds.empty()) continue;

        auto t0 = clock::now();
        if (doStep) sim_->stepFrame();
        const auto t1 = clock::now();
        publish(doStep);
        if (doStep) {
            const auto ms = [](clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
            std::lock_guard<std::mutex> lk(snapMtx_);
            costs_.push_back({ms(t0 - c0), ms(t1 - t0), ms(clock::now() - t1), int(cmds.size()), slowest, slowLabel});
            if (costs_.size() > size_t(kTimingFrames)) costs_.pop_front();
            frameDtMs_ = 1000.0 * sim_->frameDt;
        }

        // Optional real-time cap: do not run a scene faster than the wall clock.
        if (doStep && realtime_) {
            auto target = t0 + std::chrono::microseconds(int(sim_->frameDt * 1e6f));
            std::this_thread::sleep_until(target);
        }
    }
}

SimController::Timing SimController::timing() const {
    std::lock_guard<std::mutex> lk(snapMtx_);
    Timing t;
    t.frames = int(costs_.size());
    t.frameDtMs = frameDtMs_;
    for (const auto& c : costs_) {
        t.commandsMs += c.commands, t.stepMs += c.step, t.publishMs += c.publish;
        t.stepMaxMs = std::max(t.stepMaxMs, c.step);
        t.commandsPerFrame += c.count;
        if (c.slowest > t.slowestCommandMs) t.slowestCommandMs = c.slowest, t.slowestCommand = c.label;
    }
    if (t.frames) t.commandsPerFrame /= t.frames;
    if (t.frames) t.commandsMs /= t.frames, t.stepMs /= t.frames, t.publishMs /= t.frames;
    return t;
}
