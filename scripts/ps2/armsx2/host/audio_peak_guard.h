// SPDX-License-Identifier: MIT
// Stereo-linked protection BEFORE the frontend's float -> signed PCM clamp.
// This cannot reconstruct clipping already present in the guest mixer.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace emuorbit::armsx2 {
class AudioPeakGuard {
public:
    // 5 ms at the pinned 48 kHz. Fixed storage: no frame/VM wait or allocation.
    static constexpr size_t kLookAheadFrames = 240;
private:
    float gain_ = 1.0f;
    std::array<float, kLookAheadFrames * 2> delayed_{};
    struct Peak { uint64_t frame; float value; };
    std::array<Peak, kLookAheadFrames + 1> peaks_{};
    uint64_t frame_ = 0;
    size_t head_ = 0, count_ = 0;
public:
    void reset() { gain_ = 1.0f; delayed_.fill(0); head_ = count_ = 0; frame_ = 0; }
    void convert(const float* input, int16_t* output, size_t frames) {
        constexpr float headroom = .94f, targetCeiling = .88f, ceiling = .92f;
        // Smooth attack before the peak reaches output. Sliding maximum is
        // amortized O(1); 250 ms release avoids following every half-wave.
        constexpr float attack = 1.0f / 12.0f, release = 1.0f / 12000.0f;
        for (size_t frame = 0; frame < frames; ++frame) {
            const float left = std::isfinite(input[frame * 2]) ? input[frame * 2] * headroom : 0.0f;
            const float right = std::isfinite(input[frame * 2 + 1]) ? input[frame * 2 + 1] * headroom : 0.0f;
            const float peak = std::max(std::abs(left), std::abs(right));
            while (count_ && frame_ >= kLookAheadFrames && peaks_[head_].frame < frame_ - kLookAheadFrames) {
                head_ = (head_ + 1) % peaks_.size(); --count_;
            }
            while (count_ && peaks_[(head_ + count_ - 1) % peaks_.size()].value <= peak) --count_;
            peaks_[(head_ + count_) % peaks_.size()] = {frame_, peak}; ++count_;
            const float maximum = peaks_[head_].value;
            const float target = maximum > targetCeiling ? targetCeiling / maximum : 1.0f;
            gain_ += (target - gain_) * (target < gain_ ? attack : release);
            const size_t index = (frame_ % kLookAheadFrames) * 2;
            const float outLeft = delayed_[index], outRight = delayed_[index + 1];
            delayed_[index] = left; delayed_[index + 1] = right;
            const float outPeak = std::max(std::abs(outLeft), std::abs(outRight));
            // Linked last-resort bound for malformed/extreme finite values.
            const float safeGain = outPeak > 0 ? std::min(gain_, ceiling / outPeak) : gain_;
            output[frame * 2] = static_cast<int16_t>(std::clamp(outLeft * safeGain, -ceiling, ceiling) * 32767.0f);
            output[frame * 2 + 1] = static_cast<int16_t>(std::clamp(outRight * safeGain, -ceiling, ceiling) * 32767.0f);
            ++frame_;
        }
    }
};
}
