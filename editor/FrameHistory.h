#pragma once
// The last frames of a run kept in memory - what the Sequencer's playhead shows again on pause, and a plot's click goes
// to, the way PhysX PVD replays a recording. It has no strip of its own: the Sequencer's ruler is the one timeline of
// the window (SequencerPanel: dragged while the scene runs or stands, it asks for a moment here; «● Вживую» comes back).
//
// What is kept is the frame exactly as the view drew it (the immutable ke::Frame the simulation thread published), so
// going back shows that frame bit for bit; nothing is simulated again. The ring holds at most `capacity` frames (600 =
// 10 s at 60 frames a second) and at most kBudgetBytes of memory: a heavy scene keeps fewer seconds, never more memory.
// Going back pauses a running scene; going live lets it run on (MainWindow wires both: scrubStarted, wentLive).
#include "sim/Frame.h"

#include <QObject>

#include <deque>
#include <memory>

class FrameHistory : public QObject {
    Q_OBJECT
public:
    using Frame = std::shared_ptr<const ke::Frame>;
    static constexpr size_t kBudgetBytes = size_t(512) << 20; // 512 MB at most, whatever the capacity

    explicit FrameHistory(QObject* parent = nullptr) : QObject(parent) {}

    // A frame the simulation published: kept unless it is the same frame again; a frame number that went back (a reset,
    // a new play) starts the history over.
    void record(const Frame& s);
    void clear();
    int capacity() const { return capacity_; }
    int count() const { return int(frames_.size()); }
    size_t bytes() const { return bytes_; }
    bool showingPast() const { return shown_ >= 0; }
    int position() const { return shown_ >= 0 ? shown_ : count() - 1; } // 0 = the oldest kept frame
    Frame frameAt(int index) const;
    Frame newest() const { return frames_.empty() ? nullptr : frames_.back(); }
    int nearest(double time) const; // the kept frame nearest that time (-1: none)

    void seek(int index); // show kept frame `index`: the scene pauses (scrubStarted) the first time
    void goLive();        // the newest frame again («● Вживую»)

    // How much memory a frame holds, roughly: its big arrays. Shared meshes are not counted - every frame points at the
    // same ones.
    static size_t approxBytes(const ke::Frame& s);

signals:
    void showFrame(Frame s); // the view should draw this kept frame
    void scrubStarted();     // the past is shown: the scene should pause
    void wentLive();         // back to the newest frame: the scene may run on

private:
    void dropOldest();

    std::deque<Frame> frames_;
    std::deque<size_t> sizes_;
    size_t bytes_ = 0;
    int capacity_ = 600;
    int shown_ = -1; // the kept frame shown (-1: live)
};
