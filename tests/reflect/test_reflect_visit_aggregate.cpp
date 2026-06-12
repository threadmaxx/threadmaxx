/// @file test_reflect_visit_aggregate.cpp
/// @brief `visit_fields` walks aggregates (no macro) — PrintVisitor
/// renders `field_i=v` strings; HashVisitor produces a stable hash.

#include "Check.hpp"

#include <string>

#include <threadmaxx_reflect/visit.hpp>

namespace {
struct Pos { float x; float y; float z; };
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    Pos p{1.0f, 2.0f, 3.0f};

    std::string s;
    PrintVisitor pv(s);
    visit_fields(p, pv);
    CHECK(s.find("field_0=") != std::string::npos);
    CHECK(s.find("field_2=") != std::string::npos);

    HashVisitor hv;
    visit_fields(p, hv);
    const auto h1 = hv.hash();

    Pos p2 = p;
    HashVisitor hv2;
    visit_fields(p2, hv2);
    CHECK_EQ(h1, hv2.hash());

    // Different values -> different hash.
    p2.x = 99.0f;
    HashVisitor hv3;
    visit_fields(p2, hv3);
    CHECK(hv3.hash() != h1);

    CHECK(fields_equal(p, p));
    CHECK(!fields_equal(p, p2));

    EXIT_WITH_RESULT();
}
