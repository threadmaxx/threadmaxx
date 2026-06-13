/// @file panels/ReflectPanel.cpp
/// @brief ST22 — `ReflectPanel` implementation.

#include <threadmaxx_studio/panels/reflect.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx_reflect/registry.hpp>
#include <threadmaxx_reflect/type_info.hpp>

#include <cstdio>

namespace threadmaxx::studio {

ReflectPanel::ReflectPanel(const reflect::TypeRegistry& reg) noexcept
    : reg_(&reg) {}

void ReflectPanel::selectType(std::string_view name) {
    selected_.assign(name.begin(), name.end());
}

void ReflectPanel::render(editor::IEditorBackend& backend,
                          IStudioDataSource&) {
    if (reg_ == nullptr) {
        backend.drawText("Reflect: <detached>", 0.0f, 0.0f);
        lastTypes_  = 0;
        lastFields_ = 0;
        return;
    }
    const auto types = reg_->all();
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Reflect  types=%zu", types.size());
    backend.drawText(buf, 0.0f, 0.0f);

    lastTypes_ = 0;
    float y = 16.0f;
    const reflect::TypeInfo* hit = nullptr;
    for (const auto* ti : types) {
        if (ti == nullptr) continue;
        std::snprintf(buf, sizeof(buf), "%-32.32s  size=%u  align=%u",
                      std::string(ti->name).c_str(),
                      ti->sizeBytes, ti->alignBytes);
        backend.drawText(buf, 0.0f, y);
        y += 14.0f;
        ++lastTypes_;
        if (!selected_.empty() && ti->name == selected_) hit = ti;
    }

    lastFields_ = 0;
    if (hit == nullptr) return;
    y += 4.0f;
    std::snprintf(buf, sizeof(buf), "Fields of %s:",
                  std::string(hit->name).c_str());
    backend.drawText(buf, 0.0f, y);
    y += 16.0f;
    for (const auto& f : hit->fields) {
        std::snprintf(buf, sizeof(buf),
                      "  %-16.16s  %-12.12s  off=%u  sz=%u",
                      std::string(f.name).c_str(),
                      std::string(f.typeName).c_str(),
                      f.offset, f.sizeBytes);
        backend.drawText(buf, 0.0f, y);
        y += 14.0f;
        ++lastFields_;
    }
}

} // namespace threadmaxx::studio
