// SPDX-License-Identifier: MIT
// Stereo-linked protection BEFORE the frontend's float -> signed PCM clamp.
// This cannot reconstruct clipping already present in the guest mixer.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace emuorbit::armsx2 {
class AudioPeakGuard {
    float gain_ = 1.0f;
public:
    void reset() { gain_ = 1.0f; }
    void convert(const float* input, int16_t* output, size_t frames) {
        constexpr float headroom = .94f, ceiling = .92f;
        // 100 ms release at the pinned 48 kHz; immediate attack avoids overshoot.
        constexpr float release = 1.0f / 4800.0f;
        for (size_t frame = 0; frame < frames; ++frame) {
            const float left = std::isfinite(input[frame * 2]) ? input[frame * 2] * headroom : 0.0f;
            const float right = std::isfinite(input[frame * 2 + 1]) ? input[frame * 2 + 1] * headroom : 0.0f;
            const float peak = std::max(std::abs(left), std::abs(right));
            const float target = peak > ceiling ? ceiling / peak : 1.0f;
            gain_ = std::min(target, gain_ + (1.0f - gain_) * release);
            output[frame * 2] = static_cast<int16_t>(std::clamp(left * gain_, -ceiling, ceiling) * 32767.0f);
            output[frame * 2 + 1] = static_cast<int16_t>(std::clamp(right * gain_, -ceiling, ceiling) * 32767.0f);
        }
    }
};
}
