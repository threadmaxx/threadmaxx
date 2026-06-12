#pragma once

/// @file properties.hpp
/// @brief Reflection-driven property panel.
///
/// Sits on top of `threadmaxx_reflect` (R7's engine bridge surfaces
/// every built-in component's `TypeInfo`). For each component the
/// editor cares about, the user (or `addBuiltinBindings()`) supplies a
/// `ComponentBinding` that knows how to:
/// - read the current component value for an entity (returns nullptr
///   if the component is absent),
/// - write a complete new component value via `CommandBuffer`.
///
/// `readField` / `setField` then route through the registered
/// `reflect::TypeInfo` so the editor doesn't need to know the C++
/// component type.

#include "commands.hpp"
#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Handles.hpp>
#include <threadmaxx_reflect/registry.hpp>
#include <threadmaxx_reflect/type_info.hpp>
#include <threadmaxx_reflect/value.hpp>

namespace threadmaxx {
class CommandBuffer;
class World;
} // namespace threadmaxx

namespace threadmaxx::editor {

class PropertyEditor {
public:
    /// @brief Pulls one component's current value, or nullptr when the
    /// entity does not carry it.
    using GetFn = std::function<const void*(
        const threadmaxx::World&, threadmaxx::EntityHandle)>;

    /// @brief Re-emits one complete component value through a command
    /// buffer (the binding owns the type-dispatch to `cb.setX`).
    using SetFn = std::function<void(
        threadmaxx::CommandBuffer&,
        threadmaxx::EntityHandle,
        const void* /*sizeof(T) bytes*/)>;

    struct ComponentBinding {
        std::string typeName;
        const threadmaxx::reflect::TypeInfo* type{nullptr};
        GetFn get;
        SetFn set;
    };

    PropertyEditor(threadmaxx::Engine& engine,
                   threadmaxx::reflect::TypeRegistry& registry);

    /// @brief Register a binding for one component type.
    void addBinding(ComponentBinding b);

    /// @brief Register bindings for every engine-built-in data
    /// component (Transform, Velocity, Acceleration, Health, Faction,
    /// UserData, AnimationStateRef, PhysicsBodyRef, NavAgentRef,
    /// BoundingVolume). Idempotent; calling more than once is safe.
    void addBuiltinBindings();

    /// @brief Editor-facing type names of every component currently
    /// present on `entity`. Order matches binding registration.
    std::vector<std::string>
    componentsOn(threadmaxx::EntityHandle entity) const;

    bool hasBinding(std::string_view typeName) const noexcept;
    const ComponentBinding* findBinding(std::string_view typeName) const noexcept;

    /// @brief Read a primitive field value. Returns nullopt when the
    /// entity does not carry the component, the binding is unknown,
    /// the field is unknown, or the field's value type isn't a
    /// `reflect::Value`-supported primitive.
    std::optional<threadmaxx::reflect::Value>
    readField(threadmaxx::EntityHandle entity,
              std::string_view typeName,
              std::string_view fieldName) const;

    /// @brief Write a primitive field value through the command stack.
    /// Reads the current component, mutates the named field locally,
    /// queues a SetField command that re-emits the complete new value
    /// (apply) or the original (undo).
    EditResult setField(CommandStack& stack,
                        threadmaxx::EntityHandle entity,
                        std::string_view typeName,
                        std::string_view fieldName,
                        const threadmaxx::reflect::Value& newValue);

    /// @brief Currently-registered bindings.
    std::span<const ComponentBinding> bindings() const noexcept;

private:
    threadmaxx::Engine* engine_;
    threadmaxx::reflect::TypeRegistry* registry_;
    std::vector<ComponentBinding> bindings_;
};

} // namespace threadmaxx::editor
