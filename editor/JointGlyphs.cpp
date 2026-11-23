// The glyphs of joints and the magnet's ghost box as line pairs (JointGlyphs.h).
#include "JointGlyphs.h"

#include "math/ElementaryFunctions.h"

#include <QJsonArray>

#include <algorithm>

using ke::Vector3;

namespace {

void segment(std::vector<float>& d, const Vector3& p, const Vector3& q) {
    d.insert(d.end(), {p.x, p.y, p.z, 0, q.x, q.y, q.z, 0});
}

// An arc about `axis` through the point centre + radius * from, from angle a0 to a1 [rad].
void arc(std::vector<float>& d, const Vector3& centre, const Vector3& axis, const Vector3& from, float radius, float a0, float a1) {
    const Vector3 u = ke::normalize(from), v = ke::cross(axis, u);
    const int n = std::max(4, int(24.0f * std::abs(a1 - a0) / 6.2831853f));
    Vector3 prev = centre + (u * ke::cos(a0) + v * ke::sin(a0)) * radius;
    for (int k = 1; k <= n; ++k) {
        const float a = a0 + (a1 - a0) * float(k) / float(n);
        const Vector3 p = centre + (u * ke::cos(a) + v * ke::sin(a)) * radius;
        segment(d, prev, p);
        prev = p;
    }
}

// An arrowhead at `tip` pointing along `dir`, its two barbs in the plane of `side`.
void arrowhead(std::vector<float>& d, const Vector3& tip, const Vector3& dir, const Vector3& side, float length) {
    segment(d, tip, tip - dir * length + side * (0.5f * length));
    segment(d, tip, tip - dir * length - side * (0.5f * length));
}

// A hinge as its rotor's disc reads it - the motor's angle as the complex number e^{i theta} on the unit circle: the real
// axis (the zero, fixed to b) a long tick and the imaginary one (+90 degrees) a short one, the angle now a phasor with
// an arrowhead and its argument an inner arc from the zero, a servo's target a notch on the ring - the gap between the
// two is the error the motor works off.
void hinge(std::vector<float>& d, const ke::JointVis& j, const Vector3& n, float r) {
    segment(d, j.anchorA - n * (1.6f * r), j.anchorA + n * (1.6f * r));
    const Vector3 ref = ke::normalize(j.reference - n * ke::dot(j.reference, n)), im = ke::cross(n, ref);
    arc(d, j.anchorA, n, ref, r, 0.0f, 6.2831853f);
    segment(d, j.anchorA + ref * (0.8f * r), j.anchorA + ref * (1.35f * r)); // Re: the zero
    segment(d, j.anchorA + im * (0.9f * r), j.anchorA + im * (1.15f * r));   // Im: +90 degrees
    const float angle = float(j.angle);
    const Vector3 now = ref * ke::cos(angle) + im * ke::sin(angle);
    segment(d, j.anchorA, j.anchorA + now * (1.25f * r)); // the phasor: the angle now, turning with the link
    arrowhead(d, j.anchorA + now * (1.25f * r), now, ke::cross(n, now), 0.25f * r);
    if (std::abs(angle) > 1e-3f) arc(d, j.anchorA, n, ref, 0.45f * r, 0.0f, angle); // its argument theta
    if (j.servo) { // the target: a notch pointing at the ring from outside
        const float t = float(j.target);
        const Vector3 at = ref * ke::cos(t) + im * ke::sin(t), side = ke::cross(n, at);
        const Vector3 tip = j.anchorA + at * (1.02f * r), back = j.anchorA + at * (1.3f * r);
        segment(d, tip, back + side * (0.12f * r)), segment(d, tip, back - side * (0.12f * r)), segment(d, back + side * (0.12f * r), back - side * (0.12f * r));
    }
    if (j.limited) {
        arc(d, j.anchorA, n, ref, 1.15f * r, j.lower, j.upper);
        for (float a : {j.lower, j.upper})
            segment(d, j.anchorA, j.anchorA + (ref * ke::cos(a) + ke::cross(n, ref) * ke::sin(a)) * (1.15f * r));
    } else { // which way the angle (and the motor) turns: an arrowhead on the ring
        const Vector3 at = j.anchorA + ref * r, tangent = ke::cross(n, ref);
        arrowhead(d, at + tangent * (0.3f * r), tangent, ref, 0.35f * r);
    }
}

void slider(std::vector<float>& d, const ke::JointVis& j, const Vector3& n, float r) {
    const Vector3 side = ke::normalize(ke::anyPerpendicular(n));
    const Vector3 p0 = j.anchorA - n * (3.0f * r), p1 = j.anchorA + n * (3.0f * r);
    segment(d, p0, p1);
    arrowhead(d, p1, n, side, 0.5f * r);
    arrowhead(d, p0, n * -1.0f, side, 0.5f * r);
    if (!j.limited) return;
    for (float t : {j.lower, j.upper}) segment(d, j.anchorA + n * t - side * (0.4f * r), j.anchorA + n * t + side * (0.4f * r));
}

} // namespace

void appendJointGlyph(std::vector<float>& lines, const ke::JointVis& j, float size) {
    const float r = std::max(size, 0.01f);
    const Vector3 n = ke::length(j.axis) > 0 ? ke::normalize(j.axis) : Vector3(0, 1, 0);
    if (ke::length(j.anchorB - j.anchorA) > 1e-4f) segment(lines, j.anchorA, j.anchorB); // the solver's error, if any
    switch (j.type) {
    case ke::JointType::Hinge: hinge(lines, j, n, r); break;
    case ke::JointType::Slider: slider(lines, j, n, r); break;
    case ke::JointType::Ball:
        for (int a = 0; a < 3; ++a) {
            Vector3 axis(0.0f), from(0.0f);
            axis[a] = 1, from[(a + 1) % 3] = 1;
            arc(lines, j.anchorA, axis, from, 0.8f * r, 0.0f, 6.2831853f);
        }
        break;
    case ke::JointType::Fixed: appendBoxEdges(lines, j.anchorA, ke::Quaternion(), Vector3(0.9f * r)); break;
    default: segment(lines, j.anchorA, j.anchorB); break; // a distance joint: its rod
    }
}

namespace {

Vector3 unitAxis(int k) {
    Vector3 e(0.0f);
    e[k] = 1;
    return e;
}

// A travel freedom along e from the origin (the frame b's): its arrow, span, ticks and spring.
void travelGlyph(JointGizmoLines& out, const ke::JointVis& j, int k, const Vector3& o, float r) {
    const Vector3 e = j.frameB.rotate(unitAxis(k)), side = j.frameB.rotate(unitAxis((k + 1) % 3));
    std::vector<float>& d = out.axis[k];
    if (j.motion[k] == ke::JointMotion::Locked) return segment(out.dim, o - e * (0.3f * r), o + e * (0.3f * r));
    const bool limited = j.motion[k] == ke::JointMotion::Limited;
    const float lo = limited ? float(j.lowers[k]) : -1.6f * r, hi = limited ? float(j.uppers[k]) : 1.6f * r;
    segment(d, o + e * lo, o + e * hi);
    if (limited) for (float t : {lo, hi}) segment(d, o + e * t - side * (0.35f * r), o + e * t + side * (0.35f * r));
    else arrowhead(d, o + e * hi, e, side, 0.4f * r), arrowhead(d, o + e * lo, e * -1.0f, side, 0.4f * r);
    const Vector3 now = o + e * float(j.coordinates[k]); // where frame a's origin is along it
    segment(d, now - side * (0.2f * r), now + side * (0.2f * r));
    if (!j.driven[k]) return;
    Vector3 prev = o + side * (0.25f * r) + e * lo; // a spring's zigzag beside the span
    for (int z = 1; z <= 12; ++z) {
        const Vector3 p = o + e * (lo + (hi - lo) * float(z) / 12.0f) + side * (z % 2 ? 0.45f * r : 0.25f * r);
        segment(d, prev, p);
        prev = p;
    }
}

// A turn freedom about e: its spin vector (the axis, its head where a turn counts positive - the right hand), its ring or arc
// with a small arrowhead the way the angle grows, the spoke of the turn now, a motor's bigger arrowhead.
void turnGlyph(JointGizmoLines& out, const ke::JointVis& j, int k, const Vector3& o, float r) {
    if (j.motion[k + 3] == ke::JointMotion::Locked) return;
    const Vector3 e = j.frameB.rotate(unitAxis(k)), ref = j.frameB.rotate(unitAxis((k + 1) % 3)), v = ke::cross(e, ref);
    std::vector<float>& d = out.axis[k];
    const bool limited = j.motion[k + 3] == ke::JointMotion::Limited;
    const float ring = r * (0.9f + 0.15f * float(k));
    if (limited) {
        arc(d, o, e, ref, ring, float(j.lowers[k + 3]), float(j.uppers[k + 3]));
        for (float a : {float(j.lowers[k + 3]), float(j.uppers[k + 3])}) segment(d, o, o + (ref * ke::cos(a) + v * ke::sin(a)) * ring);
    } else {
        arc(d, o, e, ref, ring, 0.0f, 6.2831853f);
    }
    segment(d, o - e * (0.6f * ring), o + e * (1.9f * ring));
    arrowhead(d, o + e * (1.9f * ring), e, ref, 0.35f * r);
    const float sense = limited ? 0.5f * float(j.lowers[k + 3] + j.uppers[k + 3]) : 1.0f;
    arrowhead(d, o + (ref * ke::cos(sense) + v * ke::sin(sense)) * ring, ref * -ke::sin(sense) + v * ke::cos(sense), e, 0.22f * r);
    const float now = float(j.coordinates[k + 3]);
    segment(d, o, o + (ref * ke::cos(now) + v * ke::sin(now)) * (0.75f * ring));
    if (j.driven[k + 3]) arrowhead(d, o + (ref * ke::cos(now + 0.6f) + v * ke::sin(now + 0.6f)) * ring,
                                   ref * -ke::sin(now + 0.6f) + v * ke::cos(now + 0.6f), e, 0.4f * r);
}

} // namespace

void appendJointGizmo(JointGizmoLines& out, const ke::JointVis& j, float size) {
    const float r = std::max(size, 0.01f);
    const Vector3 o = j.anchorB;
    for (int k = 0; k < 3; ++k) travelGlyph(out, j, k, o, r), turnGlyph(out, j, k, o, r);
}

void appendBoxEdges(std::vector<float>& lines, const Vector3& centre, const ke::Quaternion& rotation, const Vector3& size) {
    const Vector3 h = size * 0.5f;
    auto corner = [&](int k) {
        return centre + rotation.rotate(Vector3(k & 1 ? h.x : -h.x, k & 2 ? h.y : -h.y, k & 4 ? h.z : -h.z));
    };
    for (int k = 0; k < 8; ++k)
        for (int bit : {1, 2, 4})
            if (!(k & bit)) segment(lines, corner(k), corner(k | bit));
}

namespace {

QJsonArray json3(const Vector3& v) { return QJsonArray{double(v.x), double(v.y), double(v.z)}; }
QJsonArray json4(const ke::Quaternion& q) { return QJsonArray{double(q.w), double(q.x), double(q.y), double(q.z)}; }

Vector3 vec3(const QJsonValue& v) {
    const QJsonArray a = v.toArray();
    return Vector3(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()));
}

ke::Quaternion quat(const QJsonValue& v) {
    const QJsonArray a = v.toArray();
    return a.size() == 4 ? ke::Quaternion{float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()), float(a[3].toDouble())}
                         : ke::Quaternion();
}

ke::JointType typeOf(const QString& word) {
    return word == "hinge" ? ke::JointType::Hinge : word == "slider" ? ke::JointType::Slider : word == "ball" ? ke::JointType::Ball
         : word == "weld" ? ke::JointType::Fixed : ke::JointType::Generic;
}

} // namespace

QJsonObject jointVisJson(const ke::JointVis& v, const char* kindWord) {
    QJsonArray motion, lower, upper, driven;
    for (int k = 0; k < 6; ++k) {
        motion.append(int(v.motion[k]));
        lower.append(double(v.lowers[k]));
        upper.append(double(v.uppers[k]));
        driven.append(v.driven[k]);
    }
    return QJsonObject{{"kind", kindWord}, {"anchor", json3(v.anchorA)}, {"axis", json3(v.axis)}, {"reference", json3(v.reference)},
                       {"size", double(v.size)}, {"limited", v.limited}, {"lower", double(v.lower)}, {"upper", double(v.upper)},
                       {"frame_a", json4(v.frameA)}, {"frame_b", json4(v.frameB)}, {"motion", motion}, {"lowers", lower},
                       {"uppers", upper}, {"driven", driven}};
}

ke::JointVis jointVisFromJson(const QJsonObject& j) {
    ke::JointVis v; // a ghost of a joint not made yet: no id, and no tie - its bodies' places are its anchor
    v.type = typeOf(j.value("kind").toString());
    v.anchorA = v.anchorB = v.bodyA = v.bodyB = vec3(j.value("anchor"));
    v.axis = vec3(j.value("axis"));
    v.reference = vec3(j.value("reference"));
    v.size = float(j.value("size").toDouble());
    v.limited = j.value("limited").toBool(), v.lower = float(j.value("lower").toDouble()), v.upper = float(j.value("upper").toDouble());
    v.generic = j.contains("motion");
    v.frameA = quat(j.value("frame_a")), v.frameB = quat(j.value("frame_b"));
    const QJsonArray motion = j.value("motion").toArray(), lower = j.value("lowers").toArray(), upper = j.value("uppers").toArray(),
                     driven = j.value("driven").toArray();
    for (int k = 0; k < 6 && k < motion.size(); ++k) {
        v.motion[k] = ke::JointMotion(motion[k].toInt());
        v.lowers[k] = float(lower[k].toDouble()), v.uppers[k] = float(upper[k].toDouble()), v.driven[k] = driven[k].toBool();
    }
    return v;
}
