// Math and spatial structures against exact answers: vector/matrix/quaternion identities, mesh
// primitives (closed manifolds, volumes) and BVH queries against brute force.
#include "TestRunner.h"

#include "core/Mesh.h"
#include "spatial/BVH.h"

#include <random>

void testMath() {
    auto near = [](Real a, Real b, Real eps = 1e-5_r) { return std::fabs(a - b) <= eps; };
    // Vector2 / Vector3
    CHECK(near(cross(Vector2(1, 0), Vector2(0, 1)), 1.0_r) && near(dot(perp(Vector2(2, 3)), Vector2(2, 3)), 0.0_r), "Vector2");
    Vector3 c = cross(Vector3(1, 0, 0), Vector3(0, 1, 0));
    CHECK(near(c.x, 0) && near(c.y, 0) && near(c.z, 1), "Vector3 cross");
    Vector3 p = anyPerpendicular(Vector3(0.3_r, -2.0_r, 0.7_r));
    CHECK(near(dot(p, Vector3(0.3_r, -2.0_r, 0.7_r)), 0.0_r) && near(length(p), 1.0_r), "anyPerpendicular");

    // Matrix3x3: inverse, determinant, skew, eigen
    Matrix3x3 A = Matrix3x3::fromRows({4, 1, 2}, {0, 3, 1}, {1, 0, 2});
    Matrix3x3 I = A * A.inverse();
    Real err = 0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) err = std::max(err, std::fabs(I.m[i][j] - (i == j ? 1.0_r : 0.0_r)));
    CHECK(err < 1e-5_r && near(A.determinant(), 4 * 6 - 1 * (-1) + 2 * (-3)), "Matrix3x3 inverse/det (err %e, det %f)", err,
          A.determinant());
    Vector3 s = Matrix3x3::skew({1, 2, 3}) * Vector3(4, 5, 6), sc = cross(Vector3(1, 2, 3), Vector3(4, 5, 6));
    CHECK(near(s.x, sc.x) && near(s.y, sc.y) && near(s.z, sc.z), "skew * v == cross");
    CHECK(Matrix3x3::zero().inverse().m[0][0] == 0.0_r, "singular inverse -> zero");
    Matrix3x3 S = Matrix3x3::fromRows({2, 1, 0}, {1, 3, 1}, {0, 1, 4}), V;
    Vector3 ev;
    symmetricEigen(S, ev, V);
    Matrix3x3 R = V * Matrix3x3::diag(ev) * V.transposed();
    err = 0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) err = std::max(err, std::fabs(R.m[i][j] - S.m[i][j]));
    CHECK(err < 1e-5_r, "3x3 eigen reconstruction error %e", err);

    // Quaternion: matrix round trip, log/exp, slerp, shortest arc
    Quaternion q = Quaternion::fromAxisAngle({1, 2, 3}, 1.1_r);
    Quaternion q2 = Quaternion::fromMatrix3x3(q.toMatrix3x3());
    CHECK(near(std::fabs(dot(q, q2)), 1.0_r), "quaternion <-> matrix");
    Quaternion q3 = Quaternion::fromRotationVector(q.log());
    CHECK(near(std::fabs(dot(q, q3)), 1.0_r), "exp(log(q)) == q");
    CHECK(near(q.angle(), 1.1_r), "angle %f", q.angle());
    const Quaternion back = Quaternion::fromAxisAngle(q.axis(), q.angle()), minus{-q.w, -q.x, -q.y, -q.z};
    CHECK(near(std::fabs(dot(q, back)), 1.0_r) && near(std::fabs(dot(minus, Quaternion::fromAxisAngle(minus.axis(), minus.angle()))), 1.0_r) &&
              near(q.axis().y, 2.0_r / std::sqrt(14.0_r)),
          "axis() and angle() give the turn back, for q and -q");
    Quaternion h = slerp(Quaternion(), Quaternion::fromAxisAngle({0, 0, 1}, 1.0_r), 0.5_r);
    CHECK(near(h.angle(), 0.5_r), "slerp halfway angle %f", h.angle());
    Vector3 to = Quaternion::fromTwoVectors({1, 0, 0}, {0, 0, -1}).rotate({1, 0, 0});
    CHECK(near(to.z, -1.0_r) && near(Quaternion::fromTwoVectors({1, 0, 0}, {-1, 0, 0}).rotate({1, 0, 0}).x, -1.0_r),
          "fromTwoVectors");
    Quaternion qi = q * q.inverse();
    CHECK(near(qi.w, 1.0_r) && near(qi.x, 0.0_r), "q * q^-1 == 1");
}

void testPrimitives() {
    TriMesh c = primitives::cylinder(0.5_r, 2.0_r, 96);
    Real v = c.signedVolume(), ve = kPi * 0.25_r * 2.0_r;
    CHECK(std::fabs(v - ve) / ve < 0.01_r, "cylinder volume %f vs %f", v, ve);
    TriMesh s = primitives::sphere(1.0_r, 96, 48);
    Real vs = s.signedVolume(), vse = 4.0_r / 3.0_r * kPi;
    CHECK(std::fabs(vs - vse) / vse < 0.01_r, "sphere volume %f vs %f", vs, vse);
    TriMesh b = primitives::box({0.5_r, 1.0_r, 1.5_r});
    CHECK(std::fabs(b.signedVolume() - 6.0_r) < 1e-4_r, "box volume %f", b.signedVolume());
    // Welding a triangle soup restores shared vertices.
    TriMesh soup;
    for (auto t : b.triangles) {
        uint32_t base = uint32_t(soup.positions.size());
        for (int k = 0; k < 3; ++k) soup.positions.push_back(b.positions[t[k]]);
        soup.triangles.push_back({base, base + 1, base + 2});
    }
    soup.weld(1e-5_r);
    CHECK(soup.positions.size() == 8, "welded vertex count %zu", soup.positions.size());
}

void testBVH() {
    TriMesh m = primitives::sphere(0.7_r, 64, 32);
    Matrix3x3 R = Quaternion::fromEuler(0.3_r, 0.2_r, 0.1_r).toMatrix3x3();
    m.transform(R, {1.0_r, 0.6_r, 0.8_r}, {0.1_r, -0.2_r, 0.3_r});
    MeshBVH bvh;
    bvh.build(m);
    std::mt19937 rng(1);
    std::uniform_real_distribution<Real> U(-1.2_r, 1.2_r);
    int mismatch = 0, signErr = 0;
    for (int n = 0; n < 2000; ++n) {
        Vector3 p(U(rng), U(rng), U(rng));
        ClosestHit h;
        bool ok = bvh.closestPoint(p, kInf, h);
        // brute force
        Real best = kInf;
        for (size_t t = 0; t < m.triangles.size(); ++t) {
            const auto& tri = m.triangles[t];
            // sample-free exact distance through a tiny BVH-free routine: reuse MeshBVH on single tri is
            // overkill; use a dense barycentric search instead.
            Vector3 a = m.positions[tri[0]], b = m.positions[tri[1]], c = m.positions[tri[2]];
            for (int i = 0; i <= 8; ++i)
                for (int j = 0; i + j <= 8; ++j) {
                    Vector3 q = a + (b - a) * (i / 8.0_r) + (c - a) * (j / 8.0_r);
                    best = std::min(best, length(p - q));
                }
        }
        if (!ok || h.distance > best + 1e-5_r || h.distance < best - 0.02_r) ++mismatch;
        // inside test vs ellipsoid equation (approximate near the surface)
        Vector3 l = R.transposed() * (p - Vector3(0.1_r, -0.2_r, 0.3_r));
        Real e = sqr(l.x / 0.7_r) + sqr(l.y / (0.7_r * 0.6_r)) + sqr(l.z / (0.7_r * 0.8_r));
        if (std::fabs(e - 1.0_r) > 0.05_r && ((e < 1.0_r) != (h.signedDistance < 0))) ++signErr;
    }
    CHECK(mismatch == 0, "closest point mismatches: %d", mismatch);
    CHECK(signErr == 0, "inside/outside errors: %d", signErr);

    RayHit hit;
    TriMesh sp = primitives::sphere(1.0_r, 128, 64);
    MeshBVH sb;
    sb.build(sp);
    bool ok = sb.raycast({-5, 0.01_r, 0.02_r}, {1, 0, 0}, 100, hit);
    CHECK(ok && std::fabs(hit.t - 4.0_r) < 0.01_r, "ray t=%f", hit.t);
}
