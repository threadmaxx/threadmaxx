#include "threadmaxx_animation/eval.hpp"

#include "threadmaxx_animation/detail/job_batch.hpp"

namespace threadmaxx::animation {

void evaluateBatch(std::span<Animator> animators,
                   std::span<PoseBuffer> outPoses,
                   EvalContext ctx,
                   std::span<EvalResult> outResults,
                   std::span<const std::uint8_t> skipMask) {
    detail::evaluateBatchRange(animators,
                               outPoses,
                               /*begin=*/0,
                               /*end=*/animators.size(),
                               ctx,
                               outResults,
                               skipMask);
}

} // namespace threadmaxx::animation
