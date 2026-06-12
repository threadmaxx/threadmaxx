/// @file PropertyEditor.cpp
/// @brief Reflection-driven property panel.

#include "threadmaxx_editor/properties.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/World.hpp>

#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/patch.hpp>

#if defined(THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE) && \
    THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE
#include <threadmaxx_reflect/engine_bridge.hpp>
#endif

#include <cstring>
#include <memory>
#include <utility>

namespace threadmaxx::editor {

namespace {

// Write the contents of `v` (a primitive reflect::Value) into `dst`
// according to the field's typeIndex. Returns true on a successful
// type-matched write.
bool writePrimitive(const threadmaxx::reflect::FieldInfo& field,
                    std::byte* objBytes,
                    const threadmaxx::reflect::Value& v) {
    auto* dst = objBytes + field.offset;
    auto tryT = [&]<typename T>() -> bool {
        if (field.typeIndex != std::type_index(typeid(T))) return false;
        T tmp{};
        if (!v.get<T>(tmp)) return false;
        std::memcpy(dst, &tmp, sizeof(T));
        return true;
    };
    if (tryT.template operator()<bool>())          return true;
    if (tryT.template operator()<std::int8_t>())   return true;
    if (tryT.template operator()<std::int16_t>())  return true;
    if (tryT.template operator()<std::int32_t>())  return true;
    if (tryT.template operator()<std::int64_t>())  return true;
    if (tryT.template operator()<std::uint8_t>())  return true;
    if (tryT.template operator()<std::uint16_t>()) return true;
    if (tryT.template operator()<std::uint32_t>()) return true;
    if (tryT.template operator()<std::uint64_t>()) return true;
    if (tryT.template operator()<float>())         return true;
    if (tryT.template operator()<double>())        return true;
    return false;
}

class SetFieldCommand final : public IEditCommand {
public:
    SetFieldCommand(const PropertyEditor::ComponentBinding* binding,
                    threadmaxx::EntityHandle target,
                    std::vector<std::byte> oldValue,
                    std::vector<std::byte> newValue)
        : binding_(binding), target_(target),
          old_(std::move(oldValue)), new_(std::move(newValue)) {}

    std::string_view name() const noexcept override { return "SetField"; }
    void apply(threadmaxx::CommandBuffer& cb) override {
        binding_->set(cb, target_, new_.data());
    }
    void undo(threadmaxx::CommandBuffer& cb) override {
        binding_->set(cb, target_, old_.data());
    }

private:
    const PropertyEditor::ComponentBinding* binding_;
    threadmaxx::EntityHandle target_;
    std::vector<std::byte> old_;
    std::vector<std::byte> new_;
};

// Trampoline factory: build a SetFn from a typed setter.
template <typename T, void (threadmaxx::CommandBuffer::*Setter)(
              threadmaxx::EntityHandle, const T&)>
PropertyEditor::SetFn makeSet() {
    return [](threadmaxx::CommandBuffer& cb,
              threadmaxx::EntityHandle e,
              const void* ptr) {
        const T* tp = static_cast<const T*>(ptr);
        (cb.*Setter)(e, *tp);
    };
}

template <typename T,
          const T* (threadmaxx::World::*Getter)(
              threadmaxx::EntityHandle) const noexcept>
PropertyEditor::GetFn makeGet() {
    return [](const threadmaxx::World& w,
              threadmaxx::EntityHandle e) -> const void* {
        return (w.*Getter)(e);
    };
}

} // namespace

PropertyEditor::PropertyEditor(threadmaxx::Engine& engine,
                               threadmaxx::reflect::TypeRegistry& registry)
    : engine_(&engine), registry_(&registry) {}

void PropertyEditor::addBinding(ComponentBinding b) {
    if (!hasBinding(b.typeName)) bindings_.push_back(std::move(b));
}

void PropertyEditor::addBuiltinBindings() {
#if defined(THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE) && \
    THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE
    // Idempotent on typeid(T); safe to call repeatedly.
    threadmaxx::reflect::engine_bridge::registerBuiltins(*registry_);
#endif
    auto bind = [&](std::string n,
                    const threadmaxx::reflect::TypeInfo* ti,
                    GetFn g, SetFn s) {
        if (!ti) return;
        ComponentBinding b{};
        b.typeName = std::move(n);
        b.type = ti;
        b.get = std::move(g);
        b.set = std::move(s);
        addBinding(std::move(b));
    };

    bind("Transform",
         registry_->find("Transform"),
         makeGet<threadmaxx::Transform, &threadmaxx::World::tryGetTransform>(),
         makeSet<threadmaxx::Transform,
                 &threadmaxx::CommandBuffer::setTransform>());
    bind("Velocity",
         registry_->find("Velocity"),
         makeGet<threadmaxx::Velocity, &threadmaxx::World::tryGetVelocity>(),
         makeSet<threadmaxx::Velocity,
                 &threadmaxx::CommandBuffer::setVelocity>());
    bind("Acceleration",
         registry_->find("Acceleration"),
         makeGet<threadmaxx::Acceleration,
                 &threadmaxx::World::tryGetAcceleration>(),
         makeSet<threadmaxx::Acceleration,
                 &threadmaxx::CommandBuffer::setAcceleration>());
    bind("Health",
         registry_->find("Health"),
         makeGet<threadmaxx::Health, &threadmaxx::World::tryGetHealth>(),
         makeSet<threadmaxx::Health,
                 &threadmaxx::CommandBuffer::setHealth>());
    bind("Faction",
         registry_->find("Faction"),
         makeGet<threadmaxx::Faction, &threadmaxx::World::tryGetFaction>(),
         makeSet<threadmaxx::Faction,
                 &threadmaxx::CommandBuffer::setFaction>());
    bind("UserData",
         registry_->find("UserData"),
         makeGet<threadmaxx::UserData, &threadmaxx::World::tryGetUserData>(),
         makeSet<threadmaxx::UserData,
                 &threadmaxx::CommandBuffer::setUserData>());
    bind("BoundingVolume",
         registry_->find("BoundingVolume"),
         makeGet<threadmaxx::BoundingVolume,
                 &threadmaxx::World::tryGetBoundingVolume>(),
         makeSet<threadmaxx::BoundingVolume,
                 &threadmaxx::CommandBuffer::setBoundingVolume>());
}

std::vector<std::string>
PropertyEditor::componentsOn(threadmaxx::EntityHandle entity) const {
    std::vector<std::string> out;
    out.reserve(bindings_.size());
    const auto& world = engine_->world();
    for (const auto& b : bindings_) {
        if (b.get(world, entity) != nullptr) out.push_back(b.typeName);
    }
    return out;
}

bool PropertyEditor::hasBinding(std::string_view typeName) const noexcept {
    for (const auto& b : bindings_) {
        if (b.typeName == typeName) return true;
    }
    return false;
}

const PropertyEditor::ComponentBinding*
PropertyEditor::findBinding(std::string_view typeName) const noexcept {
    for (const auto& b : bindings_) {
        if (b.typeName == typeName) return &b;
    }
    return nullptr;
}

std::optional<threadmaxx::reflect::Value>
PropertyEditor::readField(threadmaxx::EntityHandle entity,
                          std::string_view typeName,
                          std::string_view fieldName) const {
    const auto* binding = findBinding(typeName);
    if (!binding || !binding->type) return std::nullopt;
    const auto* fi = binding->type->findField(fieldName);
    if (!fi) return std::nullopt;
    const void* obj = binding->get(engine_->world(), entity);
    if (!obj) return std::nullopt;
    auto r = threadmaxx::reflect::readField(binding->type, obj, fieldName);
    if (!r.ok()) return std::nullopt;
    return r.value;
}

EditResult PropertyEditor::setField(CommandStack& stack,
                                    threadmaxx::EntityHandle entity,
                                    std::string_view typeName,
                                    std::string_view fieldName,
                                    const threadmaxx::reflect::Value& v) {
    const auto* binding = findBinding(typeName);
    if (!binding || !binding->type) return EditResult::Rejected;
    const auto* fi = binding->type->findField(fieldName);
    if (!fi) return EditResult::Rejected;
    const void* obj = binding->get(engine_->world(), entity);
    if (!obj) return EditResult::Rejected;

    const std::size_t sz = binding->type->sizeBytes;
    std::vector<std::byte> oldBuf(sz);
    std::memcpy(oldBuf.data(), obj, sz);
    std::vector<std::byte> newBuf = oldBuf;
    if (!writePrimitive(*fi, newBuf.data(), v)) {
        return EditResult::Rejected;
    }
    return stack.execute(std::make_unique<SetFieldCommand>(
        binding, entity, std::move(oldBuf), std::move(newBuf)));
}

std::span<const PropertyEditor::ComponentBinding>
PropertyEditor::bindings() const noexcept {
    return {bindings_.data(), bindings_.size()};
}

} // namespace threadmaxx::editor
