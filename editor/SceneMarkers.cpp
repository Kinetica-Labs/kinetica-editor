// The markers of a graph's lights, cameras and forces, their wireframes, and picking them (see SceneMarkers.h).
#include "SceneMarkers.h"

#include <algorithm>
#include <cmath>

using namespace ke;

namespace {

const float kDegToRad = 3.14159265f / 180.0f;
const float kCameraDepth = 0.35f; // how far in front of a camera's eye its frame is drawn, m

void segment(std::vector<float>& out, const Vector3& a, const Vector3& b) { out.insert(out.end(), {a.x, a.y, a.z, 0, b.x, b.y, b.z, 0}); }

// A circle around `centre` in the plane of the unit vectors u and v.
void circle(std::vector<float>& out, const Vector3& centre, const Vector3& u, const Vector3& v, float radius, int segments = 32) {
    for (int k = 0; k < segments; ++k) {
        const float a0 = 6.2831853f * float(k) / float(segments), a1 = 6.2831853f * float(k + 1) / float(segments);
        segment(out, centre + (u * std::cos(a0) + v * std::sin(a0)) * radius, centre + (u * std::cos(a1) + v * std::sin(a1)) * radius);
    }
}

void sunLines(const SceneMarker& m, std::vector<float>& solid) {
    const Vector3 d = m.rotation.rotate(Vector3(0, -1, 0)), u = m.rotation.rotate(Vector3(1, 0, 0)), v = m.rotation.rotate(Vector3(0, 0, 1));
    circle(solid, m.position, u, v, 0.15f);
    for (int k = 0; k < 8; ++k) { // short rays around the disc
        const float a = 6.2831853f * float(k) / 8.0f;
        const Vector3 r = u * std::cos(a) + v * std::sin(a);
        segment(solid, m.position + r * 0.19f, m.position + r * 0.26f);
    }
    const Vector3 tip = m.position + d * (m.reach > 0 ? m.reach : 0.7f);
    segment(solid, m.position, tip);
    segment(solid, tip, tip - d * 0.12f + u * 0.06f);
    segment(solid, tip, tip - d * 0.12f - u * 0.06f);
}

void pointLines(const SceneMarker& m, std::vector<float>& solid, std::vector<float>& faint) {
    const Vector3 x(1, 0, 0), y(0, 1, 0), z(0, 0, 1);
    circle(solid, m.position, x, z, 0.1f, 24);
    circle(solid, m.position, x, y, 0.1f, 24);
    circle(solid, m.position, y, z, 0.1f, 24);
    segment(solid, m.position - y * 0.1f, m.position - y * 0.2f); // the base of the bulb
    if (m.selected) circle(faint, m.position, x, z, m.range, 64);
}

void spotLines(const SceneMarker& m, std::vector<float>& solid, std::vector<float>& faint) {
    const Vector3 d = m.rotation.rotate(Vector3(0, -1, 0)), u = m.rotation.rotate(Vector3(1, 0, 0)), v = m.rotation.rotate(Vector3(0, 0, 1));
    circle(solid, m.position, u, v, 0.05f, 16);
    if (!m.selected) { // where it stands and where it shines; its cone only when selected - unselected it was the first frame's
        const Vector3 tip = m.position + d * 0.25f;   // biggest noise (a newcomer: «что за большой жёлтый каркас-конус?»)
        segment(solid, m.position, tip);
        segment(solid, tip, tip - d * 0.06f + u * 0.04f);
        segment(solid, tip, tip - d * 0.06f - u * 0.04f);
        return;
    }
    const float length = m.reach > 0 ? m.reach : std::clamp(m.range, 0.3f, 1.0f);
    const float outer = 0.5f * m.coneDeg * kDegToRad, inner = std::max(0.0f, 0.5f * m.coneDeg - m.softnessDeg) * kDegToRad;
    const Vector3 end = m.position + d * length;
    const float r = length * std::tan(std::min(outer, 1.45f));
    circle(solid, end, u, v, r);
    for (int k = 0; k < 4; ++k) {
        const float a = 1.5707963f * float(k);
        segment(solid, m.position, end + (u * std::cos(a) + v * std::sin(a)) * r);
    }
    circle(faint, end, u, v, length * std::tan(std::min(inner, 1.45f)));
}

void cameraLines(const SceneMarker& m, float aspect, std::vector<float>& solid) {
    const Vector3 f = m.rotation.rotate(Vector3(0, 0, -1)), up = m.rotation.rotate(Vector3(0, 1, 0)), right = m.rotation.rotate(Vector3(1, 0, 0));
    const float depth = m.reach > 0 ? m.reach : kCameraDepth;
    const float h = depth * std::tan(0.5f * std::clamp(m.fovDeg, 1.0f, 170.0f) * kDegToRad), w = h * aspect;
    const Vector3 c = m.position + f * depth;
    const Vector3 corners[4] = {c + right * w + up * h, c - right * w + up * h, c - right * w - up * h, c + right * w - up * h};
    for (int k = 0; k < 4; ++k) {
        segment(solid, m.position, corners[k]);
        segment(solid, corners[k], corners[(k + 1) % 4]);
    }
    const Vector3 top = c + up * (h * 1.1f);
    segment(solid, top - right * (w * 0.6f), top + right * (w * 0.6f));
    segment(solid, top - right * (w * 0.6f), top + up * (h * 0.6f));
    segment(solid, top + right * (w * 0.6f), top + up * (h * 0.6f));
}

// An arrow along +y of the marker's frame, its length signed (against the axis when negative), and where it reaches.
void pushLines(const SceneMarker& m, std::vector<float>& solid) {
    const Vector3 x = m.rotation.rotate(Vector3(1, 0, 0)), y = forceAxis(m.rotation), z = m.rotation.rotate(Vector3(0, 0, 1));
    const float length = m.reach != 0 ? m.reach : 1.0f, sign = length < 0 ? -1.0f : 1.0f;
    const Vector3 tip = m.position + y * length;
    segment(solid, m.position, tip);
    segment(solid, tip, tip - y * (0.12f * sign) + x * 0.06f);
    segment(solid, tip, tip - y * (0.12f * sign) - x * 0.06f);
    if (m.range > 0) circle(solid, m.position, x, z, m.range, 48);
}

// Three rings of the force's radius and an arrow on each half axis, just inside the sphere: out of it, or into it.
void burstLines(const SceneMarker& m, std::vector<float>& solid) {
    const Vector3 axes[3] = {m.rotation.rotate(Vector3(1, 0, 0)), m.rotation.rotate(Vector3(0, 1, 0)), m.rotation.rotate(Vector3(0, 0, 1))};
    const float r = m.range, len = std::min(0.25f, r), sign = m.reach < 0 ? -1.0f : 1.0f;
    for (int k = 0; k < 3; ++k) {
        circle(solid, m.position, axes[k], axes[(k + 1) % 3], r, 48);
        for (const float side : {-1.0f, 1.0f}) {
            const Vector3 a = axes[k] * side, side2 = axes[(k + 1) % 3];
            const Vector3 in = m.position + a * (r - len), out = m.position + a * r, tip = sign > 0 ? out : in;
            segment(solid, in, out);
            segment(solid, tip, tip - a * (0.06f * sign) + side2 * 0.04f);
            segment(solid, tip, tip - a * (0.06f * sign) - side2 * 0.04f);
        }
    }
}

} // namespace

void markerLines(const SceneMarker& m, float aspect, std::vector<float>& solid, std::vector<float>& faint) {
    switch (m.kind) {
    case SceneMarker::Sun: sunLines(m, solid); break;
    case SceneMarker::Point: pointLines(m, solid, faint); break;
    case SceneMarker::Spot: spotLines(m, solid, faint); break;
    case SceneMarker::Push: pushLines(m, solid); break;
    case SceneMarker::Burst: burstLines(m, solid); break;
    case SceneMarker::Camera:
        cameraLines(m, aspect, solid);
        if (m.aimed) segment(faint, m.position, m.aim); // the line of sight to its target (the target is an object: drawn itself)
        break;
    }
}

bool markerHit(const SceneMarker& m, const Ray& ray, float& t) {
    Vector3 centre = m.position;
    float radius = m.kind == SceneMarker::Sun ? 0.25f : m.kind == SceneMarker::Push || m.kind == SceneMarker::Burst ? 0.2f : 0.16f;
    if (m.kind == SceneMarker::Camera) centre = m.position + m.rotation.rotate(Vector3(0, 0, -1)) * (0.5f * kCameraDepth), radius = 0.24f;
    const Vector3 oc = ray.origin - centre;
    const float b = dot(oc, ray.dir), c = dot(oc, oc) - radius * radius, disc = b * b - c;
    if (disc < 0 || c <= 0) return false; // missed, or the eye is inside the ball (a camera made at the eye)
    t = -b - std::sqrt(disc);
    return t >= 0;
}

SceneMarker forceMarker(const Force& f) {
    SceneMarker m;
    m.kind = f.shape == ForceShape::Radial ? SceneMarker::Burst : SceneMarker::Push;
    m.range = float(f.radius);
    m.reach = f.strength < 0 ? -1.0f : 1.0f;
    return m;
}

std::vector<SceneMarker> markersOf(const SceneGraph& g) {
    std::vector<SceneMarker> out;
    auto add = [&](const SceneObject& o, SceneMarker m) {
        if (!effectivelyVisible(g, o)) return;
        m.id = o.id;
        m.color = o.color;
        worldPose(g, o, m.position, m.rotation);
        out.push_back(m);
    };
    for (const Light& l : g.lights) {
        SceneMarker m;
        m.kind = SceneMarker::Kind(int(l.kind));
        m.range = float(l.range), m.coneDeg = float(l.coneDeg), m.softnessDeg = float(l.softnessDeg);
        add(l, m);
    }
    for (const Camera& c : g.cameras) {
        SceneMarker m;
        m.kind = SceneMarker::Camera;
        m.fovDeg = float(c.fovDeg);
        add(c, m);
        if (c.kind == CameraKind::Target && !out.empty() && out.back().id == c.id)
            out.back().aimed = true, out.back().aim = cameraTarget(g, c), out.back().rotation = lookTurn(out.back().position, out.back().aim);
    }
    for (const Force& f : g.forces) add(f, forceMarker(f));
    return out;
}
