#pragma once
// A quadrature encoder (incremental, with an index): what its two channels A and B and its index Z
// put out as the shaft turns - the edges a microcontroller's interrupts see. A disc of N lines gives
// 4N edges a turn: the quadrature state s = floor(theta 4N / 2 pi) steps through the Gray code
// (A, B) = 00, 10, 11, 01 (s mod 4), so turning forward A leads B by a quarter of a line and turning
// back B leads A - the direction a decoder reads from which channel changed first. The index Z is a
// pulse one quadrature step long once a turn, at theta = 0 (mod 2 pi).
//
// The shaft's angle is given at the ends of each physics step; within a step it is taken as moving
// at a constant rate, so an edge's time is where the angle crosses its line, linearly interpolated -
// exact for a constant speed. Times are whole nanoseconds from the start (a microcontroller's clock
// has whole ticks; a double sum of steps would drift). A linear encoder (on a slider) is the same with
// the travel in place of the angle and `perMetre` steps per metre.
#include "devices/PinEdge.h"
#include "math/Real.h"

#include <cstdint>
#include <vector>

namespace ke {

class Encoder {
public:
    // `lines` per turn (4 lines = 16 steps); for a linear encoder `stepsPerMetre` instead (lines 0).
    explicit Encoder(int lines, double stepsPerMetre = 0);

    // The shaft from `angle` at time t0 to `angleNext` at t1 (radians, unwrapped by the caller, or
    // metres): appends the edges crossed, in time order, to `out`.
    void advance(double angle0, double angle1, int64_t t0Ns, int64_t t1Ns, std::vector<PinEdge>& out);
    void start(double angle);           // the position at which counting starts (state from it)

    int64_t count() const { return state_ - start_; } // quadrature steps since start(): what a decoder counts
    bool a() const { return levelA(state_); }
    bool b() const { return levelB(state_); }
    bool z() const { return index_; }
    double stepsPerUnit() const { return perUnit_; }  // 4N / 2 pi, or the steps per metre

    static bool levelA(int64_t s);
    static bool levelB(int64_t s);

private:
    int64_t stateAt(double angle) const;
    bool indexAt(int64_t s) const;

    int lines_;
    double perUnit_;
    int64_t state_ = 0, start_ = 0;
    bool index_ = false;
};

} // namespace ke
