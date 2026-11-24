// The sensors' glyphs in the view (SensorGlyphs.h): rings, chips and beams in the sensor's own frame,
// turned and placed into the world.
#include "SensorGlyphs.h"

#include <algorithm>
#include <cmath>

using namespace ke;

namespace {

const float kPiF = 3.14159265f;

void segment(std::vector<float>& out, const Vector3& a, const Vector3& b) { out.insert(out.end(), {a.x, a.y, a.z, 0, b.x, b.y, b.z, 0}); }

// A circle of radius r about c in the plane of u and w.
void circle(std::vector<float>& out, const Vector3& c, const Vector3& u, const Vector3& w, float r, int n = 32) {
    for (int k = 0; k < n; ++k) {
        const float a0 = 2 * kPiF * float(k) / float(n), a1 = 2 * kPiF * float(k + 1) / float(n);
        segment(out, c + u * (r * std::cos(a0)) + w * (r * std::sin(a0)), c + u * (r * std::cos(a1)) + w * (r * std::sin(a1)));
    }
}

void encoderLines(SensorGlyphLines& out, const Vector3& o, const Vector3& y, const Vector3& z, float s, float angle) {
    const float r = 1.2f * s;
    circle(out.encoder, o, y, z, r);
    for (int k = 0; k < 8; ++k) {
        const float a = 2 * kPiF * float(k) / 8;
        const Vector3 d = y * std::cos(a) + z * std::sin(a);
        segment(out.encoder, o + d * r, o + d * (1.3f * r));
    }
    segment(out.encoder, o, o + (y * std::cos(angle) + z * std::sin(angle)) * r);
}

void imuLines(SensorGlyphLines& out, const Vector3& o, const Vector3 axes[3], float s) {
    const float h = 0.7f * s;
    const Vector3 c[4] = {o + (axes[0] + axes[1]) * h, o + (axes[1] - axes[0]) * h, o - (axes[0] + axes[1]) * h, o + (axes[0] - axes[1]) * h};
    for (int k = 0; k < 4; ++k) segment(out.chip, c[k], c[(k + 1) % 4]);
    for (int k = 0; k < 3; ++k) segment(out.axis[k], o, o + axes[k] * (2.5f * s));
}

void sonarLines(SensorGlyphLines& out, const Vector3& o, const Vector3& x, const Vector3& y, const Vector3& z, float s, float echo) {
    circle(out.chip, o + y * (0.6f * s), y, z, 0.35f * s, 16);
    circle(out.chip, o - y * (0.6f * s), y, z, 0.35f * s, 16);
    const bool heard = echo > 0;
    const float length = heard ? echo : 4.0f, r = length * std::tan(7.5f * kPiF / 180);
    std::vector<float>& beam = heard ? out.beam : out.faint;
    const Vector3 end = o + x * length;
    circle(beam, end, y, z, r, 24);
    for (int k = 0; k < 8; ++k) {
        const float a = 2 * kPiF * float(k) / 8;
        segment(beam, o, end + (y * std::cos(a) + z * std::sin(a)) * r);
    }
}

// A lidar: its puck as a ring about z, and its rays - faint and a metre long before it scans (its pattern, not its
// range: a ring of 12 m rays drawn through the walls is a grey fog); scanning, bright to what each hit and faint on to
// its full range (faint all the way: a ray that hit nothing). At most 256 of them are drawn, spread over the pattern.
void lidarLines(SensorGlyphLines& out, const Vector3& o, const Vector3 axes[3], float s, const SensorVis& v) {
    circle(out.chip, o, axes[0], axes[1], 0.8f * s, 16);
    const std::vector<Vector3> rays = Lidar::directions(v.lidar);
    const bool scanned = v.ranges.size() == rays.size();
    const size_t stride = std::max<size_t>(rays.size() / 256, 1);
    const float far = float(v.lidar.maxM);
    for (size_t k = 0; k < rays.size(); k += stride) {
        const Vector3 dir = axes[0] * rays[k].x + axes[1] * rays[k].y + axes[2] * rays[k].z;
        if (!scanned) {
            segment(out.faint, o, o + dir * std::min(far, 1.0f));
            continue;
        }
        const float d = float(v.ranges[k]);
        if (d > 0) segment(out.beam, o, o + dir * d);
        segment(out.faint, o + dir * std::max(d, 0.0f), o + dir * far);
    }
}

// A touch pad: its square on the face, and the force pressing it as an arrow into the face, 1 cm a newton.
void touchLines(SensorGlyphLines& out, const Vector3& o, const Vector3 axes[3], float s, float force) {
    const float h = 0.8f * s;
    const Vector3 c[4] = {o + (axes[1] + axes[2]) * h, o + (axes[2] - axes[1]) * h, o - (axes[1] + axes[2]) * h, o + (axes[1] - axes[2]) * h};
    for (int k = 0; k < 4; ++k) segment(out.chip, c[k], c[(k + 1) % 4]);
    if (force > 0) segment(out.beam, o + axes[0] * std::min(0.01f * force, 0.3f), o);
}

// A stereo camera: its mount as a bar between the two lenses (y toward the left one), the basis's axes at its
// centre, and each lens's pyramid of view out along x - the left one (the depth's eye) bright, the right faint.
void stereoLines(SensorGlyphLines& out, const Vector3& o, const Vector3 axes[3], float s, const SensorVis& v) {
    const float b = float(v.baseline), half = std::tan(float(v.fovDeg) * kPiF / 360), depth = std::max(2.5f * b, 0.15f);
    const float w = depth * half, h = w * float(v.aspect), mw = b / 2 + 0.6f * s, mh = 0.5f * s;
    const Vector3 c[4] = {o + axes[1] * mw + axes[2] * mh, o - axes[1] * mw + axes[2] * mh, o - axes[1] * mw - axes[2] * mh, o + axes[1] * mw - axes[2] * mh};
    for (int k = 0; k < 4; ++k) segment(out.chip, c[k], c[(k + 1) % 4]);
    for (int k = 0; k < 3; ++k) segment(out.axis[k], o, o + axes[k] * (2.5f * s));
    for (const float side : {1.0f, -1.0f}) {
        const Vector3 lens = o + axes[1] * (side * b / 2), end = lens + axes[0] * depth;
        circle(out.chip, lens, axes[1], axes[2], 0.4f * s, 16);
        std::vector<float>& line = side > 0 ? out.beam : out.faint;
        const Vector3 f[4] = {end + axes[1] * w + axes[2] * h, end - axes[1] * w + axes[2] * h, end - axes[1] * w - axes[2] * h, end + axes[1] * w - axes[2] * h};
        for (int k = 0; k < 4; ++k) segment(line, f[k], f[(k + 1) % 4]), segment(line, lens, f[k]);
    }
}

} // namespace

void appendSensorGlyph(SensorGlyphLines& out, const SensorVis& v) {
    const Quaternion q = v.rotation;
    const Vector3 axes[3] = {q.rotate(Vector3(1, 0, 0)), q.rotate(Vector3(0, 1, 0)), q.rotate(Vector3(0, 0, 1))};
    const float s = float(v.size), reading = float(v.reading);
    if (v.kind == 1 || v.kind == 4) imuLines(out, v.position, axes, s); // an IMU, a force-torque sensor: its three axes
    else if (v.kind == 2) sonarLines(out, v.position, axes[0], axes[1], axes[2], s, reading);
    else if (v.kind == 3) touchLines(out, v.position, axes, s, reading);
    else if (v.kind == 6) stereoLines(out, v.position, axes, s, v);
    else if (v.kind == 7) lidarLines(out, v.position, axes, s, v);
    else encoderLines(out, v.position, axes[1], axes[2], s, v.kind == 5 ? 0.0f : reading); // an effort: the ring of its freedom
}

bool sensorHit(const SensorVis& v, const Ray& ray, float& t) {
    const float radius = 2.0f * float(v.size);
    const Vector3 m = ray.origin - v.position;
    const float b = dot(m, ray.dir), c = dot(m, m) - radius * radius;
    if (c <= 0) return false; // inside the ball: a click from within must not catch it
    const float disc = b * b - c;
    if (b > 0 || disc < 0) return false;
    t = -b - std::sqrt(disc);
    return true;
}
