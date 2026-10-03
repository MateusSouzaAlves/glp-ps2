// SPDX-License-Identifier: MIT
#include "cpu_reply.h"
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <thread>

static void require(bool value) { if (!value) std::abort(); }
int main() {
    using namespace emuorbit::armsx2;
    using namespace std::chrono_literals;
    auto complete = std::make_shared<CpuReply>();
    std::thread worker([complete] { complete->apply([] { return CpuResult{EMUORBIT_ARMS_OK, 2, 1234567890123ULL}; }); });
    const auto result = complete->wait(1000ms);
    worker.join();
    require(result.status == EMUORBIT_ARMS_OK && result.state == 2 && result.cycles == 1234567890123ULL);

    auto late = std::make_shared<CpuReply>();
    require(late->wait(1ms).status == EMUORBIT_ARMS_TIMEOUT);
    bool ran = false;
    late->apply([&ran] { ran = true; return CpuResult{1, 1, 1}; });
    require(!ran); // Queued but not started when the deadline expired.

    std::promise<void> entered, release;
    auto released = release.get_future();
    auto blocked = std::make_shared<CpuReply>();
    std::weak_ptr<CpuReply> retained = blocked;
    std::thread blockedWorker([blocked, &entered, &released] {
        blocked->apply([&entered, &released] {
            entered.set_value(); released.wait(); return CpuResult{1, 2, 5};
        });
    });
    entered.get_future().wait();
    const auto start = std::chrono::steady_clock::now();
    require(blocked->wait(10ms).status == EMUORBIT_ARMS_TIMEOUT);
    require(std::chrono::steady_clock::now() - start < 1000ms);
    blocked.reset();
    require(!retained.expired()); // Running work owns its reply, not caller stack.
    release.set_value(); blockedWorker.join();
    require(retained.expired());
    auto exception = std::make_shared<CpuReply>();
    std::thread throwingWorker([exception] {
        exception->apply([]() -> CpuResult { throw std::bad_alloc(); });
    });
    require(exception->wait(1000ms).status == EMUORBIT_ARMS_FAILED);
    throwingWorker.join();
    std::puts("CPU_REPLY_PASS completed/queued_late/blocked_deadline/closure_lifetime/cpu_exception scope=REPLY_NOT_VM_LIFECYCLE");
}
