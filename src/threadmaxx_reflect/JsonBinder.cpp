/// @file JsonBinder.cpp
/// @brief Runtime `to_json` / `from_json` implementations driven by
/// `TypeInfo`. Primitive-only in v1.0 — strings, nested aggregates,
/// vectors, and arrays are deferred to v1.x.

#include <threadmaxx_reflect/binders/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <typeindex>

#include <threadmaxx_reflect/type_info.hpp>

namespace threadmaxx::reflect {

namespace {

template <typename T>
void appendPrimitive(std::string& out, const void* base, std::uint32_t offset) {
    const T* p = reinterpret_cast<const T*>(
        reinterpret_cast<const std::byte*>(base) + offset);
    char buf[64];
    int n = 0;
    if constexpr (std::is_same_v<T, bool>) {
        out += (*p ? "true" : "false");
        return;
    } else if constexpr (std::is_same_v<T, float> ||
                         std::is_same_v<T, double>) {
        n = std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(*p));
    } else if constexpr (std::is_signed_v<T>) {
        n = std::snprintf(buf, sizeof(buf), "%lld",
                          static_cast<long long>(*p));
    } else {
        n = std::snprintf(buf, sizeof(buf), "%llu",
                          static_cast<unsigned long long>(*p));
    }
    if (n > 0) out.append(buf, static_cast<std::size_t>(n));
    else out += "null";
}

void renderField(std::string& out, const FieldInfo& field, const void* obj) {
    const auto ti = field.typeIndex;
    if      (ti == std::type_index(typeid(bool)))     appendPrimitive<bool>(out, obj, field.offset);
    else if (ti == std::type_index(typeid(std::int8_t)))  appendPrimitive<std::int8_t>(out, obj, field.offset);
    else if (ti == std::type_index(typeid(std::int16_t))) appendPrimitive<std::int16_t>(out, obj, field.offset);
    else if (ti == std::type_index(typeid(std::int32_t))) appendPrimitive<std::int32_t>(out, obj, field.offset);
    else if (ti == std::type_index(typeid(std::int64_t))) appendPrimitive<std::int64_t>(out, obj, field.offset);
    else if (ti == std::type_index(typeid(std::uint8_t)))  appendPrimitive<std::uint8_t>(out, obj, field.offset);
    else if (ti == std::type_index(typeid(std::uint16_t))) appendPrimitive<std::uint16_t>(out, obj, field.offset);
    else if (ti == std::type_index(typeid(std::uint32_t))) appendPrimitive<std::uint32_t>(out, obj, field.offset);
    else if (ti == std::type_index(typeid(std::uint64_t))) appendPrimitive<std::uint64_t>(out, obj, field.offset);
    else if (ti == std::type_index(typeid(float)))    appendPrimitive<float>(out, obj, field.offset);
    else if (ti == std::type_index(typeid(double)))   appendPrimitive<double>(out, obj, field.offset);
    else                                              out += "null";
}

// Strip leading whitespace.
void skipWhitespace(std::string_view& s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
                          s.front() == '\n' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
}

bool consume(std::string_view& s, char c) {
    skipWhitespace(s);
    if (s.empty() || s.front() != c) return false;
    s.remove_prefix(1);
    return true;
}

std::string_view parseKey(std::string_view& s) {
    skipWhitespace(s);
    if (s.empty() || s.front() != '"') return {};
    s.remove_prefix(1);
    auto end = s.find('"');
    if (end == std::string_view::npos) return {};
    std::string_view key = s.substr(0, end);
    s.remove_prefix(end + 1);
    return key;
}

std::string_view parseValueToken(std::string_view& s) {
    skipWhitespace(s);
    std::size_t end = 0;
    while (end < s.size() && s[end] != ',' && s[end] != '}' &&
           s[end] != ' ' && s[end] != '\t' && s[end] != '\n' && s[end] != '\r') {
        ++end;
    }
    std::string_view tok = s.substr(0, end);
    s.remove_prefix(end);
    return tok;
}

template <typename T>
bool writePrimitive(void* base, std::uint32_t offset, std::string_view tok) {
    T* p = reinterpret_cast<T*>(reinterpret_cast<std::byte*>(base) + offset);
    if constexpr (std::is_same_v<T, bool>) {
        if (tok == "true")  { *p = true;  return true; }
        if (tok == "false") { *p = false; return true; }
        return false;
    } else if constexpr (std::is_same_v<T, float> ||
                         std::is_same_v<T, double>) {
        char buf[64];
        const std::size_t n = (tok.size() < sizeof(buf) - 1) ? tok.size() : sizeof(buf) - 1;
        std::memcpy(buf, tok.data(), n);
        buf[n] = '\0';
        char* endp = nullptr;
        double v = std::strtod(buf, &endp);
        if (endp == buf) return false;
        *p = static_cast<T>(v);
        return true;
    } else if constexpr (std::is_signed_v<T>) {
        char buf[64];
        const std::size_t n = (tok.size() < sizeof(buf) - 1) ? tok.size() : sizeof(buf) - 1;
        std::memcpy(buf, tok.data(), n);
        buf[n] = '\0';
        char* endp = nullptr;
        long long v = std::strtoll(buf, &endp, 10);
        if (endp == buf) return false;
        *p = static_cast<T>(v);
        return true;
    } else {
        char buf[64];
        const std::size_t n = (tok.size() < sizeof(buf) - 1) ? tok.size() : sizeof(buf) - 1;
        std::memcpy(buf, tok.data(), n);
        buf[n] = '\0';
        char* endp = nullptr;
        unsigned long long v = std::strtoull(buf, &endp, 10);
        if (endp == buf) return false;
        *p = static_cast<T>(v);
        return true;
    }
}

bool writeField(const FieldInfo& field, void* obj, std::string_view tok) {
    const auto ti = field.typeIndex;
    if      (ti == std::type_index(typeid(bool)))     return writePrimitive<bool>(obj, field.offset, tok);
    else if (ti == std::type_index(typeid(std::int8_t)))  return writePrimitive<std::int8_t>(obj, field.offset, tok);
    else if (ti == std::type_index(typeid(std::int16_t))) return writePrimitive<std::int16_t>(obj, field.offset, tok);
    else if (ti == std::type_index(typeid(std::int32_t))) return writePrimitive<std::int32_t>(obj, field.offset, tok);
    else if (ti == std::type_index(typeid(std::int64_t))) return writePrimitive<std::int64_t>(obj, field.offset, tok);
    else if (ti == std::type_index(typeid(std::uint8_t)))  return writePrimitive<std::uint8_t>(obj, field.offset, tok);
    else if (ti == std::type_index(typeid(std::uint16_t))) return writePrimitive<std::uint16_t>(obj, field.offset, tok);
    else if (ti == std::type_index(typeid(std::uint32_t))) return writePrimitive<std::uint32_t>(obj, field.offset, tok);
    else if (ti == std::type_index(typeid(std::uint64_t))) return writePrimitive<std::uint64_t>(obj, field.offset, tok);
    else if (ti == std::type_index(typeid(float)))    return writePrimitive<float>(obj, field.offset, tok);
    else if (ti == std::type_index(typeid(double)))   return writePrimitive<double>(obj, field.offset, tok);
    else                                              return false;
}

} // namespace

std::string to_json(const TypeInfo* info, const void* obj) {
    if (info == nullptr || obj == nullptr) return "null";
    std::string out;
    out.reserve(64 + info->fields.size() * 16);
    out += "{";
    bool first = true;
    for (const auto& field : info->fields) {
        if (!first) out += ",";
        first = false;
        out += "\"";
        out.append(field.name);
        out += "\":";
        renderField(out, field, obj);
    }
    out += "}";
    return out;
}

ReflectResult<void> from_json(const TypeInfo* info,
                              void* obj,
                              std::string_view json) {
    if (info == nullptr || obj == nullptr) {
        return ReflectResult<void>::failure(ErrorCode::UnknownType, "null TypeInfo / obj");
    }
    skipWhitespace(json);
    if (!consume(json, '{')) {
        return ReflectResult<void>::failure(ErrorCode::ParseError, "expected '{'");
    }
    skipWhitespace(json);
    if (consume(json, '}')) return ReflectResult<void>::success();

    while (true) {
        std::string_view key = parseKey(json);
        if (key.empty()) {
            return ReflectResult<void>::failure(ErrorCode::ParseError, "expected key");
        }
        if (!consume(json, ':')) {
            return ReflectResult<void>::failure(ErrorCode::ParseError, "expected ':'");
        }
        std::string_view tok = parseValueToken(json);
        if (tok.empty()) {
            return ReflectResult<void>::failure(ErrorCode::ParseError, "expected value");
        }
        const auto* field = info->findField(key);
        if (field != nullptr) {
            if (!writeField(*field, obj, tok)) {
                return ReflectResult<void>::failure(ErrorCode::TypeMismatch,
                    std::string("could not parse field '") +
                    std::string(key) + "'");
            }
        }
        // Unknown fields are skipped silently.
        skipWhitespace(json);
        if (consume(json, '}')) return ReflectResult<void>::success();
        if (!consume(json, ',')) {
            return ReflectResult<void>::failure(ErrorCode::ParseError, "expected ',' or '}'");
        }
    }
}

} // namespace threadmaxx::reflect
