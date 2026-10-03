// SPDX-License-Identifier: MIT
// Heap-owned reply for asynchronous CPU work with a bounded frontend wait.
// Timeout does not certify cancellation or permit releasing native resources.
#pragma once
#include "host_api.h"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace emuorbit::armsx2 {
struct CpuResult { int status; unsigned state; uint64_t cycles; };
class CpuReply final {
public:
    template<class Action> void apply(Action action) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (abandoned_) return;
        }
        // Never hold the reply mutex across CPU/GS work. A blocked action
        // must not turn wait_for into an unbounded wait on this mutex.
        CpuResult value;
#ifdef __cpp_exceptions
        try { value = action(); }
        catch (...) { value = {EMUORBIT_ARMS_FAILED, 0, 0}; }
        // Enabled host/probe builds report exceptions as terminal. The pinned
        // core uses -fno-exceptions: do NOT imply it can recover allocation
        // failure; product state work still needs memory admission checks.
#else
        value = action();
#endif
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (abandoned_) return;
            value_ = value;
            complete_ = true;
        }
        cv_.notify_all();
    }
    CpuResult wait(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [this] { return complete_; })) {
            abandoned_ = true;
            return {EMUORBIT_ARMS_TIMEOUT, 0, 0};
        }
        return value_;
    }
private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool abandoned_ = false, complete_ = false;
    CpuResult value_{EMUORBIT_ARMS_NOT_READY, 0, 0};
};
} // namespace emuorbit::armsx2
