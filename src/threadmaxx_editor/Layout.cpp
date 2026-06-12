/// @file Layout.cpp
/// @brief LayoutManager save/load.

#include "threadmaxx_editor/layout.hpp"

#include <istream>
#include <ostream>
#include <sstream>
#include <string>

namespace threadmaxx::editor {

namespace {

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') { out += "\\\\"; }
        else if (c == '\n') { out += "\\n"; }
        else { out += c; }
    }
    return out;
}

std::string unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            if (s[i + 1] == '\\') { out += '\\'; ++i; }
            else if (s[i + 1] == 'n') { out += '\n'; ++i; }
            else { out += s[i]; }
        } else {
            out += s[i];
        }
    }
    return out;
}

} // namespace

void LayoutManager::save(std::ostream& out) const {
    out << "selectedPanel=" << escape(state_.selectedPanel) << "\n";
    out << "dockJson=" << escape(state_.dockJson) << "\n";
    for (const auto& [name, visible] : state_.panels) {
        out << "panel:" << escape(name) << "=" << (visible ? "1" : "0") << "\n";
    }
}

bool LayoutManager::load(std::istream& in) {
    LayoutState fresh{};
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) return false;
        const std::string key = line.substr(0, eq);
        const std::string val = unescape(line.substr(eq + 1));
        if (key == "selectedPanel") {
            fresh.selectedPanel = val;
        } else if (key == "dockJson") {
            fresh.dockJson = val;
        } else if (key.rfind("panel:", 0) == 0) {
            fresh.panels.emplace(unescape(key.substr(6)),
                                 val == "1");
        } else {
            return false;
        }
    }
    state_ = std::move(fresh);
    return true;
}

} // namespace threadmaxx::editor
