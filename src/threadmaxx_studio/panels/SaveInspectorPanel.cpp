/// @file panels/SaveInspectorPanel.cpp
/// @brief ST35 — SaveInspectorPanel implementation.

#include <threadmaxx_studio/panels/save_inspector.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx_migration/records.hpp>
#include <threadmaxx_migration/savefile.hpp>

#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace threadmaxx::studio {

namespace {

bool recordsEqual(const threadmaxx::migration::Record& a,
                  const threadmaxx::migration::Record& b) noexcept {
    if (a.typeName != b.typeName) return false;
    if (a.fields.size() != b.fields.size()) return false;
    for (std::size_t i = 0; i < a.fields.size(); ++i) {
        if (a.fields[i].name != b.fields[i].name) return false;
        if (a.fields[i].value.bytes.size() != b.fields[i].value.bytes.size()) {
            return false;
        }
        if (!a.fields[i].value.bytes.empty() &&
            std::memcmp(a.fields[i].value.bytes.data(),
                        b.fields[i].value.bytes.data(),
                        a.fields[i].value.bytes.size()) != 0) {
            return false;
        }
    }
    return true;
}

} // namespace

void SaveInspectorPanel::setLoadedSave(
        const threadmaxx::migration::RecordSet* set) noexcept {
    loaded_ = set;
}

void SaveInspectorPanel::setCurrentSave(
        const threadmaxx::migration::RecordSet* set) noexcept {
    current_ = set;
}

SaveInspectorPanel::DiffSummary SaveInspectorPanel::diff() const {
    DiffSummary summary{};
    if (loaded_ == nullptr || current_ == nullptr) return summary;

    std::unordered_map<std::uint64_t, const threadmaxx::migration::Record*>
        currentById;
    currentById.reserve(current_->records.size());
    for (const auto& rec : current_->records) {
        currentById.emplace(rec.stableId, &rec);
    }

    std::unordered_map<std::uint64_t, bool> consumed;
    for (const auto& rec : loaded_->records) {
        auto it = currentById.find(rec.stableId);
        if (it == currentById.end()) {
            ++summary.removed;
            continue;
        }
        consumed[rec.stableId] = true;
        if (recordsEqual(rec, *it->second)) {
            ++summary.unchanged;
        } else {
            ++summary.changed;
        }
    }
    for (const auto& [id, rec] : currentById) {
        if (consumed.find(id) == consumed.end()) {
            ++summary.added;
        }
    }
    return summary;
}

void SaveInspectorPanel::render(editor::IEditorBackend& backend,
                                IStudioDataSource&) {
    char buf[200];
    if (loaded_ == nullptr) {
        backend.drawText("Save Inspector: <no save loaded>", 0.0f, 0.0f);
        lastRows_ = 1;
        return;
    }

    const auto& m = loaded_->metadata;
    std::snprintf(buf, sizeof(buf),
                  "Save Inspector  records=%zu  schema=%u.%u.%u  format=%u",
                  loaded_->records.size(),
                  m.schemaVersion.major,
                  m.schemaVersion.minor,
                  m.schemaVersion.patch,
                  m.formatVersion.value);
    backend.drawText(buf, 0.0f, 0.0f);

    std::snprintf(buf, sizeof(buf),
                  "product=%s  build=%s",
                  m.productName.c_str(), m.buildId.c_str());
    backend.drawText(buf, 0.0f, 14.0f);

    std::snprintf(buf, sizeof(buf),
                  "commitHash=0x%016llx",
                  static_cast<unsigned long long>(m.commitHash));
    backend.drawText(buf, 0.0f, 28.0f);

    float y = 46.0f;
    std::size_t shown = 0;
    for (const auto& rec : loaded_->records) {
        if (shown >= maxRows_) break;
        std::snprintf(buf, sizeof(buf),
                      "  id=%llu  type=%-16s  fields=%zu  v=%u.%u.%u",
                      static_cast<unsigned long long>(rec.stableId),
                      rec.typeName.c_str(),
                      rec.fields.size(),
                      rec.sourceVersion.major,
                      rec.sourceVersion.minor,
                      rec.sourceVersion.patch);
        backend.drawText(buf, 0.0f, y);
        y += 14.0f;
        ++shown;
    }

    if (current_ != nullptr) {
        const auto d = diff();
        std::snprintf(buf, sizeof(buf),
                      "diff vs current: +%zu / -%zu / ~%zu  (unchanged=%zu)",
                      d.added, d.removed, d.changed, d.unchanged);
        backend.drawText(buf, 0.0f, y);
        y += 14.0f;
    }

    lastRows_ = 3 + shown + (current_ != nullptr ? 1u : 0u);
}

} // namespace threadmaxx::studio
