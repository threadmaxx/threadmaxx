/// @file test_reflect_registry_concurrent.cpp
/// @brief Stress: N reader threads finding while a writer registers.
/// Race-clean under TSAN; readers never see torn `TypeInfo*` pointers
/// thanks to shared_mutex.

#include "Check.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/registry.hpp>

namespace {

struct A { int a; };
struct B { float b; };
struct C { double c; };
struct D { int d1; int d2; };

THREADMAXX_REFLECT(A, a);
THREADMAXX_REFLECT(B, b);
THREADMAXX_REFLECT(C, c);
THREADMAXX_REFLECT(D, d1, d2);

} // namespace

int main() {
    using namespace threadmaxx::reflect;

    TypeRegistry reg;
    auto* aInfo = reg.registerType<A>("A");
    CHECK(aInfo != nullptr);

    std::atomic<bool> stop{false};
    std::atomic<int>  successfulReads{0};

    constexpr int kReaderCount = 4;
    std::vector<std::thread> readers;
    readers.reserve(kReaderCount);
    for (int i = 0; i < kReaderCount; ++i) {
        readers.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                if (auto* p = reg.find("A"); p == aInfo) {
                    successfulReads.fetch_add(1, std::memory_order_relaxed);
                }
                if (reg.find(std::type_index(typeid(A))) == aInfo) {
                    successfulReads.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    // Register more types while readers are active.
    reg.registerType<B>("B");
    reg.registerType<C>("C");
    reg.registerType<D>("D");

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    stop.store(true, std::memory_order_relaxed);
    for (auto& t : readers) t.join();

    CHECK(successfulReads.load() > 0);
    CHECK_EQ(reg.size(), 4u);
    CHECK(reg.find("D") != nullptr);

    EXIT_WITH_RESULT();
}
