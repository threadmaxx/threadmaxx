// threadmaxx_reflect v1.0 headless smoke. Registers a struct with
// attributes, dumps JSON, applies a Patch, reads back, exits 0.

#include <cstdio>
#include <string>

#include <threadmaxx_reflect/threadmaxx_reflect.hpp>

namespace game {
struct Health { int current; int max; float regen; bool alive; };
THREADMAXX_REFLECT(Health, current, max, regen, alive);
} // namespace game

int main() {
    using namespace threadmaxx::reflect;

    std::printf("threadmaxx_reflect v%s\n",
                std::string(version_string()).c_str());

    TypeRegistry reg;
    auto* h = reg.registerType<game::Health>("Health");
    reg.addFieldAttribute(h, "current", Range{0.0, 100.0});
    reg.addFieldAttribute(h, "regen",   Units{"hp/s"});

    std::printf("registered Health with %zu fields\n", h->fields.size());
    for (const auto& field : h->fields) {
        std::printf("  - %s (%s, offset=%u)\n",
                    std::string(field.name).c_str(),
                    std::string(field.typeName).c_str(),
                    field.offset);
        for (const auto& attr : field.attributes) {
            std::printf("      attr %s = %s\n",
                        std::string(attr.name).c_str(),
                        std::string(attr.payload).c_str());
        }
    }

    game::Health body{42, 100, 1.5f, true};
    const std::string j = to_json(h, &body);
    std::printf("to_json: %s\n", j.c_str());

    // Round-trip through from_json into a fresh body.
    game::Health restored{};
    auto rc = from_json(h, &restored, j);
    if (!rc.ok()) {
        std::fprintf(stderr, "from_json failed\n");
        return 1;
    }
    if (restored.current != body.current || restored.alive != body.alive) {
        std::fprintf(stderr, "round-trip mismatch\n");
        return 1;
    }

    // Patch a few fields.
    Patch p;
    p.entries.push_back({"current", Value::make<int>(7)});
    p.entries.push_back({"alive",   Value::make<bool>(false)});
    auto rc2 = applyPatch(h, &restored, p);
    if (!rc2.ok()) {
        std::fprintf(stderr, "applyPatch failed\n");
        return 1;
    }
    std::printf("patched: current=%d alive=%d\n",
                restored.current, static_cast<int>(restored.alive));

    // Read a field back as Value.
    auto rd = readField(h, &restored, "max");
    int outMax = 0;
    if (!rd.ok() || !rd.value.get(outMax)) {
        std::fprintf(stderr, "readField failed\n");
        return 1;
    }
    std::printf("readField max = %d\n", outMax);

    std::printf("reflect_demo done\n");
    return 0;
}
