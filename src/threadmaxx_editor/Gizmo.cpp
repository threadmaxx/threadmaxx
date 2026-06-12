/// @file Gizmo.cpp
/// @brief Translate-gizmo math.

#include "threadmaxx_editor/gizmo.hpp"

#include <threadmaxx/CommandBuffer.hpp>

#include <limits>
#include <memory>

namespace threadmaxx::editor {

namespace {

float dot(const threadmaxx::Vec3& a, const threadmaxx::Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float lengthSq(const threadmaxx::Vec3& v) noexcept {
    return dot(v, v);
}

class SetTransformCommand final : public IEditCommand {
public:
    SetTransformCommand(threadmaxx::EntityHandle target,
                        const threadmaxx::Transform& oldT,
                        const threadmaxx::Transform& newT)
        : target_(target), old_(oldT), new_(newT) {}

    std::string_view name() const noexcept override { return "GizmoTranslate"; }
    void apply(threadmaxx::CommandBuffer& cb) override {
        cb.setTransform(target_, new_);
    }
    void undo(threadmaxx::CommandBuffer& cb) override {
        cb.setTransform(target_, old_);
    }

private:
    threadmaxx::EntityHandle target_;
    threadmaxx::Transform old_;
    threadmaxx::Transform new_;
};

} // namespace

GizmoFrame TranslateGizmo::frameFor(
        const threadmaxx::Vec3& entityPos) const noexcept {
    GizmoFrame f{};
    f.mode = GizmoMode::Translate;
    f.origin = entityPos;
    f.x = AxisHandle{GizmoAxis::X, entityPos, {1.0f, 0.0f, 0.0f}, 1.0f, 0.1f};
    f.y = AxisHandle{GizmoAxis::Y, entityPos, {0.0f, 1.0f, 0.0f}, 1.0f, 0.1f};
    f.z = AxisHandle{GizmoAxis::Z, entityPos, {0.0f, 0.0f, 1.0f}, 1.0f, 0.1f};
    f.activeDrag = activeAxis_;
    return f;
}

GizmoAxis TranslateGizmo::hitTest(const GizmoFrame& frame,
                                  const Ray3& ray) const noexcept {
    if (lengthSq(ray.dir) == 0.0f) return GizmoAxis::None;

    // For each axis, find the point on the ray closest to the axis line
    // (treat both as infinite lines for v1.0). If the distance is
    // within the handle radius AND falls within [0, length] along the
    // axis, the handle is hit.
    const AxisHandle handles[3] = {frame.x, frame.y, frame.z};
    GizmoAxis best = GizmoAxis::None;
    float bestT = std::numeric_limits<float>::infinity();

    for (const auto& h : handles) {
        // Closest-points-on-two-lines: line A = ray, line B = (origin, axisDir).
        // We parameterize A by t (point = ray.origin + t * ray.dir) and
        // B by s (point = h.origin + s * h.axisDir).
        const auto u = ray.dir;
        const auto v = h.axisDir;
        const auto w0 = ray.origin - h.origin;
        const float a = dot(u, u);
        const float b = dot(u, v);
        const float c = dot(v, v);
        const float d = dot(u, w0);
        const float e = dot(v, w0);
        const float denom = a * c - b * b;
        if (denom <= 1e-6f) continue; // parallel
        const float t = (b * e - c * d) / denom;
        const float s = (a * e - b * d) / denom;
        if (s < 0.0f || s > h.length) continue;
        if (t < 0.0f) continue;
        const auto pA = ray.origin + ray.dir * t;
        const auto pB = h.origin + h.axisDir * s;
        const auto sep = pA - pB;
        const float distSq = lengthSq(sep);
        if (distSq <= h.radius * h.radius && t < bestT) {
            bestT = t;
            best = h.axis;
        }
    }
    return best;
}

bool TranslateGizmo::beginDrag(GizmoAxis axis) noexcept {
    if (axis == GizmoAxis::None) return false;
    activeAxis_ = axis;
    return true;
}

std::optional<GizmoDragResult>
TranslateGizmo::updateDrag(float axisDelta) const noexcept {
    if (activeAxis_ == GizmoAxis::None) return std::nullopt;
    GizmoDragResult r{};
    switch (activeAxis_) {
        case GizmoAxis::X: r.delta = {axisDelta, 0.0f, 0.0f}; break;
        case GizmoAxis::Y: r.delta = {0.0f, axisDelta, 0.0f}; break;
        case GizmoAxis::Z: r.delta = {0.0f, 0.0f, axisDelta}; break;
        default: break;
    }
    return r;
}

void TranslateGizmo::endDrag() noexcept {
    activeAxis_ = GizmoAxis::None;
}

std::unique_ptr<IEditCommand>
TranslateGizmo::makeTranslateCommand(threadmaxx::EntityHandle target,
                                     const threadmaxx::Transform& oldT,
                                     const threadmaxx::Transform& newT) {
    return std::make_unique<SetTransformCommand>(target, oldT, newT);
}

} // namespace threadmaxx::editor
