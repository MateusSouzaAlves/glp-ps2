// SPDX-License-Identifier: MIT. Pure input ABI proof, not touch/Bluetooth QA.
#include "input_snapshot.h"
#include <cstdio>

int main() {
    using emuorbit::armsx2::InputSnapshot;
    InputSnapshot snapshot;
    for (unsigned bit = 0; bit < 16; ++bit) {
        if (!snapshot.set(1u << bit, 0, 0, 0, 0)) return 1;
        for (unsigned id = 0; id < 16; ++id)
            if (snapshot.read(0, RETRO_DEVICE_JOYPAD, 0, id) != (id == bit ? 1 : 0)) return 2;
        const int16_t expected = bit == 15 ? -32768 : static_cast<int16_t>(1u << bit);
        if (snapshot.read(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK) != expected) return 3;
    }
    if (!snapshot.set(65535, -32768, 32767, -1234, 4321)) return 4;
    if (snapshot.read(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK) != -1) return 5;
    const std::array<int16_t, 4> expected{-32768, 32767, -1234, 4321};
    for (unsigned stick = 0; stick < 2; ++stick) for (unsigned axis = 0; axis < 2; ++axis)
        if (snapshot.read(0, RETRO_DEVICE_ANALOG, stick, axis) != expected[stick * 2 + axis]) return 6;
    if (snapshot.set(-1, 0, 0, 0, 0) || snapshot.set(65536, 0, 0, 0, 0)
            || snapshot.set(0, -32769, 0, 0, 0) || snapshot.set(0, 0, 32768, 0, 0)
            || snapshot.set(0, 0, 0, -32769, 0) || snapshot.set(0, 0, 0, 0, 32768)) return 7;
    if (snapshot.read(0, RETRO_DEVICE_ANALOG, 0, 0) != -32768
            || snapshot.read(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK) != -1) return 8;
    for (unsigned invalid : {2u, 255u, 0xffffffffu}) {
        if (snapshot.read(invalid, RETRO_DEVICE_JOYPAD, 0, 0)
                || snapshot.read(0, RETRO_DEVICE_ANALOG, invalid, 0)
                || snapshot.read(0, RETRO_DEVICE_ANALOG, 0, invalid)
                || snapshot.read(0, RETRO_DEVICE_JOYPAD, invalid, 0)) return 9;
    }
    if (snapshot.read(0, RETRO_DEVICE_NONE, 0, 0) || snapshot.read(0, RETRO_DEVICE_KEYBOARD, 0, 0)
            || snapshot.read(0, RETRO_DEVICE_JOYPAD, 0, 0xffffffffu)
            || snapshot.read(1, RETRO_DEVICE_JOYPAD, 0, 0)) return 10;
    snapshot.clear();
    for (unsigned bit = 0; bit < 16; ++bit) if (snapshot.read(0, RETRO_DEVICE_JOYPAD, 0, bit)) return 11;
    for (unsigned stick = 0; stick < 2; ++stick) for (unsigned axis = 0; axis < 2; ++axis)
        if (snapshot.read(0, RETRO_DEVICE_ANALOG, stick, axis)) return 12;
    std::puts("INPUT_SNAPSHOT_PASS 16_bits/mask/two_sticks/endpoints/reject_preserves/reset/unsupported scope=ABI_NOT_TOUCH_BLUETOOTH");
    return 0;
}
