// SPDX-License-Identifier: GPL-3.0-or-later
// EmuOrbit derivative of the pinned GPL frontend. Not standard Libretro API.
// No changes to VM/JIT, renderer algorithms or mixer quality settings.
#include "SIO/Memcard/MemoryCardFile.h"
static void EmuOrbitSetPacing(bool enabled)
{
#ifdef ENABLE_VULKAN
	VKLibretro::SetPacing(enabled);
#endif
#ifdef ENABLE_OPENGL
	GLLibretro::SetPacing(enabled);
#endif
}

static emuorbit::armsx2::CpuResult EmuOrbitCPUOperation(int desired)
{
	using namespace emuorbit::armsx2;
	if (s_emuorbit_terminal.load(std::memory_order_acquire))
		return {EMUORBIT_ARMS_FAILED, 0, 0};
	if (s_emuorbit_owner.load(std::memory_order_acquire) != std::this_thread::get_id())
		return {EMUORBIT_ARMS_WRONG_OWNER, 0, 0};
	const auto state = VMManager::GetState();
	if (s_shutdown_requested.load(std::memory_order_acquire) || !LibretroCore::s_vm_thread_running.load()
		|| (state != VMState::Running && state != VMState::Paused))
		return {EMUORBIT_ARMS_NOT_READY, 0, 0};
	// An observation must not silently disable pacing and let a running game
	// free-run. CPU register snapshots are supported only while paused.
	if (desired == -1 && state != VMState::Paused) return {EMUORBIT_ARMS_NOT_READY, 0, 0};
	if (desired == 0 && state == VMState::Running) return {EMUORBIT_ARMS_OK, EMUORBIT_ARMS_RUNNING, 0};
	// GS can be waiting for frontend consumption. Unpark it BEFORE requesting
	// SetState(Paused), which synchronizes VU and MTGS on the CPU owner.
	if (desired == 1) EmuOrbitSetPacing(false);
	auto reply = std::make_shared<CpuReply>();
	Host::RunOnCPUThread([reply, desired]() {
		reply->apply([desired]() -> CpuResult {
			const auto before = VMManager::GetState();
			if (s_emuorbit_terminal.load(std::memory_order_acquire) || s_shutdown_requested.load()
				|| (before != VMState::Running && before != VMState::Paused))
				return {EMUORBIT_ARMS_NOT_READY, 0, 0};
			if (desired == 1 && before == VMState::Running) VMManager::SetState(VMState::Paused);
			if (desired == 0 && before == VMState::Paused) {
				if (auto* stream = SPU2::GetOutputStream()) stream->EmptyBuffer();
				// Enable backpressure BEFORE guest execution resumes, not afterwards.
				EmuOrbitSetPacing(true);
				VMManager::SetState(VMState::Running);
			}
            if (desired == 1) {
				FileMcd_Flush(); // CPU owner, after SetState's VU/GS barrier, before host card checkpoint.
				if (auto* stream = SPU2::GetOutputStream()) stream->EmptyBuffer();
			}
			const auto after = VMManager::GetState();
			return {EMUORBIT_ARMS_OK, after == VMState::Paused ? EMUORBIT_ARMS_PAUSED : EMUORBIT_ARMS_RUNNING,
				cpuRegs.cycle}; // Read ONLY on CPU thread, never racing the recompiler.
		});
	}, false); // Never use the upstream unbounded blocking/stack-reference path.
	const auto result = reply->wait(std::chrono::milliseconds(5000));
	if (result.status != EMUORBIT_ARMS_OK) {
		s_emuorbit_terminal.store(true, std::memory_order_release);
		EmuOrbitSetPacing(false);
	}
	return result;
}

extern "C" RETRO_API unsigned retro_emuorbit_host_abi_version(void) { return EMUORBIT_ARMS_HOST_ABI; }
extern "C" RETRO_API int retro_emuorbit_set_paused_v1(unsigned paused)
{
	if (paused > 1) return EMUORBIT_ARMS_INVALID_ARGUMENT;
	const auto result = EmuOrbitCPUOperation(static_cast<int>(paused));
	if (result.status == EMUORBIT_ARMS_OK && result.state != (paused ? EMUORBIT_ARMS_PAUSED : EMUORBIT_ARMS_RUNNING)) {
		s_emuorbit_terminal.store(true, std::memory_order_release);
		EmuOrbitSetPacing(false);
		return EMUORBIT_ARMS_FAILED;
	}
	return result.status;
}
extern "C" RETRO_API int retro_emuorbit_snapshot_v1(unsigned* state, uint64_t* ee_cycles)
{
	if (!state || !ee_cycles) return EMUORBIT_ARMS_INVALID_ARGUMENT;
	const auto result = EmuOrbitCPUOperation(-1);
	if (result.status == EMUORBIT_ARMS_OK) {
		*state = result.state;
		*ee_cycles = result.cycles;
	}
	return result.status;
}

// Do not call retro_serialize: its fixed 68 MiB frontend block and unbounded
// blocking CPU closure borrow stack/output storage. Our job owns its path,
// executes only on the paused CPU owner, and leaves pacing disabled throughout.
extern "C" RETRO_API int retro_emuorbit_state_file_v1(const char* filename, unsigned loading)
{
	using namespace emuorbit::armsx2;
	if (!filename || !*filename || loading > 1 || std::strlen(filename) > 512)
		return EMUORBIT_ARMS_INVALID_ARGUMENT;
	if (s_emuorbit_terminal.load(std::memory_order_acquire)) return EMUORBIT_ARMS_FAILED;
	if (s_emuorbit_owner.load(std::memory_order_acquire) != std::this_thread::get_id())
		return EMUORBIT_ARMS_WRONG_OWNER;
	if (s_shutdown_requested.load() || !LibretroCore::s_vm_thread_running.load()
		|| VMManager::GetState() != VMState::Paused) return EMUORBIT_ARMS_NOT_READY;
	const auto owned_path = std::make_shared<const std::string>(filename); // never borrow filename
	auto reply = std::make_shared<CpuReply>();
	Host::RunOnCPUThread([reply, owned_path, loading]() {
		reply->apply([owned_path, loading]() -> CpuResult {
			if (s_emuorbit_terminal.load() || s_shutdown_requested.load()
				|| VMManager::GetState() != VMState::Paused)
				return {EMUORBIT_ARMS_NOT_READY, 0, 0};
			Error error;
			bool ok;
			if (loading) {
				ok = SaveState_UnzipFromDisk(*owned_path, &error);
			} else {
				// Private product states use bounded, Java-verifiable Deflate.
				// Never feed an unchecked Zstandard archive to native restore.
				const auto previous_method = EmuConfig.Savestate.CompressionType;
				const auto previous_ratio = EmuConfig.Savestate.CompressionRatio;
				EmuConfig.Savestate.CompressionType = SavestateCompressionMethod::Deflate;
				EmuConfig.Savestate.CompressionRatio = SavestateCompressionLevel::Low;
				auto state = SaveState_DownloadState(&error);
				ok = state && SaveState_ZipToDisk(std::move(state), SaveState_SaveScreenshot(), owned_path->c_str(), &error);
				EmuConfig.Savestate.CompressionType = previous_method;
				EmuConfig.Savestate.CompressionRatio = previous_ratio;
			}
			if (VMManager::GetState() != VMState::Paused)
				return {EMUORBIT_ARMS_FAILED, 0, 0};
			if (!ok) {
				std::fprintf(stderr, "EMUORBIT_ARMS_STATE_REFUSED loading=%u: %s\n", loading, error.GetDescription().c_str());
				// A serializer refusal never hands off a commit. A failed load can
				// have changed RAM/GS: it must not resume this VM, even if still paused.
				return {loading ? EMUORBIT_ARMS_FAILED : EMUORBIT_ARMS_REFUSED, EMUORBIT_ARMS_PAUSED, 0};
			}
			FileMcd_Flush();
			if (auto* stream = SPU2::GetOutputStream()) stream->EmptyBuffer();
			return {EMUORBIT_ARMS_OK, EMUORBIT_ARMS_PAUSED, cpuRegs.cycle};
		});
	}, false);
	const auto result = reply->wait(std::chrono::milliseconds(30000));
	if (result.status < 0 || (result.status == EMUORBIT_ARMS_OK && result.state != EMUORBIT_ARMS_PAUSED)) {
		s_emuorbit_terminal.store(true, std::memory_order_release);
		EmuOrbitSetPacing(false);
		return result.status < 0 ? result.status : EMUORBIT_ARMS_FAILED;
	}
	return result.status;
}
