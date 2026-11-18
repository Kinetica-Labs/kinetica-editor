// Explicit own-body collision checks, independent of uncertain external perception.
// Sphere separation is |ca-cb|-ra-rb; only authored body-pair permissions suppress a check.
#include "robot/ArmSelfCollision.h"
#include "math/AABB.h"
#include "core/Random.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ke {
namespace {

bool finitePose(const ArmPose& pose) {
    const auto& p = pose.position;
    const auto& r = pose.turn;
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
           std::isfinite(r.w) && std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.z);
}

bool worldBodies(const ArmModel& model, const std::vector<Real>& q, std::vector<std::vector<ArmSphere>>& out) {
    if (q.size() != model.joints.size() || !finitePose(model.base)) return false;
    for (size_t i = 0; i < q.size(); ++i) {
        if (!std::isfinite(q[i]) || model.joints[i].parent < -1 || model.joints[i].parent >= int(i)) return false;
    }
    std::vector<ArmPose> poses;
    armPoses(model, q, poses);
    for (const auto& body : model.collisionBodies) {
        if (body.link < -1 || body.link >= int(poses.size()) || body.spheres.empty()) return false;
        const auto& pose = body.link < 0 ? model.base : poses[size_t(body.link)];
        if (!finitePose(pose)) return false;
        out.emplace_back();
        for (const auto& sphere : body.spheres) {
            const Vector3 p = pose.position + pose.turn.rotate(sphere.centre);
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
                !std::isfinite(sphere.radius) || sphere.radius < 0) return false;
            out.back().push_back({p, sphere.radius});
        }
    }
    return true;
}

// Median BVH over sphere centres; nodes retain the greatest radius in their subtree.
// dist(centre boxes)-maxRadiusA-maxRadiusB is a lower bound even for intersecting spheres.
class SphereTree {
    struct Node {
        AABB bounds;
        Real radius = 0, scale = 1;
        size_t begin = 0, end = 0;
        int left = -1, right = -1;
    };
    std::vector<ArmSphere> spheres_;
    std::vector<Node> nodes_;

    int build(size_t begin, size_t end) {
        const int index = int(nodes_.size());
        nodes_.emplace_back();
        Node node;
        node.begin = begin; node.end = end;
        for (size_t i = begin; i < end; ++i) {
            const auto& s = spheres_[i];
            node.bounds.expand(s.centre); node.radius = std::max(node.radius, s.radius);
            node.scale = std::max({node.scale, std::fabs(s.centre.x), std::fabs(s.centre.y), std::fabs(s.centre.z), s.radius});
        }
        if (end - begin > 8) {
            const auto extent = node.bounds.extent();
            int axis = extent.y > extent.x ? 1 : 0;
            if (extent.z > extent[axis]) axis = 2;
            const size_t middle = begin + (end - begin) / 2;
            std::nth_element(spheres_.begin() + begin, spheres_.begin() + middle, spheres_.begin() + end,
                             [axis](const ArmSphere& a, const ArmSphere& b) { return a.centre[axis] < b.centre[axis]; });
            node.left = build(begin, middle); node.right = build(middle, end);
        }
        nodes_[size_t(index)] = node;
        return index;
    }

    static Real bound(const Node& a, const Node& b) {
        const Vector3 d = vmax(vmax(a.bounds.lo - b.bounds.hi, b.bounds.lo - a.bounds.hi), Vector3(0.0_r));
        const Real value = length(d) - a.radius - b.radius;
        // Conservative roundoff guard: ambiguous bounds descend rather than authorize separation.
        return value - 64 * std::numeric_limits<Real>::epsilon() * (a.scale + b.scale + std::fabs(value));
    }

    void visit(int ai, const SphereTree& other, int bi, Real& least) const {
        const Node& a = nodes_[size_t(ai)]; const Node& b = other.nodes_[size_t(bi)];
        if (bound(a, b) >= least) return;
        if (a.left < 0 && b.left < 0) {
            for (size_t i = a.begin; i < a.end; ++i) for (size_t j = b.begin; j < b.end; ++j) {
                const auto& sa = spheres_[i]; const auto& sb = other.spheres_[j];
                const Real gap = length(sa.centre - sb.centre) - sa.radius - sb.radius;
                if (!std::isfinite(gap)) { least = -kInf; return; }
                least = std::min(least, gap);
            }
            return;
        }
        if (b.left < 0 || (a.left >= 0 && a.end - a.begin >= b.end - b.begin)) {
            const bool leftFirst = bound(nodes_[size_t(a.left)], b) <= bound(nodes_[size_t(a.right)], b);
            visit(leftFirst ? a.left : a.right, other, bi, least);
            visit(leftFirst ? a.right : a.left, other, bi, least);
        } else {
            const bool leftFirst = bound(a, other.nodes_[size_t(b.left)]) <= bound(a, other.nodes_[size_t(b.right)]);
            visit(ai, other, leftFirst ? b.left : b.right, least);
            visit(ai, other, leftFirst ? b.right : b.left, least);
        }
    }

public:
    explicit SphereTree(std::vector<ArmSphere> spheres) : spheres_(std::move(spheres)) {
        nodes_.reserve(spheres_.size() * 2); build(0, spheres_.size());
    }
    void reduce(const SphereTree& other, Real& least) const { visit(0, other, 0, least); }
    bool pose(const ArmPose& at) {
        if (!finitePose(at)) return false;
        for (auto& sphere : spheres_) {
            sphere.centre = at.position + at.turn.rotate(sphere.centre);
            const auto& p = sphere.centre;
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
        }
        // Children follow parents in build order; reverse order refits bottom-up.
        for (auto it = nodes_.rbegin(); it != nodes_.rend(); ++it) {
            Node& node = *it;
            node.bounds = AABB(); node.scale = std::max(1.0_r, node.radius);
            if (node.left < 0) {
                for (size_t i = node.begin; i < node.end; ++i) {
                    const auto& p = spheres_[i].centre;
                    node.bounds.expand(p);
                    node.scale = std::max({node.scale, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)});
                }
            } else {
                const auto& a = nodes_[size_t(node.left)]; const auto& b = nodes_[size_t(node.right)];
                node.bounds.expand(a.bounds); node.bounds.expand(b.bounds);
                node.scale = std::max(a.scale, b.scale);
            }
        }
        return true;
    }
};

} // namespace

struct ArmSelfCollisionIndex::Impl {
    ArmModel model;
    std::vector<SphereTree> local;
    std::vector<std::pair<size_t, size_t>> checkedPairs;
    std::vector<Real> asked; // each checked pair's required gap: the margin, or 0 for near pairs and one link's bodies
    bool valid = false;
    Impl(const ArmModel& source, Real margin) : model(source) {
        const size_t count = model.collisionBodies.size();
        for (const auto& pair : model.allowedCollisionPairs)
            if (pair.first >= count || pair.second >= count || pair.first == pair.second) return;
        if (count == 0) { valid = true; return; }
        std::vector<std::vector<ArmSphere>> validation;
        if (!worldBodies(model, std::vector<Real>(model.joints.size(), 0.0_r), validation)) return;
        for (const auto& body : model.collisionBodies) local.emplace_back(body.spheres);
        for (size_t i = 0; i < count; ++i) for (size_t j = i + 1; j < count; ++j) {
            const auto allowed = [&](const std::pair<size_t, size_t>& p) {
                return (p.first == i && p.second == j) || (p.first == j && p.second == i);
            };
            if (std::any_of(model.allowedCollisionPairs.begin(), model.allowedCollisionPairs.end(), allowed)) continue;
            checkedPairs.push_back({i, j});
            const bool near = model.collisionBodies[i].link == model.collisionBodies[j].link ||
                              std::any_of(model.nearPairs.begin(), model.nearPairs.end(), allowed);
            asked.push_back(near ? 0 : margin);
        }
        valid = true;
    }
    // The checked bodies posed at q (false: an invalid pose).
    bool posed(const std::vector<Real>& q, std::vector<SphereTree>& trees) const {
        if (q.size() != model.joints.size()) return false;
        for (Real angle : q) if (!std::isfinite(angle)) return false;
        std::vector<ArmPose> poses;
        armPoses(model, q, poses);
        trees = local;
        for (size_t i = 0; i < trees.size(); ++i) {
            const int link = model.collisionBodies[i].link;
            if (!trees[i].pose(link < 0 ? model.base : poses[size_t(link)])) return false;
        }
        return true;
    }
};

ArmSelfCollisionIndex::ArmSelfCollisionIndex(const ArmModel& model, Real margin) : impl_(std::make_shared<Impl>(model, margin)) {}

Real ArmSelfCollisionIndex::clearance(const std::vector<Real>& q, std::pair<size_t, size_t>* nearest) const {
    if (!impl_->valid) return -kInf;
    if (impl_->local.empty()) return kInf;
    std::vector<SphereTree> trees;
    if (!impl_->posed(q, trees)) return -kInf;
    Real least = kInf;
    for (size_t k = 0; k < impl_->checkedPairs.size(); ++k) {
        const auto& pair = impl_->checkedPairs[k];
        Real gap = least + impl_->asked[k]; // what would beat the least so far, the pair's own ask added back
        trees[pair.first].reduce(trees[pair.second], gap);
        if (gap - impl_->asked[k] < least) {
            least = gap - impl_->asked[k];
            if (nearest) *nearest = pair;
        }
    }
    return least;
}

bool ArmSelfCollisionIndex::pairGaps(const std::vector<Real>& q, std::vector<Real>& gaps, std::vector<std::pair<size_t, size_t>>& pairs) const {
    std::vector<SphereTree> trees;
    if (!impl_->valid || !impl_->posed(q, trees)) return false;
    gaps.assign(impl_->checkedPairs.size(), kInf), pairs = impl_->checkedPairs;
    for (size_t k = 0; k < pairs.size(); ++k) trees[pairs[k].first].reduce(trees[pairs[k].second], gaps[k]);
    return true;
}

Real armSelfClearance(const ArmModel& model, const std::vector<Real>& q, std::pair<size_t, size_t>* nearest) {
    return ArmSelfCollisionIndex(model).clearance(q, nearest);
}

// The MoveIt Setup Assistant's sampling of a default collision matrix (moveit_setup_srdf_plugins, compute_default_collisions:
// random states, pairs counted by how often they collide), asked of a margin instead of contact: a pair closer than the
// margin in most coordinates is a built-in neighbour - the UR5e's upper arm passes its base a few millimetres off in every
// pose - and asking it for the margin would forbid the whole workspace.
std::vector<std::pair<size_t, size_t>> armNearPairs(const ArmModel& model, Real margin, int samples, uint32_t seed) {
    ArmModel plain = model;
    plain.nearPairs.clear();
    const ArmSelfCollisionIndex index(plain);
    Random random(seed);
    std::vector<int> close;
    std::vector<Real> gaps;
    std::vector<std::pair<size_t, size_t>> pairs;
    int counted = 0;
    for (int n = 0; n < samples; ++n) {
        std::vector<Real> q(model.joints.size());
        for (size_t k = 0; k < q.size(); ++k) q[k] = random.uniform(model.joints[k].lower, model.joints[k].upper);
        if (!index.pairGaps(q, gaps, pairs)) continue;
        close.resize(gaps.size(), 0), ++counted;
        for (size_t k = 0; k < gaps.size(); ++k) close[k] += gaps[k] < margin ? 1 : 0;
    }
    std::vector<std::pair<size_t, size_t>> near;
    for (size_t k = 0; k < close.size(); ++k)
        if (2 * close[k] > counted) near.push_back(pairs[k]);
    return near;
}

} // namespace ke
