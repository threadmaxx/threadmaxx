// threadmaxx_reflect headless smoke. Pre-R6 (no JSON binder yet) this
// just walks the aggregate path; the v1.0 close-out batch extends it
// to register a struct, dump JSON, and apply a Patch.

#include <cstdio>
#include <string>

#include <threadmaxx_reflect/aggregate.hpp>
#include <threadmaxx_reflect/version.hpp>

namespace {
struct Pos { float x; float y; float z; };
} // namespace

int main() {
    std::printf("threadmaxx_reflect v%s\n",
                std::string(threadmaxx::reflect::version_string()).c_str());

    constexpr auto N = threadmaxx::reflect::field_count<Pos>();
    static_assert(N == 3, "Pos should have 3 fields");

    Pos p{1.0f, 2.0f, 3.0f};
    int seen = 0;
    threadmaxx::reflect::for_each_field(p, [&](std::size_t i, auto& v) {
        std::printf("  field[%zu] = %g\n", i, static_cast<double>(v));
        ++seen;
    });
    if (seen != static_cast<int>(N)) return 1;

    std::printf("reflect_demo done\n");
    return 0;
}
