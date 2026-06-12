/// @file Registry.cpp
/// @brief `TypeRegistry` implementation. Deque-backed `TypeInfo`
/// storage so pointers handed to subscribers stay valid for the
/// registry's lifetime. Reader-prefer `shared_mutex` for the hot
/// lookup path.

#include <threadmaxx_reflect/registry.hpp>

#include <deque>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <threadmaxx_reflect/detail/name_arena.hpp>

namespace threadmaxx::reflect {

struct TypeRegistry::Impl {
    mutable std::shared_mutex                       mu;

    // Storage container with stable pointers (deque never invalidates
    // pointers on push_back).
    std::deque<TypeInfo>                            typeStorage;
    // Field storage owned by the registry; each TypeInfo's
    // `fields` span references a contiguous slice here.
    std::deque<std::vector<FieldInfo>>              fieldStorage;
    // Pointer index in registration order for `all()`.
    std::vector<const TypeInfo*>                    typeIndex;
    // Lookup indexes.
    std::unordered_map<std::type_index, TypeInfo*>  byTypeIndex;
    std::unordered_map<std::string_view, TypeInfo*> byName;
    // Owned string storage for runtime-supplied name overrides.
    detail::NameArena                               nameArena;
};

TypeRegistry::TypeRegistry() : impl_(std::make_unique<Impl>()) {}
TypeRegistry::~TypeRegistry() = default;

const TypeInfo* TypeRegistry::find(std::string_view name) const noexcept {
    std::shared_lock<std::shared_mutex> lk(impl_->mu);
    auto it = impl_->byName.find(name);
    return it == impl_->byName.end() ? nullptr : it->second;
}

const TypeInfo* TypeRegistry::find(std::type_index ti) const noexcept {
    std::shared_lock<std::shared_mutex> lk(impl_->mu);
    auto it = impl_->byTypeIndex.find(ti);
    return it == impl_->byTypeIndex.end() ? nullptr : it->second;
}

std::span<const TypeInfo* const> TypeRegistry::all() const noexcept {
    std::shared_lock<std::shared_mutex> lk(impl_->mu);
    return std::span<const TypeInfo* const>(impl_->typeIndex.data(),
                                            impl_->typeIndex.size());
}

std::size_t TypeRegistry::size() const noexcept {
    std::shared_lock<std::shared_mutex> lk(impl_->mu);
    return impl_->typeIndex.size();
}

const TypeInfo* TypeRegistry::registerTypeImpl(
    std::type_index ti,
    std::string_view nameOverride,
    std::uint32_t sizeBytes,
    std::uint32_t alignBytes,
    std::vector<FieldInfo> fields) {

    std::unique_lock<std::shared_mutex> lk(impl_->mu);

    // Idempotent: existing -> return without modification.
    if (auto it = impl_->byTypeIndex.find(ti); it != impl_->byTypeIndex.end()) {
        return it->second;
    }

    // Resolve the friendly name. nameOverride wins; otherwise fall back
    // to typeid(T).name() — copy either into the arena so a temporary
    // string is safe to pass in.
    std::string_view name;
    if (!nameOverride.empty()) {
        name = impl_->nameArena.intern(nameOverride);
    } else {
        const char* mangled = ti.name();
        name = impl_->nameArena.intern(std::string_view(mangled));
    }

    // Park the FieldInfo vector in stable storage; reference its span.
    auto& storedFields = impl_->fieldStorage.emplace_back(std::move(fields));

    impl_->typeStorage.emplace_back(TypeInfo{
        name,
        ti,
        sizeBytes,
        alignBytes,
        std::span<const FieldInfo>(storedFields.data(), storedFields.size()),
    });
    TypeInfo* info = &impl_->typeStorage.back();

    impl_->typeIndex.push_back(info);
    impl_->byTypeIndex.emplace(ti, info);
    impl_->byName.emplace(name, info);
    return info;
}

TypeRegistry& TypeRegistry::defaultInstance() noexcept {
    static TypeRegistry instance;
    return instance;
}

} // namespace threadmaxx::reflect
