// The quadrature encoder (Encoder.h): the state from the angle, the Gray code of A and B, the index,
// and the edges between two samples of the angle with their interpolated times.
#include "devices/Encoder.h"

#include <algorithm>
#include <cmath>

namespace ke {

Encoder::Encoder(int lines, double stepsPerMetre) : lines_(lines) {
    perUnit_ = lines > 0 ? 4.0 * double(lines) / (2.0 * 3.14159265358979323846) : stepsPerMetre;
}

int64_t Encoder::stateAt(double angle) const { return int64_t(std::floor(angle * perUnit_)); }

// The Gray code s mod 4 -> (A, B): 0 -> 00, 1 -> 10, 2 -> 11, 3 -> 01: forward, A changes first.
bool Encoder::levelA(int64_t s) {
    const int64_t m = ((s % 4) + 4) % 4;
    return m == 1 || m == 2;
}
bool Encoder::levelB(int64_t s) {
    const int64_t m = ((s % 4) + 4) % 4;
    return m == 2 || m == 3;
}

// Z is high on the one quadrature step that starts a turn (a rotary encoder only).
bool Encoder::indexAt(int64_t s) const {
    if (lines_ <= 0) return false;
    const int64_t turn = 4 * int64_t(lines_);
    return ((s % turn) + turn) % turn == 0;
}

void Encoder::start(double angle) {
    state_ = start_ = stateAt(angle);
    index_ = indexAt(state_);
}

// Every step boundary k crossed between the two samples, in the order the shaft crosses them: going
// up, from state + 1 to the new state; going down, from the state to the new state + 1. Crossing
// boundary k changes the state to k (up) or k - 1 (down): one of A, B flips - and Z when the state
// enters or leaves the index step. Its time: t0 + (k / perUnit - angle0) / (angle1 - angle0) (t1 - t0).
void Encoder::advance(double angle0, double angle1, int64_t t0Ns, int64_t t1Ns, std::vector<PinEdge>& out) {
    const int64_t target = stateAt(angle1);
    const double span = angle1 - angle0;
    while (state_ != target) {
        const bool up = target > state_;
        const int64_t boundary = up ? state_ + 1 : state_, next = up ? state_ + 1 : state_ - 1;
        const double f = span != 0 ? std::clamp((double(boundary) / perUnit_ - angle0) / span, 0.0, 1.0) : 1.0;
        const int64_t t = t0Ns + int64_t(std::llround(f * double(t1Ns - t0Ns)));
        if (levelA(next) != levelA(state_)) out.push_back({t, 'A', levelA(next)});
        if (levelB(next) != levelB(state_)) out.push_back({t, 'B', levelB(next)});
        const bool z = indexAt(next);
        if (z != index_) out.push_back({t, 'Z', z});
        state_ = next;
        index_ = z;
    }
}

} // namespace ke
