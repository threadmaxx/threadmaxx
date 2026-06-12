/// @file test_reflect_equals_visitor.cpp
/// @brief `fields_equal` returns true exactly when every field
/// matches. Floating-point edge: NaN != NaN at the byte level too
/// (because the bit patterns can differ; this is a hash-based
/// equality, not a numerical one).

#include "Check.hpp"

#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/visit.hpp>

namespace {
struct Point { int x; int y; };
THREADMAXX_REFLECT(Point, x, y);
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    Point a{1, 2};
    Point b{1, 2};
    Point c{1, 3};

    CHECK(fields_equal(a, b));
    CHECK(!fields_equal(a, c));

    // Self equals self.
    CHECK(fields_equal(a, a));

    EXIT_WITH_RESULT();
}
