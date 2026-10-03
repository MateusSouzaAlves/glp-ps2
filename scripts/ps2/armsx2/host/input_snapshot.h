// SPDX-License-Identifier: MIT
// Serial native-owner snapshot of the EXISTING PS2 touch/gamepad reducer.
// Uses pinned Libretro IDs directly: no remapping, pressure emulation or SDL.
#pragma once
#include "libretro.h"
#include <array>
#include <cstdint>

namespace emuorbit::armsx2 {
class InputSnapshot final {
public:
    bool set(int buttons, int lx, int ly, int rx, int ry) noexcept {
        if (buttons < 0 || buttons > 65535 || !axis(lx) || !axis(ly) || !axis(rx) || !axis(ry)) return false;
        buttons_ = static_cast<uint16_t>(buttons);
        axes_ = {static_cast<int16_t>(lx), static_cast<int16_t>(ly), static_cast<int16_t>(rx), static_cast<int16_t>(ry)};
        return true;
    }
    int16_t read(unsigned port, unsigned device, unsigned index, unsigned id) const noexcept {
        if (port != 0) return 0; // Pinned frontend reads one DualShock2.
        if (device == RETRO_DEVICE_JOYPAD && index == 0) {
            if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
                // Defined signed conversion for bit 15/all buttons, no narrowing
                // from a value outside int16_t's range.
                return static_cast<int16_t>(buttons_ <= 32767 ? buttons_ : static_cast<int32_t>(buttons_) - 65536);
            }
            return id <= RETRO_DEVICE_ID_JOYPAD_R3 && (buttons_ & (1u << id)) ? 1 : 0;
        }
        if (device == RETRO_DEVICE_ANALOG && index <= RETRO_DEVICE_INDEX_ANALOG_RIGHT && id <= RETRO_DEVICE_ID_ANALOG_Y)
            return axes_[index * 2 + id];
        return 0; // Unsupported device/index/pressure never becomes a pressed key.
    }
    void clear() noexcept { buttons_ = 0; axes_.fill(0); }
private:
    static bool axis(int value) noexcept { return value >= -32768 && value <= 32767; }
    uint16_t buttons_ = 0;
    std::array<int16_t, 4> axes_{};
};
static_assert(RETRO_DEVICE_INDEX_ANALOG_LEFT == 0 && RETRO_DEVICE_INDEX_ANALOG_RIGHT == 1);
static_assert(RETRO_DEVICE_ID_ANALOG_X == 0 && RETRO_DEVICE_ID_ANALOG_Y == 1);
static_assert(RETRO_DEVICE_ID_JOYPAD_R3 == 15 && RETRO_DEVICE_ID_JOYPAD_MASK == 256);
} // namespace emuorbit::armsx2
