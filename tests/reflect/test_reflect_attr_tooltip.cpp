/// @file test_reflect_attr_tooltip.cpp
/// @brief Tooltip / Units / ReadOnly / Step attributes round-trip
/// their string payloads through the registry.

#include "Check.hpp"

#include <threadmaxx_reflect/attributes.hpp>
#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/registry.hpp>

namespace {
struct Movement { float speed; float gravity; float jumpHeight; };
THREADMAXX_REFLECT(Movement, speed, gravity, jumpHeight);
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    TypeRegistry reg;
    auto* m = reg.registerType<Movement>("Movement");

    reg.addFieldAttribute(m, "speed", Tooltip{"walking speed in m/s"});
    reg.addFieldAttribute(m, "speed", Units{"m/s"});
    reg.addFieldAttribute(m, "gravity", ReadOnly{});
    reg.addFieldAttribute(m, "jumpHeight", Step{0.05});

    const auto* speed = m->findField("speed");
    CHECK_EQ(speed->attributes.size(), 2u);
    CHECK_EQ(speed->attributes[0].name, std::string_view{"Tooltip"});
    CHECK_EQ(speed->attributes[0].payload, std::string_view{"walking speed in m/s"});
    CHECK_EQ(speed->attributes[1].name, std::string_view{"Units"});
    CHECK_EQ(speed->attributes[1].payload, std::string_view{"m/s"});

    const auto* gravity = m->findField("gravity");
    CHECK_EQ(gravity->attributes.size(), 1u);
    CHECK_EQ(gravity->attributes[0].name, std::string_view{"ReadOnly"});
    CHECK_EQ(gravity->attributes[0].payload, std::string_view{}); // empty

    const auto* jump = m->findField("jumpHeight");
    CHECK_EQ(jump->attributes.size(), 1u);
    CHECK_EQ(jump->attributes[0].name, std::string_view{"Step"});
    CHECK_EQ(jump->attributes[0].payload, std::string_view{"0.05"});

    EXIT_WITH_RESULT();
}
