// The Probe: channels (values, counters, timers) and debug drawing, against what was put in.
#include "TestRunner.h"

#include "core/Probe.h"

#include <chrono>
#include <thread>

void testProbe() {
    Probe::clear();
    Probe::enableDraw(false);

    // Values keep the latest number, counters sum within a frame, timers add up milliseconds.
    Probe::set("test/value", 1.0);
    Probe::set("test/value", 2.5);
    Probe::add("test/counter", 3);
    Probe::add("test/counter", 4);
    {
        Probe::Timer t("test/timer ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    {
        Probe::Timer t("test/timer ms"); // a second scope adds to the same channel
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Probe::Snapshot s = Probe::snapshot();
    CHECK(s.value("test/value") == 2.5, "set keeps the latest value: %g", s.value("test/value"));
    CHECK(s.value("test/counter") == 7.0, "add sums within a frame: %g", s.value("test/counter"));
    CHECK(s.value("test/timer ms") >= 3.0, "timers add up: %g ms", s.value("test/timer ms"));
    CHECK(s.value("test/missing", -1.0) == -1.0, "a missing channel returns the fallback");
    // Sorted by name, and the kinds are remembered.
    CHECK(s.channels.size() == 3, "3 channels, got %zu", s.channels.size());
    for (size_t i = 1; i < s.channels.size(); ++i)
        CHECK(s.channels[i - 1].name < s.channels[i].name, "channels are sorted by name");
    for (const Probe::Channel& c : s.channels) {
        if (c.name == "test/value") CHECK(c.kind == Probe::Kind::Value, "kind of a value");
        if (c.name == "test/counter") CHECK(c.kind == Probe::Kind::Counter, "kind of a counter");
        if (c.name == "test/timer ms") CHECK(c.kind == Probe::Kind::TimerMs, "kind of a timer");
    }
    std::vector<std::string> names = Probe::channels();
    CHECK(names.size() == 3 && names[0] == "test/counter" && names[1] == "test/timer ms" && names[2] == "test/value",
          "channels() lists the sorted names");

    // A new frame: counters and timers back to zero, values stay.
    Probe::beginFrame();
    s = Probe::snapshot();
    CHECK(s.value("test/value") == 2.5, "beginFrame keeps values: %g", s.value("test/value"));
    CHECK(s.value("test/counter") == 0.0, "beginFrame zeroes counters: %g", s.value("test/counter"));
    CHECK(s.value("test/timer ms") == 0.0, "beginFrame zeroes timers: %g", s.value("test/timer ms"));
    CHECK(s.channels.size() == 3, "the channels survive the frame: %zu", s.channels.size());

    // Debug drawing: nothing while disabled, everything while enabled, cleared by the frame.
    Probe::line({0, 0, 0}, {1, 0, 0}, {1, 1, 1});
    Probe::point({0, 0, 0}, {1, 1, 1});
    s = Probe::snapshot();
    CHECK(s.lines.empty() && s.points.empty(), "drawing is off by default: %zu lines, %zu points", s.lines.size(), s.points.size());
    Probe::enableDraw(true);
    CHECK(Probe::drawEnabled(), "enableDraw(true)");
    Probe::line({0, 0, 0}, {1, 0, 0}, {1, 0, 0});
    Probe::arrow({0, 0, 0}, {0, 1, 0}, {0, 1, 0});
    Probe::box(AABB({0, 0, 0}, {1, 1, 1}), {0, 0, 1});
    Probe::point({0.5_r, 0.5_r, 0.5_r}, {1, 1, 0}, 0.02_r);
    s = Probe::snapshot();
    CHECK(s.lines.size() == 1 + 3 + 12, "a line, an arrow (3 segments) and a box (12 edges): %zu segments", s.lines.size());
    CHECK(s.points.size() == 1 && s.points[0].size == 0.02_r, "one point of size 0.02");
    CHECK(length(s.lines[0].b - Vector3(1, 0, 0)) < 1e-6_r && s.lines[0].color.x == 1.0_r, "the line as given");
    // The arrow's shaft ends at the tip; its two head strokes start there.
    CHECK(length(s.lines[1].b - Vector3(0, 1, 0)) < 1e-6_r, "arrow shaft ends at the tip");
    CHECK(length(s.lines[2].a - Vector3(0, 1, 0)) < 1e-6_r && length(s.lines[3].a - Vector3(0, 1, 0)) < 1e-6_r, "arrow head starts at the tip");
    // The box's 12 edges all have unit length.
    for (size_t i = 4; i < 16; ++i) CHECK(std::fabs(length(s.lines[i].b - s.lines[i].a) - 1.0_r) < 1e-6_r, "box edge %zu has unit length", i);
    Probe::beginFrame();
    s = Probe::snapshot();
    CHECK(s.lines.empty() && s.points.empty(), "beginFrame clears the drawing: %zu lines, %zu points", s.lines.size(), s.points.size());
    Probe::enableDraw(false);
    CHECK(!Probe::drawEnabled(), "enableDraw(false)");
    Probe::clear();
    CHECK(Probe::channels().empty(), "clear() removes every channel");
}
