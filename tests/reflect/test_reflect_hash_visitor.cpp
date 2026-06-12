/// @file test_reflect_hash_visitor.cpp
/// @brief HashVisitor produces stable FNV-1a-64 fingerprints across
/// equal objects; different-by-any-field objects diverge.

#include "Check.hpp"

#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/visit.hpp>

namespace {
struct V3 { float x; float y; float z; };
THREADMAXX_REFLECT(V3, x, y, z);
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    V3 a{1, 2, 3};
    V3 b{1, 2, 3};
    V3 c{1, 2, 4}; // differs by z

    HashVisitor ha, hb, hc;
    visit_fields(a, ha);
    visit_fields(b, hb);
    visit_fields(c, hc);

    CHECK_EQ(ha.hash(), hb.hash());
    CHECK(ha.hash() != hc.hash());

    // Sanity: hash isn't zero (would imply we never mixed anything).
    CHECK(ha.hash() != 0ull);

    EXIT_WITH_RESULT();
}
