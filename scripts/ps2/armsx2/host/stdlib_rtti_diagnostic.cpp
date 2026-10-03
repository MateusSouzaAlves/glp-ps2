// SPDX-License-Identifier: MIT
// Standalone toolchain diagnostic, no EmuOrbit/core/lifecycle code included.
// Preserves the NDK 29/Clang 21 vptr report on an ordinary make_shared object.
#include <memory>
#include <cstdio>
struct StandardObject { int value = 7; };
int main() {
    auto object = std::make_shared<StandardObject>();
    if (object->value != 7) return 1;
    std::puts("STDLIB_SHARED_PTR_PASS");
}
