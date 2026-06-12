/// @file test_reflect_visit_macro.cpp
/// @brief Visitors work on macro-registered types: PrintVisitor uses
/// the field name; HashVisitor is order-stable.

#include "Check.hpp"

#include <string>

#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/visit.hpp>

namespace {
struct Stats { int level; float xp; double mana; };
THREADMAXX_REFLECT(Stats, level, xp, mana);
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    Stats s{42, 100.5f, 7.25};
    std::string buf;
    visit_fields(s, PrintVisitor(buf));
    CHECK(buf.find("level=42") != std::string::npos);
    CHECK(buf.find("xp=") != std::string::npos);
    CHECK(buf.find("mana=") != std::string::npos);

    // Hash is deterministic for the same input.
    HashVisitor h1, h2;
    visit_fields(s, h1);
    visit_fields(s, h2);
    CHECK_EQ(h1.hash(), h2.hash());

    EXIT_WITH_RESULT();
}
