// The last frames of a run (see FrameHistory.h): the ring, its memory, and going back and live.
#include "FrameHistory.h"

#include <algorithm>
#include <cmath>

// 1. The same frame again (a paused scene publishes it after every command): nothing to keep. 2. A frame number that
// went back: a new run, the old history goes. 3. Keep it, drop the oldest frames while over the capacity or the budget.
void FrameHistory::record(const Frame& s) {
    if (!s) return;
    if (!frames_.empty() && s->frame == frames_.back()->frame && s->sceneName == frames_.back()->sceneName) return;
    if (!frames_.empty() && (s->frame < frames_.back()->frame || s->sceneName != frames_.back()->sceneName)) clear();
    const size_t size = approxBytes(*s);
    frames_.push_back(s);
    sizes_.push_back(size);
    bytes_ += size;
    while (frames_.size() > 1 && (int(frames_.size()) > capacity_ || bytes_ > kBudgetBytes)) dropOldest();
}

void FrameHistory::dropOldest() {
    bytes_ -= sizes_.front();
    frames_.pop_front();
    sizes_.pop_front();
    if (shown_ > 0) --shown_; // the frame on screen keeps its place in the ring
}

void FrameHistory::clear() {
    frames_.clear();
    sizes_.clear();
    bytes_ = 0;
    shown_ = -1;
}

FrameHistory::Frame FrameHistory::frameAt(int index) const { return index >= 0 && index < count() ? frames_[size_t(index)] : nullptr; }

int FrameHistory::nearest(double time) const {
    if (frames_.empty()) return -1;
    const auto after = std::lower_bound(frames_.begin(), frames_.end(), time, [](const Frame& f, double t) { return double(f->time) < t; });
    if (after == frames_.end()) return count() - 1;
    if (after == frames_.begin()) return 0;
    const auto before = after - 1;
    return int((std::fabs(double((*after)->time) - time) < std::fabs(double((*before)->time) - time) ? after : before) - frames_.begin());
}

void FrameHistory::seek(int index) {
    if (frames_.empty()) return;
    if (!showingPast()) emit scrubStarted();
    shown_ = std::clamp(index, 0, count() - 1);
    emit showFrame(frames_[size_t(shown_)]);
}

void FrameHistory::goLive() {
    const bool wasPast = showingPast();
    shown_ = -1;
    if (!frames_.empty()) emit showFrame(frames_.back());
    if (wasPast) emit wentLive();
}

size_t FrameHistory::approxBytes(const ke::Frame& s) {
    size_t n = sizeof(ke::Frame);
    n += s.bodies.size() * sizeof(ke::Frame::Body);
    n += s.joints.size() * sizeof(ke::JointVis) + s.sensors.size() * sizeof(ke::SensorVis) + s.goals.size() * sizeof(ke::GoalVis);
    n += s.contacts.size() * sizeof(ke::Frame::ContactInfo) + s.lights.size() * sizeof(ke::Frame::LightInfo);
    n += s.probe.lines.size() * sizeof(ke::Probe::Line) + s.probe.points.size() * sizeof(ke::Probe::Point);
    n += s.probe.labels.size() * sizeof(ke::Probe::Label) + s.probe.channels.size() * sizeof(ke::Probe::Channel);
    n += s.measurements.size() * sizeof(ke::Measurement);
    for (const auto& [name, value] : s.info) n += name.capacity() + value.capacity();
    return n;
}
