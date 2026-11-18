#pragma once
// Immutable own-body geometry index. Queries have private pose scratch storage,
// allowing reuse across planner poses and concurrent readers without stale bounds.
#include "robot/ArmPlanner.h"
#include <memory>
#include <utility>

namespace ke {
class ArmSelfCollisionIndex {
public:
    // margin: the gap asked of every checked pair but the model's near pairs and two bodies on one link, which must only
    // not overlap; clearance is then the least gap less what its pair is asked for.
    explicit ArmSelfCollisionIndex(const ArmModel& model, Real margin = 0);
    // Each checked pair's own gap at q (the pairs in the index's order, `pairs` their bodies); false: invalid pose.
    bool pairGaps(const std::vector<Real>& q, std::vector<Real>& gaps, std::vector<std::pair<size_t, size_t>>& pairs) const;
    // nearest: the two collision bodies (ArmModel::collisionBodies indices) the least gap is between, when it is finite.
    Real clearance(const std::vector<Real>& q, std::pair<size_t, size_t>* nearest = nullptr) const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
};
}
