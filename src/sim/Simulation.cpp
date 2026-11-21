// The running scene (Simulation.h): load builds the world of the scene's physics engine from the graph,
// stepFrame steps it with the robot's drives, reports to the probe and measures the channels asked for,
// fill gives the frame.
#include "sim/Simulation.h"

#include <algorithm>
#include <chrono>

namespace ke {

void Simulation::load(const SceneGraph& graph) {
    if (robot_ && robot_->recording()) endedTake_ = std::make_unique<SequenceObject>(robot_->takeRecording(""));
    robot_.reset();
    physics_ = makePhysicsWorld(graph.world.physics, graph.world.gravity);
    robot_ = std::make_unique<RobotWorld>(graph);
    robot_->build(*physics_);
    time_ = 0, frame_ = 0, stepMs_ = 0;
    measurements_.clear();
}

bool Simulation::takeRecording(const std::string& name, SequenceObject& out) {
    if (robot_ && robot_->recording()) out = robot_->takeRecording(name);
    else if (endedTake_) out = std::move(*endedTake_), out.name = name, endedTake_.reset();
    else return false;
    return true;
}

void Simulation::stepFrame() {
    if (!physics_) return;
    Probe::beginFrame();
    const auto start = std::chrono::steady_clock::now();
    {
        Probe::Timer timer("physics/step ms");
        robot_->step(*physics_, frameDt, robot_->graph().world.physics.substeps);
    }
    stepMs_ = Real(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    time_ += frameDt;
    ++frame_;
    Probe::set("physics/time s", double(time_));
    robot_->drawLayers(*physics_, frameDt / Real(robot_->engineSteps(robot_->graph().world.physics.substeps)));
    if (channels.empty()) measurements_.clear();
    else measurements_ = measureAll(*this, channels);
}

void Simulation::fill(Frame& out) const {
    out = Frame();
    out.frame = frame_, out.time = time_, out.stepMs = stepMs_;
    if (robot_) robot_->fill(*physics_, out, frameContacts);
    out.measurements = measurements_;
    out.probe = Probe::snapshot();
}

} // namespace ke
