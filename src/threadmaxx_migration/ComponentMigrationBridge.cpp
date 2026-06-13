/// @file ComponentMigrationBridge.cpp
/// @brief M7 — ComponentMigrationBridge implementation.

#include <threadmaxx_migration/component.hpp>

#include <algorithm>
#include <utility>

namespace threadmaxx::migration {

void ComponentMigrationBridge::registerCodec(std::string typeName,
                                            SchemaVersion version,
                                            ComponentEncoder encoder,
                                            ComponentDecoder decoder) {
    auto& list = codecs_[std::move(typeName)];
    for (auto& entry : list) {
        if (entry.version == version) {
            entry.encoder = std::move(encoder);
            entry.decoder = std::move(decoder);
            return;
        }
    }
    list.push_back(VersionedCodec{version, std::move(encoder),
                                  std::move(decoder)});
    std::sort(list.begin(), list.end(),
              [](const VersionedCodec& a, const VersionedCodec& b) {
                  return a.version < b.version;
              });
}

const ComponentEncoder*
ComponentMigrationBridge::findEncoder(std::string_view typeName,
                                     SchemaVersion version) const noexcept {
    auto it = codecs_.find(std::string(typeName));
    if (it == codecs_.end()) return nullptr;
    const ComponentEncoder* picked = nullptr;
    for (const auto& entry : it->second) {
        if (entry.version <= version && entry.encoder) {
            picked = &entry.encoder;
        }
        if (version < entry.version) break;
    }
    return picked;
}

const ComponentDecoder*
ComponentMigrationBridge::findDecoder(std::string_view typeName,
                                     SchemaVersion version) const noexcept {
    auto it = codecs_.find(std::string(typeName));
    if (it == codecs_.end()) return nullptr;
    const ComponentDecoder* picked = nullptr;
    for (const auto& entry : it->second) {
        if (entry.version <= version && entry.decoder) {
            picked = &entry.decoder;
        }
        if (version < entry.version) break;
    }
    return picked;
}

bool ComponentMigrationBridge::hasType(std::string_view typeName) const noexcept {
    return codecs_.find(std::string(typeName)) != codecs_.end();
}

std::size_t ComponentMigrationBridge::codecCount() const noexcept {
    std::size_t total = 0;
    for (const auto& [_, list] : codecs_) total += list.size();
    return total;
}

bool ComponentMigrationBridge::encode(std::string_view typeName,
                                     SchemaVersion version,
                                     const void* opaque,
                                     Record& dst) const {
    const auto* enc = findEncoder(typeName, version);
    if (enc == nullptr || !*enc) return false;
    dst.typeName       = std::string{typeName};
    dst.sourceVersion  = version;
    return (*enc)(opaque, dst);
}

bool ComponentMigrationBridge::decode(const Record& src,
                                     void* opaque) const {
    const auto* dec = findDecoder(src.typeName, src.sourceVersion);
    if (dec == nullptr || !*dec) return false;
    return (*dec)(src, opaque);
}

} // namespace threadmaxx::migration
