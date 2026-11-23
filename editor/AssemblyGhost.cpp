// The assembly tools' shared pieces (AssemblyGhost.h): the pick under the mouse, the ghost's edges and its motion, an
// arrow, the API's vectors.
#include "AssemblyGhost.h"

#include "JointGlyphs.h"
#include "SceneBuilder.h"
#include "Viewport.h"

#include "math/ElementaryFunctions.h"
#include "scene/SceneGraph.h"

#include <cmath>

using ke::Vector3;

ToolPlace placeUnder(SceneBuilder* builder, Viewport* view, QPointF pos) {
    ToolPlace p;
    p.found = builder->pickShape(view->camera().screenRay(pos, view->size()), p.target, p.point, &p.normal);
    return p;
}

std::vector<Vector3> ghostEdges(const ke::SceneGraph& g, const ke::Entity& e) {
    const ke::Quaternion q = ke::entityRotation(e);
    const ke::TriMesh m = e.shape == ke::ShapeKind::Box ? ke::TriMesh() : ke::entityLocalMesh(e, g.baseDirectory);
    std::vector<Vector3> out;
    if (m.triangles.empty() || m.triangles.size() > 3000) {
        std::vector<float> lines;
        appendBoxEdges(lines, e.position, q, e.size);
        for (size_t i = 0; i + 4 <= lines.size(); i += 4) out.push_back(Vector3(lines[i], lines[i + 1], lines[i + 2]));
        return out;
    }
    for (const auto& t : m.triangles)
        for (int k = 0; k < 3; ++k) {
            out.push_back(e.position + q.rotate(m.positions[t[k]]));
            out.push_back(e.position + q.rotate(m.positions[t[(k + 1) % 3]]));
        }
    return out;
}

std::vector<float> ghostLines(const std::vector<GhostPart>& parts, float phase) {
    std::vector<float> lines;
    for (const GhostPart& g : parts) {
        ke::Quaternion q;
        Vector3 shift(0.0f);
        if (g.type == int(ke::JointType::Hinge)) q = ke::Quaternion::fromAxisAngle(g.axis, g.spins ? 3.0f * phase : 0.5f * ke::sin(3.5f * phase));
        if (g.type == int(ke::JointType::Slider)) shift = g.axis * (g.travel * 0.5f * (1 - ke::cos(3.0f * phase)));
        if (g.type == int(ke::JointType::Ball))
            q = ke::Quaternion::fromAxisAngle(g.reference, 0.35f * ke::sin(3.0f * phase)) *
                ke::Quaternion::fromAxisAngle(ke::cross(g.axis, g.reference), 0.35f * ke::cos(2.3f * phase));
        for (const Vector3& p : g.points) {
            const Vector3 m = g.anchor + q.rotate(p - g.anchor) + shift;
            lines.insert(lines.end(), {m.x, m.y, m.z, 0});
        }
    }
    return lines;
}

void appendArrow(std::vector<float>& lines, const Vector3& a, const Vector3& b, const Vector3& eye, float head) {
    const Vector3 d = ke::normalize(b - a), side = ke::normalize(ke::cross(d, eye - b));
    lines.insert(lines.end(), {a.x, a.y, a.z, 0.0f, b.x, b.y, b.z, 0.0f});
    for (float k : {-1.0f, 1.0f}) {
        const Vector3 t = b - d * head + side * (0.5f * k * head);
        lines.insert(lines.end(), {b.x, b.y, b.z, 0.0f, t.x, t.y, t.z, 0.0f});
    }
}

Vector3 across(const Vector3& axis) {
    const Vector3 a = ke::normalize(axis);
    return ke::normalize(ke::cross(a, std::fabs(a.y) < 0.9f ? Vector3(0, 1, 0) : Vector3(1, 0, 0)));
}

QJsonArray jsonOf(const Vector3& v) { return QJsonArray{double(v.x), double(v.y), double(v.z)}; }

Vector3 vecOf(const QJsonValue& v) {
    const QJsonArray a = v.toArray();
    return Vector3(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()));
}
