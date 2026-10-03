/* SPDX-License-Identifier: MIT. Private, versioned EmuOrbit extension, NOT Libretro API. */
#pragma once
#include <stdint.h>

#define EMUORBIT_ARMS_HOST_ABI 1u
enum EmuOrbitArmsResult {
    EMUORBIT_ARMS_OK = 1,
    EMUORBIT_ARMS_REFUSED = 2, /* serializer refused; paused/unchanged VM confirmed */
    EMUORBIT_ARMS_NOT_READY = 0,
    EMUORBIT_ARMS_FAILED = -1,
    EMUORBIT_ARMS_TIMEOUT = -2,
    EMUORBIT_ARMS_WRONG_OWNER = -3,
    EMUORBIT_ARMS_INVALID_ARGUMENT = -4
};
enum EmuOrbitArmsState { EMUORBIT_ARMS_RUNNING = 1, EMUORBIT_ARMS_PAUSED = 2 };
/* All operations except version belong to the serial frontend owner.
 * Negative failures are terminal to the host: retain core/window/lease until
 * process exit if unload cannot be confirmed. No late closure retains an
 * output pointer or a reference to the waiting thread's stack. */
typedef unsigned (*EmuOrbitArmsHostVersion)(void);
typedef int (*EmuOrbitArmsSetPaused)(unsigned paused);
typedef int (*EmuOrbitArmsSnapshot)(unsigned* state, uint64_t* ee_cycles);
/* Private scratch file owned/retained by the frontend VM lease. Not an
 * external-state import API. Requires Paused; a failed load may have mutated
 * the VM and is terminal. A timed-out job retains its own filename. */
typedef int (*EmuOrbitArmsStateFile)(const char* filename, unsigned loading);
