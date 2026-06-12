/// @file Patch.cpp
/// @brief `applyPatch` + `readField` implementations.

#include <threadmaxx_reflect/patch.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <typeindex>

namespace threadmaxx::reflect {

namespace {

template <typename T>
bool writePrimitive(void* base, const FieldInfo& field, const Value& v) {
    T tmp{};
    if (!v.get(tmp)) return false;
    return field.set<T>(base, tmp);
}

bool writePrimitiveByTypeIndex(void* base, const FieldInfo& field, const Value& v) {
    const auto ti = field.typeIndex;
    if      (ti == std::type_index(typeid(bool)))      return writePrimitive<bool>(base, field, v);
    else if (ti == std::type_index(typeid(std::int8_t)))   return writePrimitive<std::int8_t>(base, field, v);
    else if (ti == std::type_index(typeid(std::int16_t)))  return writePrimitive<std::int16_t>(base, field, v);
    else if (ti == std::type_index(typeid(std::int32_t)))  return writePrimitive<std::int32_t>(base, field, v);
    else if (ti == std::type_index(typeid(std::int64_t)))  return writePrimitive<std::int64_t>(base, field, v);
    else if (ti == std::type_index(typeid(std::uint8_t)))  return writePrimitive<std::uint8_t>(base, field, v);
    else if (ti == std::type_index(typeid(std::uint16_t))) return writePrimitive<std::uint16_t>(base, field, v);
    else if (ti == std::type_index(typeid(std::uint32_t))) return writePrimitive<std::uint32_t>(base, field, v);
    else if (ti == std::type_index(typeid(std::uint64_t))) return writePrimitive<std::uint64_t>(base, field, v);
    else if (ti == std::type_index(typeid(float)))     return writePrimitive<float>(base, field, v);
    else if (ti == std::type_index(typeid(double)))    return writePrimitive<double>(base, field, v);
    return false;
}

template <typename T>
Value readPrimitive(const void* base, const FieldInfo& field) {
    const T* p = field.get<T>(base);
    return p ? Value::make<T>(*p) : Value{};
}

Value readPrimitiveByTypeIndex(const void* base, const FieldInfo& field) {
    const auto ti = field.typeIndex;
    if      (ti == std::type_index(typeid(bool)))      return readPrimitive<bool>(base, field);
    else if (ti == std::type_index(typeid(std::int8_t)))   return readPrimitive<std::int8_t>(base, field);
    else if (ti == std::type_index(typeid(std::int16_t)))  return readPrimitive<std::int16_t>(base, field);
    else if (ti == std::type_index(typeid(std::int32_t)))  return readPrimitive<std::int32_t>(base, field);
    else if (ti == std::type_index(typeid(std::int64_t)))  return readPrimitive<std::int64_t>(base, field);
    else if (ti == std::type_index(typeid(std::uint8_t)))  return readPrimitive<std::uint8_t>(base, field);
    else if (ti == std::type_index(typeid(std::uint16_t))) return readPrimitive<std::uint16_t>(base, field);
    else if (ti == std::type_index(typeid(std::uint32_t))) return readPrimitive<std::uint32_t>(base, field);
    else if (ti == std::type_index(typeid(std::uint64_t))) return readPrimitive<std::uint64_t>(base, field);
    else if (ti == std::type_index(typeid(float)))     return readPrimitive<float>(base, field);
    else if (ti == std::type_index(typeid(double)))    return readPrimitive<double>(base, field);
    return Value::makeOpaque(field.typeIndex,
        reinterpret_cast<const std::byte*>(base) + field.offset);
}

bool isFlatPath(std::string_view path) noexcept {
    for (char c : path) {
        if (c == '.' || c == '[' || c == ']') return false;
    }
    return true;
}

} // namespace

ReflectResult<void> applyPatch(const TypeInfo* typeInfo,
                               void* obj,
                               const Patch& patch) {
    if (typeInfo == nullptr || obj == nullptr) {
        return ReflectResult<void>::failure(ErrorCode::UnknownType,
                                            "null TypeInfo / obj");
    }
    for (const auto& entry : patch.entries) {
        if (!isFlatPath(entry.fieldPath)) {
            return ReflectResult<void>::failure(ErrorCode::Unsupported,
                "nested paths (.field / [n]) are v1.x; v1.0 supports flat names");
        }
        const auto* field = typeInfo->findField(entry.fieldPath);
        if (field == nullptr) {
            return ReflectResult<void>::failure(ErrorCode::UnknownField,
                std::string("unknown field '") + entry.fieldPath + "'");
        }
        if (!writePrimitiveByTypeIndex(obj, *field, entry.newValue)) {
            return ReflectResult<void>::failure(ErrorCode::TypeMismatch,
                std::string("type mismatch on field '") + entry.fieldPath + "'");
        }
    }
    return ReflectResult<void>::success();
}

ReflectResult<Value> readField(const TypeInfo* typeInfo,
                               const void* obj,
                               std::string_view fieldPath) {
    if (typeInfo == nullptr || obj == nullptr) {
        return ReflectResult<Value>::failure(ErrorCode::UnknownType, "null TypeInfo / obj");
    }
    if (!isFlatPath(fieldPath)) {
        return ReflectResult<Value>::failure(ErrorCode::Unsupported,
            "nested paths (.field / [n]) are v1.x; v1.0 supports flat names");
    }
    const auto* field = typeInfo->findField(fieldPath);
    if (field == nullptr) {
        return ReflectResult<Value>::failure(ErrorCode::UnknownField,
            std::string("unknown field '") + std::string(fieldPath) + "'");
    }
    Value v = readPrimitiveByTypeIndex(obj, *field);
    return ReflectResult<Value>::success(v);
}

} // namespace threadmaxx::reflect
