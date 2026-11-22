// Inverse kinematics by damped least squares on the scene graph (Kinematics.h).
#include "scene/Kinematics.h"

#include "math/ElementaryFunctions.h"
#include "scene/Assembly.h"

#include <algorithm>
#include <cmath>

namespace ke {

namespace {

const JointObject* holderOf(const SceneGraph& g, uint32_t entity) {
    for (const JointObject& j : g.joints)
        if (j.a == entity) return &j;
    return nullptr;
}

// The turn that takes rotation `from` to `to` as a rotation vector (axis times angle, |angle| <= pi).
Vector3 turnBetween(const Quaternion& to, const Quaternion& from) {
    Quaternion d = (to * from.conjugate()).normalized();
    if (d.w < 0) d = {-d.w, -d.x, -d.y, -d.z};
    const Vector3 v(d.x, d.y, d.z);
    const Real s = length(v);
    return s > 1e-12_r ? v * (2 * ke::atan2(s, d.w) / s) : v * 2;
}

// det(A) of an n x n matrix by Gaussian elimination with partial pivoting (A destroyed).
Real determinant(std::vector<Real> A, int n) {
    Real det = 1;
    for (int c = 0; c < n; ++c) {
        int p = c;
        for (int r = c + 1; r < n; ++r)
            if (std::fabs(A[size_t(r * n + c)]) > std::fabs(A[size_t(p * n + c)])) p = r;
        if (A[size_t(p * n + c)] == 0) return 0;
        if (p != c) det = -det;
        for (int k = 0; k < n; ++k) std::swap(A[size_t(c * n + k)], A[size_t(p * n + k)]);
        det *= A[size_t(c * n + c)];
        for (int r = c + 1; r < n; ++r) {
            const Real f = A[size_t(r * n + c)] / A[size_t(c * n + c)];
            for (int k = c; k < n; ++k) A[size_t(r * n + k)] -= f * A[size_t(c * n + k)];
        }
    }
    return det;
}

// Yoshikawa's manipulability of the m-row task Jacobian J (6 x n, row-major, rows past m unused): the
// smaller Gram matrix, J^T J (n x n) when n <= m, else J J^T (m x m).
Real manipulability(const std::vector<Real>& J, int m, int n) {
    const int k = std::min(m, n);
    std::vector<Real> G(size_t(k * k), 0.0_r);
    for (int a = 0; a < k; ++a)
        for (int b = 0; b < k; ++b)
            for (int i = 0; i < (n <= m ? m : n); ++i)
                G[size_t(a * k + b)] += n <= m ? J[size_t(i * n + a)] * J[size_t(i * n + b)] : J[size_t(a * n + i)] * J[size_t(b * n + i)];
    return std::sqrt(std::max(determinant(G, k), 0.0_r));
}

// Solves the n x n system A x = b in place by Gaussian elimination with partial pivoting (n <= 6 here).
void solve(std::vector<Real>& A, std::vector<Real>& b, int n) {
    for (int c = 0; c < n; ++c) {
        int p = c;
        for (int r = c + 1; r < n; ++r)
            if (std::fabs(A[size_t(r * n + c)]) > std::fabs(A[size_t(p * n + c)])) p = r;
        for (int k = 0; k < n; ++k) std::swap(A[size_t(c * n + k)], A[size_t(p * n + k)]);
        std::swap(b[size_t(c)], b[size_t(p)]);
        for (int r = c + 1; r < n; ++r) {
            const Real f = A[size_t(r * n + c)] / A[size_t(c * n + c)];
            for (int k = c; k < n; ++k) A[size_t(r * n + k)] -= f * A[size_t(c * n + k)];
            b[size_t(r)] -= f * b[size_t(c)];
        }
    }
    for (int r = n - 1; r >= 0; --r) {
        for (int k = r + 1; k < n; ++k) b[size_t(r)] -= A[size_t(r * n + k)] * b[size_t(k)];
        b[size_t(r)] /= A[size_t(r * n + r)];
    }
}

// The Jacobian's column of a link: how the effector's point and turn move per unit of the coordinate -
// a turn about axis a through o: (a x (p - o), a); a travel along a: (a, 0). In the world.
void column(const SceneGraph& g, const IkLink& l, const Vector3& p, Real out[6]) {
    Vector3 oa, ob;
    Quaternion fa, fb;
    jointWorldFrames(g, *findJoint(g, l.joint), oa, fa, ob, fb);
    Vector3 e(0.0_r);
    e[l.dof % 3] = 1;
    const Vector3 a = fb.rotate(e), v = l.dof >= DofRx ? cross(a, p - ob) : a, w = l.dof >= DofRx ? a : Vector3(0.0_r);
    for (int k = 0; k < 3; ++k) out[k] = v[k], out[k + 3] = w[k];
}

} // namespace

std::vector<IkLink> ikChain(const SceneGraph& g, uint32_t effector) {
    std::vector<IkLink> chain;
    for (const JointObject* j = holderOf(g, effector); j && chain.size() < 64; j = j->b ? holderOf(g, j->b) : nullptr)
        if (mainFreedom(*j) >= 0) chain.insert(chain.begin(), IkLink{j->id, mainFreedom(*j), jointPosition(g, *j)});
    return chain;
}

// Each round: the error (the travel to the point, the turn's rotation vector weighted by a length so metres
// and radians count alike), J^T (J J^T + lambda^2 I)^-1 e as the step - each coordinate's step held under
// 0.2 rad (or 0.2 m) so a far target is walked to, not jumped at -, the coordinates set by forward kinematics.
IkResult solveIk(SceneGraph& g, uint32_t effector, const Vector3& position, const Quaternion* rotation, Real positionTolerance, Real angleTolerance,
                 int iterations) {
    IkResult r;
    r.links = ikChain(g, effector);
    const SceneObject* tip = findObject(g, effector);
    const int n = int(r.links.size());
    if (!tip || n == 0) return r;
    const Real weight = rotation ? 0.3_r : 0.0_r, damping = 0.05_r; // metres a radian of turn counts as (0: the point only); lambda
    for (r.iterations = 0; r.iterations <= iterations; ++r.iterations) {
        Vector3 p;
        Quaternion q;
        worldPose(g, *findObject(g, effector), p, q);
        const Vector3 dp = position - p, dw = rotation ? turnBetween(*rotation, q) : Vector3(0.0_r);
        r.positionError = length(dp), r.angleError = length(dw);
        r.residual.push_back(std::sqrt(length2(dp) + weight * weight * length2(dw)));
        std::vector<Real> J(size_t(6 * n)), A(36, 0.0_r), e = {dp.x, dp.y, dp.z, weight * dw.x, weight * dw.y, weight * dw.z};
        for (int c = 0; c < n; ++c) {
            Real col[6];
            column(g, r.links[size_t(c)], p, col);
            for (int k = 0; k < 6; ++k) J[size_t(k * n + c)] = col[k] * (k >= 3 ? weight : 1.0_r);
        }
        r.manipulability = manipulability(J, rotation ? 6 : 3, n);
        if ((r.reached = r.positionError <= positionTolerance && r.angleError <= angleTolerance) || r.iterations == iterations) break;
        for (int i = 0; i < 6; ++i)
            for (int k = 0; k < 6; ++k) {
                for (int c = 0; c < n; ++c) A[size_t(i * 6 + k)] += J[size_t(i * n + c)] * J[size_t(k * n + c)];
                A[size_t(i * 6 + k)] += i == k ? damping * damping : 0.0_r;
            }
        solve(A, e, 6);
        for (int c = 0; c < n; ++c) {
            Real step = 0;
            for (int k = 0; k < 6; ++k) step += J[size_t(k * n + c)] * e[size_t(k)];
            IkLink& l = r.links[size_t(c)];
            l.coordinate = poseJoint(g, l.joint, l.dof, l.coordinate + std::clamp(step, -0.2_r, 0.2_r));
        }
    }
    return r;
}

namespace {
JointDrive* servoOf(SceneGraph& g, const IkLink& l) {
    for (JointObject& j : g.joints) {
        JointDrive& d = j.dof[l.dof].drive;
        if (j.id == l.joint) return d.enabled && d.stiffness > 0 && d.constant == 0 ? &d : nullptr;
    }
    return nullptr;
}
} // namespace

// Targets: solved on a copy posed where the servos are sent (warm-started from the last command, the
// robot's present state) - a goal dragged on stays on the arm's branch, an elbow up does not flip to down. Pose:
// solved on the scene itself, its links as they stand. Either way the servos then hold what was found.
IkResult reachTargets(SceneGraph& g, uint32_t effector, const Vector3& position, const Quaternion* rotation, bool pose,
                      std::vector<uint32_t>* undriven) {
    SceneGraph work = g;
    for (const IkLink& l : ikChain(work, effector))
        if (const JointDrive* d = pose ? nullptr : servoOf(work, l)) poseJoint(work, l.joint, l.dof, d->target);
    const IkResult ik = solveIk(pose ? g : work, effector, position, rotation);
    for (const IkLink& l : ik.links) {
        if (JointDrive* d = servoOf(g, l)) d->target = l.coordinate; // a servo: its new target
        else if (undriven) undriven->push_back(l.joint);
    }
    return ik;
}

IkResult reachGoal(SceneGraph& g, uint32_t goal, std::vector<uint32_t>* undriven) {
    const SceneObject* o = findObject(g, goal);
    uint32_t effector = 0;
    for (const Entity& e : g.entities)
        if (e.id == goal) effector = e.goalOf;
    if (!o || effector == 0) return {};
    Vector3 p;
    Quaternion q;
    worldPose(g, *o, p, q);
    const bool full = ikChain(g, effector).size() >= 6; // fewer freedoms than a pose has: the point only, as a robot's 3-axis arm
    return reachTargets(g, effector, p, full ? &q : nullptr, false, undriven);
}

uint32_t addGoal(SceneGraph& g, uint32_t effector) {
    const SceneObject* tip = findObject(g, effector);
    if (!tip || ikChain(g, effector).empty()) return 0;
    Entity goal;
    goal.id = nextId(g);
    goal.name = "Цель «" + tip->name + "»";
    const Entity* link = nullptr;
    for (const Entity& e : g.entities)
        if (e.id == effector) link = &e;
    const Real reach = link && link->shape != ShapeKind::Mesh ? maxComp(link->size) : 0.06_r;
    goal.size = Vector3(std::max(0.05_r, 1.3_r * reach)); // a box around the hand, not one flush with its faces
    goal.color = {0.30_r, 0.95_r, 0.55_r};
    goal.goalOf = effector;
    Quaternion q;
    worldPose(g, *tip, goal.position, q);
    goal.rotationDeg = eulerDegrees(q);
    g.entities.push_back(goal);
    return goal.id;
}

namespace {

// The path's control points and turns: the goal's own place and turn, then each waypoint (turned as the goal when
// it has no turns), back to the start when looped; each turn on the same side of the sphere as the one before.
void pathControls(const Entity& goal, std::vector<Vector3>& points, std::vector<Quaternion>& turns) {
    const bool turned = goal.pathTurns.size() == goal.path.size();
    points = {goal.position}, turns = {entityRotation(goal)};
    for (size_t k = 0; k < goal.path.size(); ++k) {
        SceneObject o;
        o.rotationDeg = turned ? goal.pathTurns[k] : goal.rotationDeg;
        points.push_back(goal.path[k]), turns.push_back(objectRotation(o));
    }
    if (goal.pathLoop) points.push_back(points.front()), turns.push_back(turns.front());
    for (size_t k = 1; k < turns.size(); ++k)
        if (dot(turns[k], turns[k - 1]) < 0) turns[k] = Quaternion{-turns[k].w, -turns[k].x, -turns[k].y, -turns[k].z};
}

// Quaternion log and exp of a unit turn: (0, axis * half angle) and back.
Quaternion qlog(const Quaternion& q) {
    const Real s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    const Real a = s > 1e-12_r ? ke::atan2(s, q.w) / s : 1.0_r;
    return {0, q.x * a, q.y * a, q.z * a};
}
Quaternion qexp(const Quaternion& v) {
    const Real a = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z), f = a > 1e-12_r ? ke::sin(a) / a : 1.0_r;
    return Quaternion{ke::cos(a), v.x * f, v.y * f, v.z * f}.normalized();
}

// Slerp that keeps its arc (Shoemake's SlerpNoInvert): SQUAD's blends must not jump to the other arc where the
// two turns are 90 degrees apart on the sphere - the shortest-arc slerp would turn the result half round there.
Quaternion slerpKeep(const Quaternion& a, const Quaternion& b, Real t) {
    const Real c = std::clamp(dot(a, b), -1.0_r, 1.0_r);
    if (std::fabs(c) > 0.9995_r) return Quaternion{a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}.normalized();
    const Real theta = ke::acos(c), sa = ke::sin((1 - t) * theta) / ke::sin(theta), sb = ke::sin(t * theta) / ke::sin(theta);
    return Quaternion{a.w * sa + b.w * sb, a.x * sa + b.x * sb, a.y * sa + b.y * sb, a.z * sa + b.z * sb}.normalized();
}

// SQUAD's inner control of turn k: q_k exp(-(log(q_k^-1 q_k+1) + log(q_k^-1 q_k-1)) / 4); an open path's ends are their own.
Quaternion squadControl(const std::vector<Quaternion>& q, size_t k, bool loop) {
    const size_t n = q.size();
    if (!loop && (k == 0 || k + 1 == n)) return q[k];
    const Quaternion& prev = q[k == 0 ? n - 2 : k - 1]; // looped: the last point is the first again
    const Quaternion& next = q[k + 1 == n ? 1 : k + 1];
    const Quaternion inv = q[k].conjugate(), a = qlog(inv * next), b = qlog(inv * prev);
    return q[k] * qexp(Quaternion{0, -(a.x + b.x) / 4, -(a.y + b.y) / 4, -(a.z + b.z) / 4});
}

// Centripetal Catmull-Rom between p1 and p2 (Barry-Goldman pyramid, knots by sqrt of the chord: no cusps
// nor self-crossings), at u in [0, 1].
Vector3 catmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, Real u) {
    const Real t1 = std::sqrt(std::max(length(p1 - p0), 1e-9_r)), t2 = t1 + std::sqrt(std::max(length(p2 - p1), 1e-9_r));
    const Real t3 = t2 + std::sqrt(std::max(length(p3 - p2), 1e-9_r)), t = t1 + (t2 - t1) * u;
    const Vector3 a1 = p0 * ((t1 - t) / t1) + p1 * (t / t1), a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1));
    const Vector3 a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));
    const Vector3 b1 = a1 * ((t2 - t) / t2) + a2 * (t / t2), b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));
    return b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1));
}

// The spline's segment k at u, its outer neighbours mirrored at an open path's ends (or wrapped, looped).
Vector3 splineAt(const std::vector<Vector3>& p, size_t k, Real u, bool loop) {
    const size_t n = p.size();
    const Vector3 before = k > 0 ? p[k - 1] : loop ? p[n - 2] : p[0] * 2 - p[1];
    const Vector3 after = k + 2 < n ? p[k + 2] : loop ? p[1] : p[n - 1] * 2 - p[n - 2];
    return catmullRom(before, p[k], p[k + 1], after, u);
}

constexpr int kSamples = 128; // a segment's arc length by 128 chords: the speed held to a fraction of a percent

// Segment and u at arc length s along the spline (a table of chords per segment, linear between).
void splineLocate(const std::vector<Vector3>& p, bool loop, Real s, size_t& segment, Real& u) {
    for (segment = 0; segment + 1 < p.size(); ++segment) {
        Vector3 last = p[segment];
        for (int i = 1; i <= kSamples; ++i) {
            const Vector3 next = splineAt(p, segment, Real(i) / kSamples, loop);
            const Real chord = length(next - last);
            if (s <= chord && chord > 0) return void(u = (Real(i - 1) + s / chord) / kSamples);
            s -= chord, last = next;
        }
    }
    segment = p.size() - 2, u = 1;
}

Real splineLength(const std::vector<Vector3>& p, bool loop) {
    Real total = 0;
    for (size_t k = 0; k + 1 < p.size(); ++k) {
        Vector3 last = p[k];
        for (int i = 1; i <= kSamples; ++i) {
            const Vector3 next = splineAt(p, k, Real(i) / kSamples, loop);
            total += length(next - last), last = next;
        }
    }
    return total;
}

} // namespace

// A straight stretch's length as the path walks it: the travel, and the turn between its ends (2 acos |q1 . q2|, the
// angle of the relative rotation) at pathTurnLength metres a radian.
static Real stretch(const Entity& goal, const std::vector<Vector3>& points, const std::vector<Quaternion>& turns, size_t k) {
    const Real travel = length(points[k] - points[k - 1]);
    if (goal.pathTurnLength == 0) return travel;
    const Real c = std::min(1.0_r, std::fabs(dot(turns[k], turns[k - 1])));
    return travel + goal.pathTurnLength * 2 * ke::acos(c);
}

Real pathLength(const Entity& goal) {
    std::vector<Vector3> points;
    std::vector<Quaternion> turns;
    pathControls(goal, points, turns);
    if (goal.pathSmooth && points.size() >= 3) return splineLength(points, goal.pathLoop);
    Real total = 0;
    for (size_t k = 1; k < points.size(); ++k) total += stretch(goal, points, turns, k);
    return total;
}

void pathPose(const Entity& goal, Real elapsed, Vector3& position, Quaternion& turn, bool* done) {
    std::vector<Vector3> points;
    std::vector<Quaternion> turns;
    pathControls(goal, points, turns);
    const bool smooth = goal.pathSmooth && points.size() >= 3;
    const Real total = pathLength(goal);
    Real s = std::max(goal.pathSpeed * elapsed, 0.0_r);
    if (done) *done = !goal.pathLoop && s >= total;
    if (goal.pathLoop && total > 0) s -= total * std::floor(s / total);
    if (smooth) { // the spline at constant speed, the turn by SQUAD: slerp(slerp(q_k, q_k+1), slerp(s_k, s_k+1), 2u(1 - u))
        size_t k = 0;
        Real u = 0;
        splineLocate(points, goal.pathLoop, s, k, u);
        position = splineAt(points, k, u, goal.pathLoop);
        const Quaternion outer = slerpKeep(turns[k], turns[k + 1], u), inner = slerpKeep(squadControl(turns, k, goal.pathLoop), squadControl(turns, k + 1, goal.pathLoop), u);
        turn = slerpKeep(outer, inner, 2 * u * (1 - u));
        return;
    }
    for (size_t k = 1; k < points.size(); ++k) {
        const Real step = stretch(goal, points, turns, k);
        if (s <= step && step > 0) {
            position = points[k - 1] + (points[k] - points[k - 1]) * (s / step), turn = slerp(turns[k - 1], turns[k], s / step);
            return;
        }
        s -= step;
    }
    position = points.back(), turn = turns.back();
}

Vector3 pathPoint(const Entity& goal, Real elapsed, bool* done) {
    Vector3 p;
    Quaternion q;
    pathPose(goal, elapsed, p, q, done);
    return p;
}

} // namespace ke
