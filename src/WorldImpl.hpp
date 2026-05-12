#pragma once

#include "EntityStorage.hpp"

namespace threadmaxx::internal {

// Private engine-internal home of the world. The public World class
// forwards to this through a unique_ptr (PImpl). The engine constructs one
// WorldImpl, hands its World handle to systems, and uses impl_() to mutate.
class WorldImpl {
public:
    explicit WorldImpl(std::uint32_t initialCapacity)
        : storage(initialCapacity) {}

    EntityStorage storage;
};

} // namespace threadmaxx::internal
