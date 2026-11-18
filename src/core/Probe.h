#pragma once
// The probe: one place every part of the engine reports to, and the viewer, the plots, the tests
// and the CSV take from by name - as Bullet's btIDebugDraw and profiler, Box2D's b2Profile and
// b2Counters, PhysX's Visual Debugger. Global on purpose: a solver deep inside a substep, a scene
// and the application all reach it with one line, and the reader asks for any channel by name
// without anybody having wired it up in advance.
//
//   Probe::set("physics/contacts", n);            a value: the latest number of the channel
//   Probe::add("arm/plans", 1);                   a counter: summed over the frame, zero at beginFrame
//   { Probe::Timer t("vision/match"); ... }       a timer: milliseconds summed over the frame
//   if (Probe::layerOn(DrawLayer::ContactNormals))  debug drawing by LAYER (as PhysX's
//       Probe::arrow(DrawLayer::ContactNormals, p, n, colour);   visualization parameters)
//
// Rules: report per frame or per substep, not per pixel (the map is behind a mutex); the
// names are "part/quantity" in ASCII so the channels sort into groups. Test the layer BEFORE a
// loop that draws: with every layer off the drawing costs one atomic load per solver per step.
// Every layer is capped at kLayerCap primitives per frame (a label "(обрезано)" then says so).
// Simulation::stepFrame calls beginFrame() and copies snapshot() into the Frame.
#include "math/AABB.h"
#include "math/Vector3.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace ke {

// What the research view can show, one bit each (the editor's panel toggles them). The analogues:
// PhysX PxVisualizationParameter, Bullet btIDebugDraw::DebugDrawModes, Box2D b2Draw flags.
enum class DrawLayer : uint32_t {
    Misc,               // drawings without a layer (the old line/arrow/point/box calls)
    ContactPoints,      // contact points
    ContactNormals,     // their normals (0.1 m)
    ContactImpulses,    // normal force per point: impulse / dt, 1 m of arrow = 100 N (kForceScale)
    PenetrationDepth,   // a segment of the penetration depth along the normal, red
    BodyAabbs,          // every awake body's world box
    CentreOfMass,       // a small cross at each body's centre of mass
    InertiaAxes,        // principal axes, length proportional to sqrt(I / m)
    Velocities,         // linear (cyan) and angular (magenta) velocity arrows
    Sleeping,           // sleeping bodies' boxes, dimmed
    JointFrames,        // joint anchors, the link between them and the joint axis
    Count
};

class Probe {
public:
    enum class Kind { Value, Counter, TimerMs };
    struct Channel {
        std::string name;
        double value = 0;
        Kind kind = Kind::Value;
    };
    struct Line { Vector3 a, b, color; DrawLayer layer = DrawLayer::Misc; };
    struct Point { Vector3 p, color; Real size; DrawLayer layer = DrawLayer::Misc; };
    struct Label { Vector3 at; std::string text; DrawLayer layer = DrawLayer::Misc; };
    struct Snapshot {
        std::vector<Channel> channels; // sorted by name
        std::vector<Line> lines;
        std::vector<Point> points;
        std::vector<Label> labels;
        double value(const std::string& name, double fallback = 0) const;
    };

    static void set(const char* name, double value);
    static void add(const char* name, double delta);
    class Timer {
    public:
        explicit Timer(const char* name) : name_(name), t0_(std::chrono::steady_clock::now()) {}
        ~Timer();
        Timer(const Timer&) = delete;
        Timer& operator=(const Timer&) = delete;
    private:
        const char* name_;
        std::chrono::steady_clock::time_point t0_;
    };

    // --- Layers --------------------------------------------------------------------------------
    static constexpr uint32_t bit(DrawLayer l) { return 1u << uint32_t(l); }
    static void setLayers(uint32_t mask) { layers_.store(mask, std::memory_order_relaxed); }
    static uint32_t layers() { return layers_.load(std::memory_order_relaxed); }
    static bool layerOn(DrawLayer l) { return (layers() & bit(l)) != 0; }
    // The bits of the layers first..last (in the order of DrawLayer), e.g. every rigid layer.
    static constexpr uint32_t bits(DrawLayer first, DrawLayer last) {
        return (bit(last) - bit(first)) | bit(last); // first .. last-1, and last
    }
    // Removes this frame's drawing of the layers in mask. A solver stepped several times a frame
    // (substeps) calls it before it draws, so the picture is that of its last step.
    static const char* layerName(DrawLayer l); // "ContactPoints" ... (ASCII, for panels and files)
    static constexpr int kLayerCap = 20000;    // primitives per layer per frame
    static constexpr Real kForceScale = 0.01_r; // ContactImpulses: metres of arrow per newton

    // The old switch, kept for the viewer's "Отладочная отрисовка" box: on = the basic layers.
    static bool drawEnabled() { return layers() != 0; }
    static void enableDraw(bool on);

    // Drawing by layer (nothing is stored when the layer is off). Colours 0..1.
    static void line(DrawLayer l, const Vector3& a, const Vector3& b, const Vector3& color);
    static void arrow(DrawLayer l, const Vector3& p, const Vector3& v, const Vector3& color); // a line with a head
    static void point(DrawLayer l, const Vector3& p, const Vector3& color, Real size = 0.01_r);
    static void box(DrawLayer l, const AABB& b, const Vector3& color); // its 12 edges
    static void label(DrawLayer l, const Vector3& at, const std::string& text);
    // Without a layer: DrawLayer::Misc.
    static void line(const Vector3& a, const Vector3& b, const Vector3& color) { line(DrawLayer::Misc, a, b, color); }
    static void arrow(const Vector3& p, const Vector3& v, const Vector3& color) { arrow(DrawLayer::Misc, p, v, color); }
    static void point(const Vector3& p, const Vector3& color, Real size = 0.01_r) { point(DrawLayer::Misc, p, color, size); }
    static void box(const AABB& b, const Vector3& color) { box(DrawLayer::Misc, b, color); }

    // The frame: counters and timers back to zero, drawings cleared; values stay.
    static void beginFrame();
    // How many times beginFrame() and clear() ran: whoever draws once a frame from outside the
    // step remembers it and draws again only when it changed.
    static uint64_t frameIndex() { return frames_.load(std::memory_order_relaxed); }
    static Snapshot snapshot();
    static std::vector<std::string> channels();
    static void clear(); // everything, including the values (tests)

    // Memory: whoever replaces the global operator new (the tests, the app) adds to this; the
    // engine itself never touches the allocator. stepFrame reports the difference per frame.
    static std::atomic<long long> allocations;

private:
    static std::atomic<uint32_t> layers_;
    static std::atomic<uint64_t> frames_;
};


} // namespace ke
