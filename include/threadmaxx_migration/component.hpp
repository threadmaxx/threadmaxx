#pragma once

/// @file component.hpp
/// @brief M7 — `ComponentCodec` registry. Game code plugs per-type-name
/// encoders + decoders so non-engine custom components can flow
/// through the migration pipeline.
///
/// Each codec is keyed by (typeName, schemaVersion). When the
/// pipeline decodes a Record, the bridge picks the highest-version
/// codec ≤ the record's `sourceVersion`. Encoders rewrite outbound
/// records (M6 + game) using the codec for the OUTBOUND version.
///
/// Codec functions are deliberately opaque: encoders produce raw
/// bytes into a Record, decoders consume them. The migration library
/// never sees the C++ types — game code casts via reinterpret_cast or
/// std::memcpy on its own side.

#include "records.hpp"
#include "version.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace threadmaxx::migration {

/// @brief Encoder: rewrite a Record from in-memory game state. The
/// callback receives an `opaque` pointer the game side cast to its
/// concrete type. Returns true on success.
using ComponentEncoder = std::function<bool(const void* opaque,
                                            Record& dst)>;

/// @brief Decoder: read a Record into in-memory game state. The
/// callback receives a writable `opaque` pointer the game side cast
/// to its concrete type. Returns true on success.
using ComponentDecoder = std::function<bool(const Record& src,
                                            void* opaque)>;

class ComponentMigrationBridge {
public:
    /// @brief Register a codec for (typeName, schemaVersion). Both
    /// callbacks may be null — null means "no codec available in
    /// that direction." Re-registering the same key overwrites.
    void registerCodec(std::string typeName,
                       SchemaVersion version,
                       ComponentEncoder encoder,
                       ComponentDecoder decoder);

    /// @brief Pick the highest-version codec ≤ @p version for
    /// @p typeName. Returns nullptr if no codec is registered for
    /// the type, or if every registered version is above @p version.
    [[nodiscard]] const ComponentEncoder*
    findEncoder(std::string_view typeName,
                SchemaVersion version) const noexcept;
    [[nodiscard]] const ComponentDecoder*
    findDecoder(std::string_view typeName,
                SchemaVersion version) const noexcept;

    /// @brief True if any codec (any version, any direction) is
    /// registered for @p typeName.
    [[nodiscard]] bool hasType(std::string_view typeName) const noexcept;

    /// @brief Total registered codec entries (sum across types).
    [[nodiscard]] std::size_t codecCount() const noexcept;

    /// @brief Encode @p value into @p dst using the codec that
    /// matches @p version. Returns false if no encoder is registered
    /// or the encoder itself returns false.
    [[nodiscard]] bool encode(std::string_view typeName,
                              SchemaVersion version,
                              const void* opaque,
                              Record& dst) const;

    /// @brief Decode @p src into @p opaque. Returns false if no
    /// decoder is registered for the record's typeName at its
    /// sourceVersion (or below), or the decoder returns false.
    [[nodiscard]] bool decode(const Record& src,
                              void* opaque) const;

private:
    struct VersionedCodec {
        SchemaVersion    version{};
        ComponentEncoder encoder;
        ComponentDecoder decoder;
    };

    // typeName → sorted list of versioned codecs (ascending version).
    std::map<std::string, std::vector<VersionedCodec>> codecs_;
};

} // namespace threadmaxx::migration
