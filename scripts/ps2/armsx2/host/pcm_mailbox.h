// SPDX-License-Identifier: MIT
// Fixed-capacity signed-16 stereo transport. Only the serial native owner may
// push/drain/reset; no retained core pointer, heap allocation or device write.
// Audio and VM clocks remain separate. Saturation is measured, never repaired
// by clipping/gain changes here. Overflow drops OLDEST PCM to bound latency.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace emuorbit::armsx2 {
class PcmMailbox final {
public:
    static constexpr size_t kCapacity = 8192;
    static constexpr size_t kCallbackMax = 2048; // Pinned Main.cpp PullFrames bound.
    static constexpr unsigned kRate = 48000;
    struct Metrics {
        uint64_t received = 0, dropped = 0, samples = 0, clipped = 0;
        unsigned peak = 0;
        size_t maxPending = 0;
    };
    bool push(const int16_t* source, size_t count) noexcept {
        if (count > kCallbackMax || (count && !source)) return false;
        if (!count) return true;
        metrics_.received += count;
        for (size_t i = 0; i < count * 2; ++i) {
            const int value = source[i];
            const unsigned magnitude = static_cast<unsigned>(value < 0 ? -value : value);
            metrics_.peak = std::max(metrics_.peak, magnitude);
            ++metrics_.samples;
            if (value == -32768 || value == 32767) ++metrics_.clipped;
        }
        const size_t excess = count_ + count > kCapacity ? count_ + count - kCapacity : 0;
        head_ = (head_ + excess) % kCapacity; count_ -= excess; metrics_.dropped += excess;
        const size_t tail = (head_ + count_) % kCapacity;
        const size_t first = std::min(count, kCapacity - tail);
        std::memcpy(data_.data() + tail * 2, source, first * 4);
        if (first < count) std::memcpy(data_.data(), source + first * 2, (count - first) * 4);
        count_ += count; metrics_.maxPending = std::max(metrics_.maxPending, count_);
        return true;
    }
    size_t drain(int16_t* destination, size_t capacityFrames) noexcept {
        const size_t count = std::min(capacityFrames, count_);
        if (!destination || !count) return 0;
        const size_t first = std::min(count, kCapacity - head_);
        std::memcpy(destination, data_.data() + head_ * 2, first * 4);
        if (first < count) std::memcpy(destination + first * 2, data_.data(), (count - first) * 4);
        head_ = (head_ + count) % kCapacity; count_ -= count;
        return count;
    }
    void discard() noexcept { metrics_.dropped += count_; head_ = count_ = 0; }
    size_t pending() const noexcept { return count_; }
    const Metrics& metrics() const noexcept { return metrics_; }
private:
    std::array<int16_t, kCapacity * 2> data_{};
    size_t head_ = 0, count_ = 0;
    Metrics metrics_{};
};
} // namespace emuorbit::armsx2
