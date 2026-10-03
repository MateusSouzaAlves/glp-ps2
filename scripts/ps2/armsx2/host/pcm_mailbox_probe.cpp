// SPDX-License-Identifier: MIT. Synthetic data only; no core/library/media.
#include "pcm_mailbox.h"
#include <cstdio>

int main() {
    using emuorbit::armsx2::PcmMailbox;
    PcmMailbox mailbox;
    std::array<int16_t, 4096> callback{};
    std::array<int16_t, PcmMailbox::kCapacity * 2> output{};
    if (mailbox.push(nullptr, 1) || mailbox.push(callback.data(), 2049)
            || mailbox.push(callback.data(), static_cast<size_t>(-1))
            || !mailbox.push(nullptr, 0) || mailbox.pending() != 0) return 1;
    for (unsigned block = 1; block <= 5; ++block) {
        callback.fill(static_cast<int16_t>(block));
        if (!mailbox.push(callback.data(), 2048)) return 2;
        callback.fill(-10); // Copies, never retains a callback pointer.
    }
    if (mailbox.metrics().received != 10240 || mailbox.metrics().dropped != 2048
            || mailbox.metrics().maxPending != 8192 || mailbox.metrics().samples != 20480) return 3;
    if (mailbox.drain(nullptr, 8192) != 0 || mailbox.pending() != 8192) return 4;
    if (mailbox.drain(output.data(), 1000) != 1000) return 5;
    for (size_t i = 0; i < 2000; ++i) if (output[i] != 2) return 6;
    if (mailbox.drain(output.data(), 8192) != 7192 || mailbox.pending() != 0) return 7;
    for (size_t i = 0; i < 7192; ++i) {
        const int expected = i < 1048 ? 2 : i < 3096 ? 3 : i < 5144 ? 4 : 5;
        if (output[i * 2] != expected || output[i * 2 + 1] != expected) return 8;
    }
    callback.fill(-32768); if (!mailbox.push(callback.data(), 100)) return 9;
    if (mailbox.metrics().peak != 32768 || mailbox.metrics().clipped != 200) return 10;
    mailbox.discard(); if (mailbox.pending() != 0 || mailbox.metrics().dropped != 2148) return 11;
    callback.fill(32767); if (!mailbox.push(callback.data(), 100)) return 12;
    if (mailbox.metrics().clipped != 400 || mailbox.drain(output.data(), static_cast<size_t>(-1)) != 100) return 13;
    for (size_t i = 0; i < 200; ++i) if (output[i] != 32767) return 14;
    std::puts("PCM_MAILBOX_PASS copy/overflow/wrap/partial/drain/discard/signed_min/clipping bounds=8192 callback=2048 rate=48000 scope=TRANSPORT_NOT_HEARD");
    return 0;
}
