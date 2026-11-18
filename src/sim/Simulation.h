#pragma once
// The running scene: a physics world (src/physics) with a robot world (RobotWorld.h) built in it from a
// scene graph, stepped a frame of frameDt at a time, and the frame a viewer draws; after each frame the
// channels asked for (Channels.h) are measured. Not thread-safe: one
// thread owns it (the editor's SimController runs it on its own and posts commands between frames).
#include "physics/PhysicsWorld.h"
#include "sim/Channels.h"
#include "sim/Frame.h"
#include "sim/RobotWorld.h"

#include <memory>

namespace ke {

class Simulation {
public:
    Real frameDt = 1.0_r / 60;
    ChannelRequests channels; // what to measure after every frame (empty: nothing)

    // A new world from the graph: the old one is gone, the clock back at zero.
    void load(const SceneGraph& graph);
    bool loaded() const { return physics_ != nullptr; }
    void stepFrame();
    void fill(Frame& out) const;

    Real time() const { return time_; }
    uint64_t frame() const { return frame_; }
    const std::vector<Measurement>& measurements() const { return measurements_; } // of the last frame
    PhysicsWorld* physics() { return physics_.get(); }
    const PhysicsWorld* physics() const { return physics_.get(); }
    RobotWorld* robot() { return robot_.get(); }
    const RobotWorld* robot() const { return robot_.get(); }

private:
    std::unique_ptr<PhysicsWorld> physics_;
    std::unique_ptr<RobotWorld> robot_;
    Real time_ = 0, stepMs_ = 0;
    uint64_t frame_ = 0;
    std::vector<Measurement> measurements_;
};

} // namespace ke
