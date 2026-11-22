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

// An arrow from `from` to `tip` with a head that reads from any side: a ring at its base and four edges to the point.
void arrow(std::vector<float>& out, const Vector3& from, const Vector3& tip, float head) {
    const Vector3 along = tip - from;
    const float length = std::sqrt(dot(along, along));
    if (length < 1e-6f) return;
    const Vector3 d = along * (1.0f / length);
    const Vector3 side = std::fabs(d.y) < 0.9f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
    const Vector3 u = normalize(cross(d, side)), v = cross(d, u);
    const Vector3 base = tip - d * head;
    const float r = head * 0.38f;
    segment(out, from, base);
    circle(out, base, u, v, r, 12);
    for (const Vector3& e : {u, v, u * -1.0f, v * -1.0f}) segment(out, base + e * r, tip);
}

// The reach of a force, a sphere as three thin rings: drawn for the selected force only - around every force it would
// fill the scene with wire.
void reachRings(const SceneMarker& m, std::vector<float>& faint) {
    if (!m.selected || m.range <= 0) return;
    const Vector3 x(1, 0, 0), y(0, 1, 0), z(0, 0, 1);
    circle(faint, m.position, x, z, m.range, 72);
    circle(faint, m.position, x, y, m.range, 72);
    circle(faint, m.position, y, z, m.range, 72);
}

// A directional force: one arrow along the object's own axis (against it when the strength is negative), as long as
// the force is strong (forceArrow), standing on a small ring - the point it is applied at.
void pushLines(const SceneMarker& m, std::vector<float>& solid, std::vector<float>& faint) {
    const Vector3 x = m.rotation.rotate(Vector3(1, 0, 0)), y = forceAxis(m.rotation), z = m.rotation.rotate(Vector3(0, 0, 1));
    circle(solid, m.position, x, z, 0.07f, 16);
    arrow(solid, m.position, m.position + y * m.reach, std::min(0.22f, 0.45f * std::fabs(m.reach)));
    reachRings(m, faint);
}

// A radial force: six short arrows out of its point (into it when the strength is negative); selected, its sphere too,
// with the same arrows at the sphere - where the reach ends and which way the bodies there are pushed.
void burstLines(const SceneMarker& m, std::vector<float>& solid, std::vector<float>& faint) {
    const Vector3 axes[3] = {Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1)};
    const bool out = m.reach >= 0;
    const auto rays = [&](float from, float to, float head) {
        for (const Vector3& axis : axes)
            for (const float side : {-1.0f, 1.0f}) {
                const Vector3 a = m.position + axis * (side * from), b = m.position + axis * (side * to);
                arrow(solid, out ? a : b, out ? b : a, head);
            }
    };
    rays(0.07f, 0.34f, 0.12f);
    reachRings(m, faint);
    if (m.selected && m.range > 0.6f) rays(m.range - 0.26f, m.range, 0.12f);
}

// A torque: three quarters of a ring about its own axis, the arrowhead where it ends, the way it turns - a positive
// strength turns z toward x about +y, as the right hand does -, and its axis through the ring.
void twistLines(const SceneMarker& m, std::vector<float>& solid) {
    const Vector3 y = forceAxis(m.rotation), u = m.rotation.rotate(Vector3(0, 0, 1)), w = m.rotation.rotate(Vector3(1, 0, 0)) * (m.reach < 0 ? -1.0f : 1.0f);
    const float r = 0.18f, end = 4.712389f;
    const auto on = [&](float a) { return m.position + (u * std::cos(a) + w * std::sin(a)) * r; };
    for (int k = 0; k < 24; ++k) segment(solid, on(end * float(k) / 24.0f), on(end * float(k + 1) / 24.0f));
    const Vector3 along = u * -std::sin(end) + w * std::cos(end);
    arrow(solid, on(end) - along * 0.02f, on(end) + along * 0.08f, 0.09f);
    segment(solid, m.position - y * 0.1f, m.position + y * 0.26f);
}

} // namespace

void markerLines(const SceneMarker& m, float aspect, std::vector<float>& solid, std::vector<float>& faint) {
    switch (m.kind) {
    case SceneMarker::Sun: sunLines(m, solid); break;
    case SceneMarker::Point: pointLines(m, solid, faint); break;
    case SceneMarker::Spot: spotLines(m, solid, faint); break;
    case SceneMarker::Push: pushLines(m, solid, faint); break;
    case SceneMarker::Burst: burstLines(m, solid, faint); break;
    case SceneMarker::Twist: twistLines(m, solid); break;
    case SceneMarker::Camera:
        cameraLines(m, aspect, solid);
        if (m.aimed) segment(faint, m.position, m.aim); // the line of sight to its target (the target is an object: drawn itself)
        break;
    }
}

// Where a ray passes within `radius` of the segment a-b (the ray's own parameter in t): the nearest points of the two lines.
static bool nearSegment(const Ray& ray, const Vector3& a, const Vector3& b, float radius, float& t) {
    const Vector3 ab = b - a, ao = ray.origin - a;
    const float abab = dot(ab, ab), dab = dot(ray.dir, ab), dao = dot(ray.dir, ao), abao = dot(ab, ao);
    const float denominator = abab - dab * dab; // the ray's direction is a unit vector
    const float s = denominator > 1e-8f ? std::clamp((abao - dao * dab) / denominator, 0.0f, 1.0f) : 0.0f;
    const Vector3 on = a + ab * s;
    t = dot(on - ray.origin, ray.dir);
    const Vector3 off = ray.at(t) - on;
    return t >= 0 && dot(off, off) <= radius * radius;
}

bool markerHit(const SceneMarker& m, const Ray& ray, float& t) {
    Vector3 centre = m.position;
    // A directional force: its whole arrow, not only the ring it stands on - the ring may be inside a body.
    if (m.kind == SceneMarker::Push && nearSegment(ray, m.position, m.position + forceAxis(m.rotation) * m.reach, 0.1f, t)) return true;
    float radius = m.kind == SceneMarker::Sun ? 0.25f : m.kind == SceneMarker::Push ? 0.2f : m.kind == SceneMarker::Burst ? 0.34f : m.kind == SceneMarker::Twist ? 0.26f : 0.16f;
    if (m.kind == SceneMarker::Camera) centre = m.position + m.rotation.rotate(Vector3(0, 0, -1)) * (0.5f * kCameraDepth), radius = 0.24f;
    const Vector3 oc = ray.origin - centre;
    const float b = dot(oc, ray.dir), c = dot(oc, oc) - radius * radius, disc = b * b - c;
    if (disc < 0 || c <= 0) return false; // missed, or the eye is inside the ball (a camera made at the eye)
    t = -b - std::sqrt(disc);
    return t >= 0;
}

float forceArrow(const SceneGraph& g, const Force& f) {
    const float length = std::max(0.15f, std::fabs(float(f.strength / forceUnitPerMetre(g, f))));
    return f.strength < 0 ? -length : length;
}

// A directional force: its arrow's signed length in reach; a body's own force has no reach of a field to draw.
SceneMarker forceMarker(const SceneGraph& g, const Force& f) {
    SceneMarker m;
    m.kind = f.shape == ForceShape::Radial ? SceneMarker::Burst : f.shape == ForceShape::Torque ? SceneMarker::Twist : SceneMarker::Push;
    m.range = forceBody(g, f) || f.shape == ForceShape::Torque ? 0.0f : float(f.radius);
    m.idle = forceIdle(g, f) || (f.shape == ForceShape::Torque && !forceBody(g, f)); // a torque on no body turns nothing
    m.reach = f.shape == ForceShape::Directional ? forceArrow(g, f) : (f.strength < 0 ? -1.0f : 1.0f);
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
    for (const Force& f : g.forces) add(f, forceMarker(g, f));
    return out;
}
