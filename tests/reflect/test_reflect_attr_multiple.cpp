/// @file test_reflect_attr_multiple.cpp
/// @brief Multiple attributes per field stack in insertion order and
/// preserve their order across vector growth.

#include "Check.hpp"

#include <threadmaxx_reflect/attributes.hpp>
#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/registry.hpp>

namespace {
struct Volume { float master; float music; float sfx; };
THREADMAXX_REFLECT(Volume, master, music, sfx);
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    TypeRegistry reg;
    auto* v = reg.registerType<Volume>("Volume");

    // Stack 8 attributes onto master to force vector reallocation.
    reg.addFieldAttribute(v, "master", Range{0.0, 1.0});
    reg.addFieldAttribute(v, "master", Tooltip{"master volume"});
    reg.addFieldAttribute(v, "master", Units{"linear"});
    reg.addFieldAttribute(v, "master", Step{0.01});
    reg.addFieldAttribute(v, "master", ReadOnly{});
    reg.addFieldAttribute(v, "master", Hidden{});
    reg.addFieldAttribute(v, "master", Range{0.1, 0.9});
    reg.addFieldAttribute(v, "master", Tooltip{"updated"});

    const auto* master = v->findField("master");
    CHECK_EQ(master->attributes.size(), 8u);
    CHECK_EQ(master->attributes[0].name, std::string_view{"Range"});
    CHECK_EQ(master->attributes[0].payload, std::string_view{"0,1"});
    CHECK_EQ(master->attributes[1].name, std::string_view{"Tooltip"});
    CHECK_EQ(master->attributes[1].payload, std::string_view{"master volume"});
    CHECK_EQ(master->attributes[6].name, std::string_view{"Range"});
    CHECK_EQ(master->attributes[6].payload, std::string_view{"0.1,0.9"});
    CHECK_EQ(master->attributes[7].payload, std::string_view{"updated"});

    EXIT_WITH_RESULT();
}
