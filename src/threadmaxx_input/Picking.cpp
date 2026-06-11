#include "threadmaxx_input/picking.hpp"

#include <cmath>

namespace threadmaxx::input {

namespace {

struct Mat4 {
    float m[16]{};  // column-major: m[col * 4 + row]

    float& at(int row, int col) noexcept { return m[col * 4 + row]; }
    float at(int row, int col) const noexcept { return m[col * 4 + row]; }
};

Mat4 multiply(const Mat4& a, const Mat4& b) noexcept {
    Mat4 r{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) {
                s += a.at(row, k) * b.at(k, col);
            }
            r.at(row, col) = s;
        }
    }
    return r;
}

// Standard cofactor-based 4x4 inverse. Returns false + leaves `out`
// untouched on singular input.
bool inverse4(const Mat4& a, Mat4& out) noexcept {
    const float* m = a.m;
    float inv[16];

    inv[0] =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15]
            + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15]
            - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] =  m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15]
            + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14]
            - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];

    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15]
            - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15]
            + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15]
            - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14]
            + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];

    inv[2] =  m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15]
            + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15]
            - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15]
            + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14]
            - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];

    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11]
            - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] =  m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11]
            + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11]
            - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10]
            + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];

    const float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (std::fabs(det) < 1e-12f) return false;
    const float invDet = 1.0f / det;
    for (int i = 0; i < 16; ++i) out.m[i] = inv[i] * invDet;
    return true;
}

// Transforms a 4D vector (x, y, z, w) by `a`. Column-major convention:
// result = a * v.
void transform4(const Mat4& a, float x, float y, float z, float w,
                float out[4]) noexcept {
    for (int row = 0; row < 4; ++row) {
        out[row] = a.at(row, 0) * x + a.at(row, 1) * y +
                   a.at(row, 2) * z + a.at(row, 3) * w;
    }
}

Mat4 fromCamera(const float src[16]) noexcept {
    Mat4 m;
    for (int i = 0; i < 16; ++i) m.m[i] = src[i];
    return m;
}

}  // namespace

Ray screenToRay(const Camera& cam, float screenX, float screenY) noexcept {
    // Screen → NDC. Vulkan convention: NDC X ∈ [-1, 1], NDC Y ∈ [-1, 1] (Y
    // down in screen-space matches NDC's Y-down clip). z=0 is near plane,
    // z=1 is far plane.
    const float w = cam.viewportW > 0.0f ? cam.viewportW : 1.0f;
    const float h = cam.viewportH > 0.0f ? cam.viewportH : 1.0f;
    const float ndcX = 2.0f * (screenX - cam.viewportX) / w - 1.0f;
    const float ndcY = 2.0f * (screenY - cam.viewportY) / h - 1.0f;

    const Mat4 viewMat = fromCamera(cam.view);
    const Mat4 projMat = fromCamera(cam.projection);
    const Mat4 vp = multiply(projMat, viewMat);

    Mat4 invVp{};
    if (!inverse4(vp, invVp)) return Ray{};

    float nearH[4];
    float farH[4];
    transform4(invVp, ndcX, ndcY, 0.0f, 1.0f, nearH);
    transform4(invVp, ndcX, ndcY, 1.0f, 1.0f, farH);

    const float nearW = nearH[3] != 0.0f ? nearH[3] : 1.0f;
    const float farW = farH[3] != 0.0f ? farH[3] : 1.0f;
    const float nx = nearH[0] / nearW;
    const float ny = nearH[1] / nearW;
    const float nz = nearH[2] / nearW;
    const float fx = farH[0] / farW;
    const float fy = farH[1] / farW;
    const float fz = farH[2] / farW;

    float dx = fx - nx;
    float dy = fy - ny;
    float dz = fz - nz;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 0.0f) {
        dx /= len;
        dy /= len;
        dz /= len;
    }
    Ray r{};
    r.origin[0] = nx;
    r.origin[1] = ny;
    r.origin[2] = nz;
    r.direction[0] = dx;
    r.direction[1] = dy;
    r.direction[2] = dz;
    return r;
}

ScreenPoint worldToScreen(const Camera& cam, const float worldXyz[3]) noexcept {
    const Mat4 viewMat = fromCamera(cam.view);
    const Mat4 projMat = fromCamera(cam.projection);
    const Mat4 vp = multiply(projMat, viewMat);

    float clip[4];
    transform4(vp, worldXyz[0], worldXyz[1], worldXyz[2], 1.0f, clip);

    ScreenPoint sp{};
    if (clip[3] <= 0.0f) {
        sp.inFrontOfCamera = false;
        return sp;
    }
    const float ndcX = clip[0] / clip[3];
    const float ndcY = clip[1] / clip[3];

    sp.x = cam.viewportX + (ndcX * 0.5f + 0.5f) * cam.viewportW;
    sp.y = cam.viewportY + (ndcY * 0.5f + 0.5f) * cam.viewportH;
    sp.inFrontOfCamera = true;
    return sp;
}

}  // namespace threadmaxx::input
