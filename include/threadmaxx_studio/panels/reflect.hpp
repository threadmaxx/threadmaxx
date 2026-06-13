#pragma once

/// @file panels/reflect.hpp
/// @brief ST22 — `ReflectPanel` browses a `reflect::TypeRegistry`.
/// Header row + one row per registered type; when a type is
/// selected, additional rows enumerate its fields (name + type +
/// offset + size). No sibling prep — reflect v1.0 surfaces are
/// sufficient.

#include "../panel.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace threadmaxx::reflect {
class TypeRegistry;
struct TypeInfo;
} // namespace threadmaxx::reflect

namespace threadmaxx::studio {

class ReflectPanel : public IStudioPanel {
public:
    ReflectPanel() noexcept = default;
    explicit ReflectPanel(const reflect::TypeRegistry& reg) noexcept;

    void setRegistry(const reflect::TypeRegistry* reg) noexcept { reg_ = reg; }
    [[nodiscard]] const reflect::TypeRegistry* registry() const noexcept {
        return reg_;
    }

    /// @brief Highlight a type by name. `render()` expands it into
    /// one row per field after the type-list section. Empty string
    /// (the default) collapses the field section.
    void selectType(std::string_view name);
    [[nodiscard]] std::string_view selectedType() const noexcept {
        return selected_;
    }

    std::string_view id() const noexcept override {
        return "sibling.reflect";
    }
    std::string_view title() const noexcept override { return "Reflect"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    [[nodiscard]] std::size_t typeRowCount() const noexcept { return lastTypes_; }
    [[nodiscard]] std::size_t fieldRowCount() const noexcept { return lastFields_; }

private:
    const reflect::TypeRegistry* reg_{nullptr};
    std::string                  selected_;
    std::size_t                  lastTypes_{0};
    std::size_t                  lastFields_{0};
};

} // namespace threadmaxx::studio
