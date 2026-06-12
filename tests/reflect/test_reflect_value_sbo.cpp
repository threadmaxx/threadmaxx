/// @file test_reflect_value_sbo.cpp
/// @brief `Value` round-trips every supported primitive and reports
/// type mismatch cleanly. SBO budget pinned.

#include "Check.hpp"

#include <cstdint>

#include <threadmaxx_reflect/value.hpp>

int main() {
    using namespace threadmaxx::reflect;

    static_assert(sizeof(Value) <= 48);

    {
        Value v = Value::make<int>(42);
        int out = 0;
        CHECK(v.get(out));
        CHECK_EQ(out, 42);
        CHECK(v.is<int>());
        CHECK(!v.is<float>());
    }

    {
        Value v = Value::make<float>(3.5f);
        float out = 0.0f;
        CHECK(v.get(out));
        CHECK_EQ(out, 3.5f);
        double mismatch = 0.0;
        CHECK(!v.get(mismatch)); // wrong type
    }

    {
        Value v = Value::make<bool>(true);
        bool out = false;
        CHECK(v.get(out));
        CHECK_EQ(out, true);
    }

    {
        Value v = Value::make<std::uint64_t>(0xDEADBEEFCAFEBABEull);
        std::uint64_t out = 0;
        CHECK(v.get(out));
        CHECK_EQ(out, 0xDEADBEEFCAFEBABEull);
    }

    // Empty Value -> can't get anything.
    {
        Value e;
        CHECK(e.empty());
        int x = 0;
        CHECK(!e.get(x));
    }

    EXIT_WITH_RESULT();
}
