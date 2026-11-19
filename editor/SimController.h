#pragma once
// Runs the Simulation (sim/Simulation.h) on a background thread. The GUI never touches the Simulation directly:
// it posts commands (executed between frames on the simulation thread) and reads immutable
// Frames published after every frame. The newest one is what the view draws; every one of
// them also waits in a short queue (takePublished) so a recording misses no frame even when the
// window paints slower than the simulation steps. And every stepped frame leaves its numbers
// - without its picture - in a longer queue (takeReadings): a plot records every frame the
// simulation made, however slowly the window paints.

#include "sim/Simulation.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class SimController {
public:
    using Command = std::function<void(ke::Simulation&)>;

    SimController();
    ~SimController();

    // label: what the command is, for timing()'s slowest command (a literal; empty: unnamed)
    void post(Command cmd, const char* label = "");
    // The scene's name every published frame carries (Frame::sceneName): the window names its file.
    void setSceneName(std::string name);
    void setRunning(bool on);
    void requestStep();
    void setRealtimeLimit(bool on) { realtime_ = on; }

    std::shared_ptr<const ke::Frame> snapshot() const;
    // Every snapshot published since the last call, oldest first (at most kPublishedKept: a window that
    // does not ask for a long while loses the oldest, never grows without bound).
    std::vector<std::shared_ptr<const ke::Frame>> takePublished();
    static constexpr size_t kPublishedKept = 240;

    // One stepped frame's numbers: what the plots record.
    struct Reading {
        uint64_t frame = 0;
        double time = 0;
        std::string scene;
        std::vector<ke::Probe::Channel> channels;     // the Probe's channels
        std::vector<ke::Measurement> measurements;    // the channels asked for (ke::Simulation::channels)
    };
    // Every stepped frame's numbers since the last call, oldest first (at most kReadingsKept: about
    // 5 minutes at 60 frames a second if nobody asks, never more).
    std::vector<Reading> takeReadings();
    static constexpr size_t kReadingsKept = 20000;

    // Where a frame's wall time goes, over the last kTimingFrames stepped frames: the posted commands (API calls,
    // the window's queries), the step itself, the snapshot's publication. A scene runs slower than the wall clock
    // exactly when their sum passes the frame's dt; this says which part does it.
    struct Timing {
        int frames = 0;
        double commandsMs = 0, stepMs = 0, publishMs = 0, stepMaxMs = 0, frameDtMs = 0;
        double commandsPerFrame = 0, slowestCommandMs = 0; // over the same frames
        std::string slowestCommand;                       // its label
    };
    Timing timing() const;
    static constexpr int kTimingFrames = 120;

private:
    void loop();
    void publish(bool stepped);

    std::unique_ptr<ke::Simulation> sim_;
    std::thread thread_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<std::pair<Command, const char*>> queue_;
    int stepRequests_ = 0;
    std::atomic<bool> running_{false}, quit_{false}, realtime_{true};

    mutable std::mutex snapMtx_;
    std::shared_ptr<const ke::Frame> snap_;
    std::string sceneName_;                                  // under snapMtx_
    std::deque<std::shared_ptr<const ke::Frame>> published_; // not yet taken (under snapMtx_)
    std::deque<Reading> readings_;                                      // not yet taken (under snapMtx_)
    std::atomic<uint64_t> serial_{0};
    struct FrameCost { double commands, step, publish; int count; double slowest; const char* label; };
    std::deque<FrameCost> costs_; // the last kTimingFrames stepped frames (under snapMtx_)
    double frameDtMs_ = 0;        // under snapMtx_
};
