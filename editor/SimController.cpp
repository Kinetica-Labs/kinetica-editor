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

void SimController::setLockstep(std::function<void(ke::Simulation&)> onFrame, int patienceMs) {
    std::lock_guard<std::mutex> hand(handMtx_); // a frame being handed over is handed first: none after an empty one is set
    {
        std::lock_guard<std::mutex> lk(mtx_);
        onFrame_ = std::move(onFrame);
        patienceMs_ = patienceMs;
        awaiting_ = false;
        if (onFrame_) // the frame the run starts from is the first one handed over: the controllers answer it before the first
            queue_.emplace_back([this](ke::Simulation&) { handOver(); }, "lockstep"); // step (a force from the first frame on)
    }
    cv_.notify_all();
}

void SimController::handOver() {
    std::lock_guard<std::mutex> hand(handMtx_);
    std::function<void(ke::Simulation&)> onFrame;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        onFrame = onFrame_, awaiting_ = bool(onFrame_), handed_ = sim_->frame();
    }
    if (onFrame) onFrame(*sim_);
}

void SimController::release(uint64_t frame) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (frame == handed_) awaiting_ = false;
    }
    cv_.notify_all();
}

void SimController::awaitControllers() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(patienceMs_);
    std::unique_lock<std::mutex> lk(mtx_);
    while (awaiting_ && onFrame_ && !quit_) {
        if (!cv_.wait_until(lk, deadline, [&] { return quit_ || !awaiting_ || !queue_.empty() || !onFrame_; })) break; // too slow
        std::deque<std::pair<Command, const char*>> cmds;
        cmds.swap(queue_);
        lk.unlock();
        for (auto& c : cmds) c.first(*sim_); // their questions answered, their commands applied before the next step
        lk.lock();
    }
    if (awaiting_) ++late_, awaiting_ = false;
}

void SimController::stepFrames(int n, std::function<bool()> cancelled, std::function<void(ke::Simulation&, int)> after) {
    if (n <= 0) return post([after](ke::Simulation& s) { after(s, 0); }, "steps");
    {
        std::lock_guard<std::mutex> lk(mtx_);
        stepRequests_ += n, stepsDone_ = 0;
        stepsCancelled_ = std::move(cancelled);
        afterSteps_ = std::move(after);
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
        bool doStep = false, finishSteps = false;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait_for(lk, std::chrono::milliseconds(50),
                         [&] { return quit_ || !queue_.empty() || running_ || stepRequests_ > 0; });
            if (quit_) break;
            cmds.swap(queue_);
            if (running_ && endTime_ > 0 && double(sim_->time()) >= endTime_ - 1e-6) running_ = false, rangeEnded_ = true; // the play range's end
            if (running_) doStep = true;
            else if (stepRequests_ > 0 && stepsCancelled_ && stepsCancelled_()) stepRequests_ = 0, finishSteps = true; // the rest dropped
            else if (stepRequests_ > 0) { --stepRequests_, ++stepsDone_, doStep = true, finishSteps = stepRequests_ == 0; }
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
        if (!doStep && cmds.empty() && !finishSteps) continue;
        const auto w0 = clock::now();
        if (doStep) awaitControllers(); // the last frame answered (or given up on) before the next is stepped

        auto t0 = clock::now();
        if (doStep) sim_->stepFrame();
        const auto t1 = clock::now();
        publish(doStep);
        if (doStep) handOver();
        if (doStep) {
            const auto ms = [](clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
            std::lock_guard<std::mutex> lk(snapMtx_);
            costs_.push_back({ms(w0 - c0), ms(t0 - w0), ms(t1 - t0), ms(clock::now() - t1), int(cmds.size()), slowest, slowLabel});
            if (costs_.size() > size_t(kTimingFrames)) costs_.pop_front();
            frameDtMs_ = 1000.0 * sim_->frameDt;
        }

        if (finishSteps) { // the frames stepFrames asked for are stepped (or the rest dropped): its answer, after their snapshot
            std::function<void(ke::Simulation&, int)> after;
            int done = 0;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                after.swap(afterSteps_), done = stepsDone_, stepsCancelled_ = nullptr;
            }
            if (after) after(*sim_, done);
        }
        // Optional real-time cap: do not run a scene faster than the wall clock (frames asked for on pause: at once).
        if (doStep && realtime_ && running_) {
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
        t.commandsMs += c.commands, t.controllersMs += c.controllers, t.stepMs += c.step, t.publishMs += c.publish;
        t.stepMaxMs = std::max(t.stepMaxMs, c.step);
        t.commandsPerFrame += c.count;
        if (c.slowest > t.slowestCommandMs) t.slowestCommandMs = c.slowest, t.slowestCommand = c.label;
    }
    if (t.frames) t.commandsPerFrame /= t.frames;
    if (t.frames) t.commandsMs /= t.frames, t.controllersMs /= t.frames, t.stepMs /= t.frames, t.publishMs /= t.frames;
    return t;
}
