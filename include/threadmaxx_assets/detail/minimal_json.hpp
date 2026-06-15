#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

/// Header-only minimal JSON parser. Hand-rolled so the asset library
/// stays dependency-free; only the subset glTF needs is implemented.
///
/// Surface:
///   threadmaxx::assets::detail::JsonValue       — tagged-union tree node
///   threadmaxx::assets::detail::parseJson(s)    — returns owning root
///                                                  or empty on failure
///
/// Performance is best-effort, not optimal — glTF JSON descriptors are
/// kilobyte-scale; binary payloads live in the BIN chunk. We don't
/// need a streaming parser.
namespace threadmaxx::assets::detail {

class JsonValue {
public:
    enum class Type : std::uint8_t {
        Null = 0,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    JsonValue() noexcept = default;
    explicit JsonValue(Type t) noexcept : type_{t} {}

    Type type() const noexcept { return type_; }

    bool isNull()   const noexcept { return type_ == Type::Null; }
    bool isBool()   const noexcept { return type_ == Type::Bool; }
    bool isNumber() const noexcept { return type_ == Type::Number; }
    bool isString() const noexcept { return type_ == Type::String; }
    bool isArray()  const noexcept { return type_ == Type::Array; }
    bool isObject() const noexcept { return type_ == Type::Object; }

    bool        asBool()   const noexcept { return boolValue_; }
    double      asNumber() const noexcept { return numberValue_; }
    const std::string& asString() const noexcept { return stringValue_; }

    const std::vector<JsonValue>& asArray() const noexcept { return arrayValue_; }
    std::vector<JsonValue>&       asArray()       noexcept { return arrayValue_; }

    // Object members are stored as parallel keys + values for cheap
    // ordered iteration. Linear lookup is fine — glTF objects have
    // at most a few dozen keys.
    const std::vector<std::string>& objectKeys()   const noexcept { return objectKeys_; }
    const std::vector<JsonValue>&   objectValues() const noexcept { return objectValues_; }

    const JsonValue* find(std::string_view key) const noexcept {
        for (std::size_t i = 0; i < objectKeys_.size(); ++i) {
            if (objectKeys_[i] == key) return &objectValues_[i];
        }
        return nullptr;
    }

    // Mutators used during construction by the parser.
    void setBool(bool v) noexcept { type_ = Type::Bool; boolValue_ = v; }
    void setNumber(double v) noexcept { type_ = Type::Number; numberValue_ = v; }
    void setString(std::string v) { type_ = Type::String; stringValue_ = std::move(v); }
    std::vector<JsonValue>& makeArray() {
        type_ = Type::Array;
        return arrayValue_;
    }
    std::pair<std::vector<std::string>&, std::vector<JsonValue>&> makeObject() {
        type_ = Type::Object;
        return {objectKeys_, objectValues_};
    }

private:
    Type                       type_{Type::Null};
    bool                       boolValue_{};
    double                     numberValue_{};
    std::string                stringValue_;
    std::vector<JsonValue>     arrayValue_;
    std::vector<std::string>   objectKeys_;
    std::vector<JsonValue>     objectValues_;
};

namespace minimal_json_impl {

struct Parser {
    const char* p{};
    const char* end{};
    bool        ok{true};

    void skipWs() noexcept {
        while (p < end) {
            const char c = *p;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++p;
            } else {
                break;
            }
        }
    }

    bool match(char c) noexcept {
        skipWs();
        if (p < end && *p == c) { ++p; return true; }
        return false;
    }

    bool peek(char c) noexcept {
        skipWs();
        return p < end && *p == c;
    }

    bool consumeLiteral(const char* lit) noexcept {
        const std::size_t n = std::strlen(lit);
        if (static_cast<std::size_t>(end - p) < n) return false;
        if (std::memcmp(p, lit, n) != 0) return false;
        p += n;
        return true;
    }

    bool parseString(std::string& out) noexcept {
        if (!match('"')) { ok = false; return false; }
        out.clear();
        while (p < end && *p != '"') {
            char c = *p++;
            if (c == '\\') {
                if (p >= end) { ok = false; return false; }
                char esc = *p++;
                switch (esc) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        // Minimal \uXXXX → UTF-8 (BMP only; surrogate
                        // pairs are not glTF-shaped data).
                        if (end - p < 4) { ok = false; return false; }
                        unsigned cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = p[i];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                            else { ok = false; return false; }
                        }
                        p += 4;
                        if (cp < 0x80) {
                            out.push_back(static_cast<char>(cp));
                        } else if (cp < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default: ok = false; return false;
                }
            } else {
                out.push_back(c);
            }
        }
        if (p >= end || *p != '"') { ok = false; return false; }
        ++p;
        return true;
    }

    bool parseNumber(double& out) noexcept {
        skipWs();
        char buf[64];
        std::size_t n = 0;
        while (p < end && n < sizeof(buf) - 1) {
            char c = *p;
            const bool numChar =
                (c >= '0' && c <= '9') || c == '-' || c == '+' ||
                c == '.' || c == 'e' || c == 'E';
            if (!numChar) break;
            buf[n++] = c;
            ++p;
        }
        if (n == 0) { ok = false; return false; }
        buf[n] = '\0';
        char* endPtr = nullptr;
        out = std::strtod(buf, &endPtr);
        if (endPtr == buf) { ok = false; return false; }
        return true;
    }

    void parseValue(JsonValue& out) noexcept {
        skipWs();
        if (p >= end) { ok = false; return; }
        char c = *p;
        if (c == '"') {
            std::string s;
            if (parseString(s)) out.setString(std::move(s));
        } else if (c == '{') {
            ++p;
            auto pair = out.makeObject();
            auto& keys = pair.first;
            auto& vals = pair.second;
            if (peek('}')) { ++p; return; }
            for (;;) {
                std::string k;
                if (!parseString(k)) return;
                if (!match(':')) { ok = false; return; }
                keys.push_back(std::move(k));
                vals.emplace_back();
                parseValue(vals.back());
                if (!ok) return;
                if (match(',')) continue;
                if (match('}')) return;
                ok = false;
                return;
            }
        } else if (c == '[') {
            ++p;
            auto& arr = out.makeArray();
            if (peek(']')) { ++p; return; }
            for (;;) {
                arr.emplace_back();
                parseValue(arr.back());
                if (!ok) return;
                if (match(',')) continue;
                if (match(']')) return;
                ok = false;
                return;
            }
        } else if (c == 't') {
            if (consumeLiteral("true")) { out.setBool(true); return; }
            ok = false;
        } else if (c == 'f') {
            if (consumeLiteral("false")) { out.setBool(false); return; }
            ok = false;
        } else if (c == 'n') {
            if (consumeLiteral("null")) return;     // already Null
            ok = false;
        } else {
            double v = 0;
            if (parseNumber(v)) out.setNumber(v);
        }
    }
};

} // namespace minimal_json_impl

inline bool parseJson(std::string_view src, JsonValue& out) noexcept {
    minimal_json_impl::Parser ps;
    ps.p   = src.data();
    ps.end = src.data() + src.size();
    ps.parseValue(out);
    return ps.ok;
}

} // namespace threadmaxx::assets::detail
