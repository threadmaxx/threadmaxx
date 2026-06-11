#pragma once

// Tiny test-only helpers for picking tests. Constructs column-major
// matrices used by Camera in `threadmaxx::input::picking`.

#include <cmath>

#include "threadmaxx_input/picking.hpp"

namespace threadmaxx::input::test {

inline void identity4(float m[16]) noexcept {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// Vulkan-style perspective: column-major, NDC z ∈ [0, 1], m22 = f/(n-f).
// X factor uses aspect = w/h. Y factor is positive (we let the host pick
// the sign convention via the view matrix).
inline void perspectiveVulkan(float fovY, float aspect, float n, float f,
                              float m[16]) noexcept {
    const float t = std::tan(fovY * 0.5f);
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0]  = 1.0f / (aspect * t);
    m[5]  = 1.0f / t;
    m[10] = f / (n - f);
    m[14] = (f * n) / (n - f);  // column-major: row=2, col=3 → index 14
    m[11] = -1.0f;              // column-major: row=3, col=2 → index 11
}

// Look-at viewing matrix (right-handed, +Y up). Column-major.
inline void lookAt(const float eye[3], const float target[3], const float up[3],
                   float m[16]) noexcept {
    const float fx = target[0] - eye[0];
    const float fy = target[1] - eye[1];
    const float fz = target[2] - eye[2];
    const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    const float nfx = fx / fl, nfy = fy / fl, nfz = fz / fl;

    const float rxRaw = nfy * up[2] - nfz * up[1];
    const float ryRaw = nfz * up[0] - nfx * up[2];
    const float rzRaw = nfx * up[1] - nfy * up[0];
    const float rl = std::sqrt(rxRaw * rxRaw + ryRaw * ryRaw + rzRaw * rzRaw);
    const float rx = rxRaw / rl, ry = ryRaw / rl, rz = rzRaw / rl;

    const float ux = ry * nfz - rz * nfy;
    const float uy = rz * nfx - rx * nfz;
    const float uz = rx * nfy - ry * nfx;

    // column 0
    m[0] = rx;  m[1] = ux;  m[2]  = -nfx; m[3]  = 0.0f;
    // column 1
    m[4] = ry;  m[5] = uy;  m[6]  = -nfy; m[7]  = 0.0f;
    // column 2
    m[8] = rz;  m[9] = uz;  m[10] = -nfz; m[11] = 0.0f;
    // column 3
    m[12] = -(rx * eye[0] + ry * eye[1] + rz * eye[2]);
    m[13] = -(ux * eye[0] + uy * eye[1] + uz * eye[2]);
    m[14] =   nfx * eye[0] + nfy * eye[1] + nfz * eye[2];
    m[15] = 1.0f;
}

inline bool approx(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

}  // namespace threadmaxx::input::test
