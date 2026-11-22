// The coordinates of a joint's frames (JointModel.h): travel along frame b's axes, then the twist
// and swing of frame a relative to frame b.
#include "scene/JointModel.h"

#include "math/ElementaryFunctions.h"

namespace ke {

void frameCoordinates(const Vector3& oA, const Quaternion& QA, const Vector3& oB, const Quaternion& QB, Real out[6]) {
    const Vector3 axes[3] = {Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1)};
    for (int k = 0; k < 3; ++k) out[k] = dot(oA - oB, QB.rotate(axes[k]));
    Quaternion q = (QB.conjugate() * QA).normalized();
    if (q.w < 0) q = Quaternion{-q.w, -q.x, -q.y, -q.z};
    const Real n = ke::hypot(q.w, q.x);
    const Quaternion twist = n > 1e-9_r ? Quaternion{q.w / n, q.x / n, 0, 0} : Quaternion();
    const Quaternion swing = q * twist.conjugate();
    out[DofRx] = 2 * ke::atan2(q.x, q.w);
    out[DofRy] = 2 * ke::atan2(swing.y, swing.w);
    out[DofRz] = 2 * ke::atan2(swing.z, swing.w);
}

} // namespace ke
