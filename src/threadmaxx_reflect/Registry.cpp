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
    mutable std::shared_mutex mu;

    /// Bundle every per-type owned storage so pointers stay together.
    struct StoredType {
        TypeInfo                                info;
        std::vector<FieldInfo>                  fields;
        // Parallel to `fields` — fieldAttrs[i] is the attribute list
        // for fields[i]. Owned by registry so FieldInfo::attributes
        // is a stable span.
        std::vector<std::vector<AttributeInfo>> fieldAttrs;
    };

    // Stable-pointer container for StoredType.
    std::deque<StoredType>                          types;
    std::vector<const TypeInfo*>                    typeIndex;
    std::unordered_map<std::type_index, TypeInfo*>  byTypeIndex;
    std::unordered_map<std::string_view, TypeInfo*> byName;
    detail::NameArena                               nameArena;

    // Find the owning StoredType for a TypeInfo*. nullptr if foreign.
    StoredType* find(const TypeInfo* info) noexcept {
        if (info == nullptr) return nullptr;
        for (auto& t : types) {
            if (&t.info == info) return &t;
        }
        return nullptr;
    }
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

    // Push a new StoredType; pointers are stable on deque growth.
    auto& stored = impl_->types.emplace_back(Impl::StoredType{});
    stored.fields     = std::move(fields);
    stored.fieldAttrs = std::vector<std::vector<AttributeInfo>>(stored.fields.size());

    stored.info = TypeInfo{
        name,
        ti,
        sizeBytes,
        alignBytes,
        std::span<const FieldInfo>(stored.fields.data(), stored.fields.size()),
    };

    impl_->typeIndex.push_back(&stored.info);
    impl_->byTypeIndex.emplace(ti, &stored.info);
    impl_->byName.emplace(name, &stored.info);
    return &stored.info;
}

bool TypeRegistry::addFieldAttributeImpl(const TypeInfo* typeInfo,
                                        std::string_view fieldName,
                                        std::string_view attrName,
                                        std::string_view attrPayload) {
    std::unique_lock<std::shared_mutex> lk(impl_->mu);
    auto* stored = impl_->find(typeInfo);
    if (stored == nullptr) return false;

    std::size_t fieldIdx = 0;
    bool found = false;
    for (; fieldIdx < stored->fields.size(); ++fieldIdx) {
        if (stored->fields[fieldIdx].name == fieldName) { found = true; break; }
    }
    if (!found) return false;

    // Intern the payload — name comes from a static constexpr literal
    // already (Attr::kName lives in .rodata) so we keep the original
    // string_view as-is.
    std::string_view payloadView = attrPayload.empty()
                                       ? std::string_view{}
                                       : impl_->nameArena.intern(attrPayload);

    auto& vec = stored->fieldAttrs[fieldIdx];
    vec.push_back(AttributeInfo{attrName, payloadView});

    // Re-point the FieldInfo's attributes span to the (possibly-
    // reallocated) vector storage.
    stored->fields[fieldIdx].attributes =
        std::span<const AttributeInfo>(vec.data(), vec.size());
    return true;
}

TypeRegistry& TypeRegistry::defaultInstance() noexcept {
    static TypeRegistry instance;
    return instance;
}

} // namespace threadmaxx::reflect
