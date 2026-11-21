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
    bool frameContacts = true; // the frame lists the contact points (RobotWorld::fill): off while no one reads them

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
    // The take (RobotWorld::startRecording): the running one ended and handed over, or the one a new load (Stop)
    // ended - kept for this call. False: none.
    bool takeRecording(const std::string& name, SequenceObject& out);
    void startRecording() { if (robot_) robot_->startRecording(*physics_); }

private:
    std::unique_ptr<PhysicsWorld> physics_;
    std::unique_ptr<RobotWorld> robot_;
    Real time_ = 0, stepMs_ = 0;
    uint64_t frame_ = 0;
    std::vector<Measurement> measurements_;
    std::unique_ptr<SequenceObject> endedTake_;
};

} // namespace ke
