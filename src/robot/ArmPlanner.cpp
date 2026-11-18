// The arm's planner (ArmPlanner.h): spheres over a link's surface cell by cell, forward kinematics hinge by hinge,
// inverse kinematics on the model (damped least squares on a Jacobian of differences), the spheres' clearance from the
// distance field,
// stretches in the joints checked so finely that no sphere moves more than `resolution` between two checks (a turn of
// d rad moves a point at most reach * d), RRT-Connect's extend and connect (Kuffner & LaValle 2000, fig. 3-5) and the
// corners cut at random (Geraerts & Overmars 2007, "Creating high-quality paths for motion planning").
#include "robot/ArmPlanner.h"
#include "robot/ArmSelfCollision.h"

#include "core/Random.h"

#include <algorithm>
#include <chrono>
#include <array>
#include <map>
#include <set>
#include <cmath>

namespace ke {

namespace {

using Config = std::vector<Real>;

struct Tree {
    std::vector<Config> nodes;
    std::vector<int> parent; // -1: a root
};

Real span(const Config& a, const Config& b) { // the largest joint's turn between two sets of coordinates
    Real m = 0;
    for (size_t i = 0; i < a.size(); ++i) m = std::max(m, std::fabs(a[i] - b[i]));
    return m;
}

int nearest(const Tree& t, const Config& q) {
    int best = 0;
    Real bestD = kInf;
    for (size_t n = 0; n < t.nodes.size(); ++n) {
        Real d = 0;
        for (size_t i = 0; i < q.size(); ++i) d += sqr(t.nodes[n][i] - q[i]);
        if (d < bestD) bestD = d, best = int(n);
    }
    return best;
}

Config toward(const Config& from, const Config& to, Real step) {
    const Real s = span(from, to);
    if (s <= step) return to;
    Config q(from.size());
    for (size_t i = 0; i < q.size(); ++i) q[i] = from[i] + (to[i] - from[i]) * (step / s);
    return q;
}

std::vector<Config> chainTo(const Tree& t, int i) { // root .. i
    std::vector<Config> c;
    for (; i >= 0; i = t.parent[size_t(i)]) c.push_back(t.nodes[size_t(i)]);
    std::reverse(c.begin(), c.end());
    return c;
}

} // namespace

std::vector<ArmSphere> armSpheres(const std::vector<Vector3>& points, Real step) {
    std::vector<ArmSphere> out;
    if (points.empty()) return out;
    Vector3 lo = points[0];
    for (const Vector3& p : points) lo = vmin(lo, p);
    std::map<std::array<int, 3>, size_t> cells; // a cell's index in `boxes`
    std::vector<std::pair<Vector3, Vector3>> boxes;
    std::vector<size_t> of(points.size());
    for (size_t n = 0; n < points.size(); ++n) {
        const Vector3& p = points[n];
        const std::array<int, 3> key{int((p.x - lo.x) / step), int((p.y - lo.y) / step), int((p.z - lo.z) / step)};
        const auto it = cells.emplace(key, boxes.size()).first;
        if (it->second == boxes.size()) boxes.push_back({p, p});
        boxes[it->second].first = vmin(boxes[it->second].first, p), boxes[it->second].second = vmax(boxes[it->second].second, p);
        of[n] = it->second;
    }
    for (const auto& b : boxes) out.push_back({(b.first + b.second) * 0.5_r, 0});
    for (size_t n = 0; n < points.size(); ++n) out[of[n]].radius = std::max(out[of[n]].radius, length(points[n] - out[of[n]].centre));
    return out;
}

void armPoses(const ArmModel& m, const std::vector<Real>& q, std::vector<ArmPose>& links) {
    links.resize(m.joints.size());
    for (size_t i = 0; i < m.joints.size(); ++i) {
        const ArmJoint& j = m.joints[i];
        const ArmPose& p = j.parent < 0 ? m.base : links[size_t(j.parent)];
        const Quaternion frameB = p.turn * j.turnB;
        const Vector3 origin = p.position + p.turn.rotate(j.originB);
        const Quaternion frameA = frameB * Quaternion::fromAxisAngle(Vector3(1, 0, 0), q[i]); // QA = QB Rx(q)
        const Quaternion turn = (frameA * j.turnA.conjugate()).normalized();                   // the link's: QA = q_a R(frame a)
        links[i] = {origin - turn.rotate(j.originA), turn};                                     // o_A = p_a + q_a anchor_a = o_B
    }
}

ArmPose effectorPose(const ArmModel& m, const std::vector<Real>& q) {
    std::vector<ArmPose> links;
    armPoses(m, q, links);
    const ArmPose& last = links.empty() ? m.base : links.back();
    return {last.position + last.turn.rotate(m.effector.position), (last.turn * m.effector.turn).normalized()};
}

ArmPlanner::ArmPlanner(const ArmModel& model, std::function<Real(const Vector3&)> clearance, const ArmPlanOptions& options,
                       std::function<Real(const Vector3&, Vector3&)> gradient)
    : model_(model), clearance_(std::move(clearance)), gradient_(std::move(gradient)), options_(options) {
    selfCollision_ = options_.selfIndex ? options_.selfIndex : std::make_shared<ArmSelfCollisionIndex>(model_, options_.selfMargin);
    selfOverlap_ = options_.selfMargin > 0 ? std::make_shared<ArmSelfCollisionIndex>(model_) : selfCollision_;
    std::vector<ArmPose> links;
    armPoses(model_, Config(model_.joints.size(), 0.0_r), links);
    const Vector3 first = links.empty() ? model_.base.position : links.front().position;
    reach_ = 0.1_r;
    for (size_t i = 0; i < links.size(); ++i)
        for (const ArmSphere& s : model_.joints[i].spheres)
            reach_ = std::max(reach_, length(links[i].position + links[i].turn.rotate(s.centre) - first) + s.radius);
    levers(links);
}

// Each joint's lever: the longest way along the chain from its pivot to the far edge of a sphere it carries (the
// pivots are fixed in both links a joint joins, so the way is the same in every pose). A point's velocity is
// sum_i qdot_i axis_i x (p - pivot_i), so a straight stretch dq moves no point of the arm farther than
// sum_i |dq_i| lever_i (Schwarzer, Saha & Latombe 2005, section III).
void ArmPlanner::levers(const std::vector<ArmPose>& links) {
    std::vector<Vector3> pivots(links.size());
    for (size_t i = 0; i < links.size(); ++i) {
        const ArmJoint& joint = model_.joints[i];
        const ArmPose& parent = joint.parent < 0 ? model_.base : links[size_t(joint.parent)];
        pivots[i] = parent.position + parent.turn.rotate(joint.originB);
    }
    lever_.assign(links.size(), 0);
    const auto carry = [&](int k, const ArmSphere& s) { // the sphere on link k, carried by every joint up its chain
        Real along = length(links[size_t(k)].position + links[size_t(k)].turn.rotate(s.centre) - pivots[size_t(k)]) + s.radius;
        for (int j = k; j >= 0; j = model_.joints[size_t(j)].parent) {
            lever_[size_t(j)] = std::max(lever_[size_t(j)], along);
            const int up = model_.joints[size_t(j)].parent;
            if (up >= 0) along += length(pivots[size_t(j)] - pivots[size_t(up)]);
        }
    };
    for (size_t k = 0; k < links.size(); ++k)
        for (const ArmSphere& s : model_.joints[k].spheres) carry(int(k), s);
    for (const ArmCollisionBody& body : model_.collisionBodies)
        if (body.link >= 0 && size_t(body.link) < links.size())
            for (const ArmSphere& s : body.spheres) carry(body.link, s);
}

Real ArmPlanner::clearance(const std::vector<Real>& q, int* link, ArmSphere* where) const {
    std::vector<ArmPose> links;
    armPoses(model_, q, links);
    Real least = kInf;
    for (size_t i = 0; i < links.size(); ++i)
        for (const ArmSphere& s : model_.joints[i].spheres) {
            const Vector3 centre = links[i].position + links[i].turn.rotate(s.centre);
            const Real c = clearance_(centre) - s.radius;
            if (c < least) {
                least = c;
                if (link) *link = int(i);
                if (where) *where = {centre, s.radius};
            }
        }
    return least;
}

bool ArmPlanner::free(const std::vector<Real>& q, Real* witness, Real* room) const {
    if (witness) *witness = -kInf;
    if (room) *room = 0;
    if (q.size() != model_.joints.size()) return false;
    for (size_t i = 0; i < q.size(); ++i)
        if (!std::isfinite(q[i]) || q[i] < model_.joints[i].lower || q[i] > model_.joints[i].upper) return false;
    // Own-body overlap never inherits the environment's near-start escape allowance. Inside the self margin the same rule
    // as for the world holds: a start already that close may move out, never deeper (setStart) - its gap there is
    // negative, so the bound is the start's less 2 mm, as the world's; 0.9 of it lay above the start itself, and a line
    // from a pose 4.8 mm inside a 5 mm margin was refused where it stood.
    const Real self = selfCollision_->clearance(q);
    if (!(self > 0) && !(startSelf_ < 0 && start_.size() == q.size() && span(q, start_) < 0.3_r && self >= startSelf_ - 0.002_r &&
                         selfOverlap_->clearance(q) > 0)) {
        if (witness) *witness = self;
        return false;
    }
    Real least = options_.margin;
    if (startClearance_ < options_.margin && start_.size() == q.size() && span(q, start_) < 0.3_r)
        least = startClearance_ > 0 ? 0.9_r * startClearance_ : startClearance_ - 0.002_r;
    std::vector<ArmPose> links;
    armPoses(model_, q, links);
    Real minimum = kInf;
    for (size_t i = 0; i < links.size(); ++i)
        for (const ArmSphere& sphere : model_.joints[i].spheres) {
            const Vector3 centre = links[i].position + links[i].turn.rotate(sphere.centre);
            const Real value = clearance_(centre) - sphere.radius;
            if (!std::isfinite(value)) return false;
            minimum = std::min(minimum, value);
            if (!(value > least)) {
                if (witness) *witness = value;
                return false;
            }
        }
    if (witness) *witness = minimum;
    // How far any point of the arm may move before it can come within the margins: the world's distance is a
    // distance (1-Lipschitz) read trilinearly between voxel centres (each partial derivative at most 1: sqrt 3 in all),
    // two of the arm's own bodies close at most twice as fast as either moves. The near-start allowance is no rule
    // that holds along a move: none is claimed under it.
    if (room && least == options_.margin && self > 0) *room = std::min((minimum - least) / 1.7320508_r, self / 2);
    return true;
}

void ArmPlanner::setStart(const std::vector<Real>& start) const {
    start_ = start, startClearance_ = clearance(start), startSelf_ = selfCollision_->clearance(start);
}

// The stretch's checks are those every `resolution` of the farthest sphere's travel, but a check that finds room lets
// the next ones be skipped while no point of the arm can have crossed it - conservative advancement (Schwarzer, Saha &
// Latombe 2005, "Adaptive dynamic collision checking for single and multiple articulated robots in complex
// environments", IEEE T-RO 21(3)): the same answer, fewer checks where the arm passes far from everything.
bool ArmPlanner::freeStretch(const std::vector<Real>& a, const std::vector<Real>& b) const {
    const int steps = std::max(1, int(std::ceil(span(a, b) * reach_ / options_.resolution)));
    Real travel = 0; // the farthest any point of the arm moves from a to b
    for (size_t i = 0; i < a.size() && i < lever_.size(); ++i) travel += std::fabs(b[i] - a[i]) * lever_[i];
    Config q(a.size());
    for (int k = 1; k <= steps;) {
        const Real t = Real(k) / Real(steps);
        for (size_t i = 0; i < q.size(); ++i) q[i] = a[i] + (b[i] - a[i]) * t;
        Real room = 0;
        if (!free(q, nullptr, &room)) return false;
        // Checks k+1 .. k+n lie within the room when n / steps * travel <= room: free by it, not asked again.
        const Real covered = travel > 0 ? room / travel * Real(steps) : Real(steps);
        k += 1 + int(std::min(covered, Real(steps)));
        if (k > steps && t < 1) k = steps; // the stretch's end is always checked
    }
    return true;
}

bool ArmPlanner::plan(const std::vector<Real>& start, const std::vector<std::vector<Real>>& goals, std::vector<std::vector<Real>>& way,
                      int* grown) const {
    way.clear();
    timing_ = Timing();
    setStart(start);
    if (!(startClearance_ > -0.02_r)) return false; // 2 cm deep in something: no way out of it is this planner's
    Tree from{{start}, {-1}}, to;
    for (const Config& g : goals)
        if (free(g)) to.nodes.push_back(g), to.parent.push_back(-1);
    if (!free(start) || to.nodes.empty()) return false;
    // RRT-Connect's local connector is sufficient when a goal has a clear direct
    // edge. Do not let clearance optimization turn this shortest joint-space
    // segment into a detour merely to pursue the optional 'wanted' clearance.
    const Config* direct = nullptr;
    Real distance = kInf;
    for (const Config& goal : to.nodes)
        if (span(start, goal) < distance && freeStretch(start, goal)) {
            direct = &goal;
            distance = span(start, goal);
        }
    if (direct) {
        way = {start, *direct};
        if (grown) *grown = 0;
        return true;
    }
    Random r(options_.seed);
    Tree *a = &from, *b = &to;
    const auto grow = [&](Tree& t, const Config& q, int near) { // one step from `near` toward q: the new node, or -1
        const Config next = toward(t.nodes[size_t(near)], q, options_.step);
        if (!freeStretch(t.nodes[size_t(near)], next)) return -1;
        t.nodes.push_back(next), t.parent.push_back(near);
        return int(t.nodes.size()) - 1;
    };
    for (int it = 0; it < options_.iterations; ++it) {
        Config q(start.size());
        for (size_t i = 0; i < q.size(); ++i) q[i] = r.uniform(model_.joints[i].lower, model_.joints[i].upper); // a draw a statement
        const int added = grow(*a, q, nearest(*a, q));
        if (added >= 0) {
            const Config& target = a->nodes[size_t(added)];
            int last = nearest(*b, target);
            while (last >= 0 && span(b->nodes[size_t(last)], target) > 0) last = grow(*b, target, last); // connect: on till reached or stuck
            if (last >= 0) {
                std::vector<Config> head = chainTo(from, a == &from ? added : last), tail = chainTo(to, a == &from ? last : added);
                way = head;
                for (size_t k = tail.size() - 1; k-- > 0;) way.push_back(tail[k]); // the meeting point once
                if (grown) *grown = int(from.nodes.size() + to.nodes.size());
                const auto found = std::chrono::steady_clock::now();
                shorten(way);
                const auto shortened = std::chrono::steady_clock::now();
                optimize(way);
                timing_.shortenMs = std::chrono::duration<double, std::milli>(shortened - found).count();
                timing_.optimizeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - shortened).count();
                return true;
            }
        }
        std::swap(a, b);
    }
    if (grown) *grown = int(from.nodes.size() + to.nodes.size());
    return false;
}

Real ArmPlanner::nearness(const std::vector<Real>& q, std::vector<Real>* gradient) const {
    std::vector<ArmPose> links;
    armPoses(model_, q, links);
    if (gradient) gradient->assign(q.size(), 0);
    std::vector<Vector3> axes, pivots;
    if (gradient && gradient_) { axes.resize(q.size()); pivots.resize(q.size()); }
    for (size_t i = 0; i < axes.size(); ++i) {
        const auto& joint = model_.joints[i];
        const auto& parent = joint.parent < 0 ? model_.base : links[size_t(joint.parent)];
        axes[i] = (parent.turn * joint.turnB).rotate({1, 0, 0});
        pivots[i] = parent.position + parent.turn.rotate(joint.originB);
    }
    Real sum = 0;
    for (size_t i = 0; i < links.size(); ++i)
        for (const ArmSphere& s : model_.joints[i].spheres) {
            const Vector3 point = links[i].position + links[i].turn.rotate(s.centre);
            Vector3 normal;
            const Real distance = gradient && gradient_ ? gradient_(point, normal) : clearance_(point);
            const Real short_ = options_.wanted - (distance - s.radius);
            if (short_ <= 0) continue;
            sum += short_ * short_;
            // Revolute Jacobian: dp/dq = axis x (p - pivot), Buss, 2009 revision, section 2, p. 5.
            // https://people.csail.mit.edu/jbarry/spring2011PR2/readings/jactrans.pdf
            if (gradient && gradient_) for (int joint = int(i); joint >= 0; joint = model_.joints[size_t(joint)].parent)
                (*gradient)[size_t(joint)] -= 2 * short_ * dot(normal, cross(axes[size_t(joint)], point - pivots[size_t(joint)]));
        }
    if (gradient && !gradient_) for (size_t i = 0; i < q.size(); ++i) {
        auto moved = q; moved[i] += 1e-3_r;
        (*gradient)[i] = (nearness(moved) - sum) / 1e-3_r;
    }
    return sum;
}

namespace {

// The way with about n points along its length in the joints, its corners kept among them (points evenly between two
// corners would cut a corner - a stretch RRT never checked, through the post it went round).
std::vector<Config> resample(const std::vector<Config>& way, int n) {
    std::vector<Real> step;
    Real total = 0;
    for (size_t k = 1; k < way.size(); ++k) {
        Real d = 0;
        for (size_t i = 0; i < way[k].size(); ++i) d += sqr(way[k][i] - way[k - 1][i]);
        step.push_back(std::sqrt(d)), total += step.back();
    }
    std::vector<Config> out{way[0]};
    for (size_t k = 1; k < way.size(); ++k) {
        const int parts = std::max(1, int(std::lround(Real(n - 1) * step[k - 1] / std::max(total, 1e-9_r))));
        for (int j = 1; j <= parts; ++j) {
            Config q(way[k].size());
            for (size_t i = 0; i < q.size(); ++i) q[i] = way[k - 1][i] + (way[k][i] - way[k - 1][i]) * (Real(j) / Real(parts));
            out.push_back(q);
        }
    }
    return out;
}

} // namespace

// The weight of the arm's room against the way's smoothness in the action: at 3 and at 30 the smoothness pulled the way
// onto the margin's edge by the post it went round (its closest 1.7 cm, RRT's 2.5).
constexpr Real kRoom = 300;

// Rounds after the optimisation's whose step is the gradient's own, not its direction's: a step of 0.02 rad whatever
// the gradient overshoots the least and leaves a saw of about its size (2.7 mm at a UR5e's hand, 187 reversals of a
// joint in 708 steps, arm-goal-planner-20261108); a step of half the gradient - with nothing near, each point to the
// middle of its neighbours - damps such short waves within a few sweeps as a Gauss-Seidel smoother does (Briggs,
// Henson & McCormick 2000, "A Multigrid Tutorial", ch. 2), while the long ones the fixed steps shaped stay. Half as
// many as the rounds asked for: an executor that asks for few to plan fast (ArmControlPlan.cpp: 5) gets few more.
// Not done: a long detour is not straightened (testArmPlannerOptimizesStraight). CHOMP's covariant step through the
// smoothness metric's inverse (Ratliff et al. 2009) puts a free way on the line in one step but, in the rounds an
// executor can spend, pulled the way round a post to 2.4 cm of it against 4.8 cm here (arm-chunked-replanning-20261108).

void ArmPlanner::optimize(std::vector<std::vector<Real>>& way) const {
    if (options_.optimize <= 0 || way.size() < 2) return;
    std::vector<Config> q = resample(way, 120); // ~0.05 rad apart: a stretch between two free points seldom cuts anything
    if (q.size() < 3) return;
    const size_t n = q[0].size();
    const int settle = (options_.optimize + 1) / 2;
    for (int round = 0; round < options_.optimize + settle; ++round)
        for (size_t k = 1; k + 1 < q.size(); ++k) {
            Config g(n);
            Config obstacleGradient;
            const Real here = gradient_ ? nearness(q[k], &obstacleGradient) : nearness(q[k]);
            Real largest = 0;
            for (size_t i = 0; i < n; ++i) { // d(action)/dq: the smoothness term exactly, the arm's by a difference
                Real obstacle = 0;
                if (gradient_) obstacle = kRoom * obstacleGradient[i];
                else { // Preserve the legacy finite-difference arithmetic order.
                    Config moved = q[k]; moved[i] += 1e-3_r;
                    obstacle = kRoom * (nearness(moved) - here) / 1e-3_r;
                }
                g[i] = 2 * q[k][i] - q[k - 1][i] - q[k + 1][i] + obstacle;
                largest = std::max(largest, std::fabs(g[i]));
            }
            if (largest < 1e-9_r) continue;
            Config next = q[k];
            // A step of at most 0.02 rad down the gradient, never into something; the settling rounds' no more than half of it.
            const Real scale = round < options_.optimize ? 0.02_r / largest : std::min(0.5_r, 0.02_r / largest);
            for (size_t i = 0; i < n; ++i)
                next[i] = std::clamp(q[k][i] - scale * g[i], model_.joints[i].lower, model_.joints[i].upper);
            if (free(next)) q[k] = next;
        }
    for (size_t k = 1; k < q.size(); ++k)
        if (!freeStretch(q[k - 1], q[k])) return; // not free all along: RRT's way stays
    way = q;
}

void ArmPlanner::shorten(std::vector<std::vector<Real>>& way) const {
    Random r(options_.seed + 1);
    std::set<std::pair<Config, Config>> blocked; // Immutable field: deleting corners cannot unblock an unchanged edge.
    for (int s = 0; s < options_.shortcuts && way.size() > 2; ++s) {
        size_t i = size_t(r.nextBits() % uint32_t(way.size()));
        size_t j = size_t(r.nextBits() % uint32_t(way.size()));
        if (i > j) std::swap(i, j);
        if (j - i < 2) continue;
        const auto edge = std::make_pair(way[i], way[j]);
        if (blocked.count(edge)) continue;
        if (!freeStretch(way[i], way[j])) { blocked.insert(edge); continue; }
        way.erase(way.begin() + std::ptrdiff_t(i + 1), way.begin() + std::ptrdiff_t(j));
    }
}

} // namespace ke
