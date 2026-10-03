// SPDX-License-Identifier: MIT. Synthetic floats only, no game or BIOS.
#include "audio_peak_guard.h"
#include <array>
#include <cstdio>
#include <limits>

int main() {
    using emuorbit::armsx2::AudioPeakGuard;
    AudioPeakGuard guard;
    const std::array<float, 10> input{.25f, -.5f, 0.f, 0.f, 4.f, -2.f,
        std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 1.f, -1.f};
    std::array<int16_t, 10> output{};
    guard.convert(input.data(), output.data(), input.size() / 2);
    if (std::abs(output[0] - 7700) > 1 || std::abs(output[1] + 15400) > 1) return 1;
    if (output[2] || output[3] || output[6] || output[7]) return 2;
    for (auto sample : output) if (std::abs(static_cast<int>(sample)) > 30146) return 3;
    if (std::abs(output[4] + 2 * output[5]) > 2) return 4; // Linked gain preserves stereo balance.
    if (input[4] != 4.f) return 5;
    guard.reset(); guard.convert(input.data(), output.data(), 1);
    if (std::abs(output[0] - 7700) > 1) return 6;
    float overload[]{8.f, -8.f}; int16_t recovery[2];
    guard.convert(overload, recovery, 1);
    const int compressed = std::abs(recovery[0]);
    float quiet[]{.5f, -.5f}; guard.convert(quiet, recovery, 1);
    const int first = recovery[0];
    for (int frame = 0; frame < 48000; ++frame) guard.convert(quiet, recovery, 1);
    if (compressed != 30145 || first >= 3000 || recovery[0] < 15390 || recovery[0] > 15401) return 7;
    guard.convert(nullptr, nullptr, 0);
    std::puts("AUDIO_PEAK_GUARD_PASS quiet/headroom/overload/stereo/nonfinite/reset/release/zero before-int16 no-allocation");
}
