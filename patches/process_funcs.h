// process_api.h
#ifndef __PROCESS_API_H__
#define __PROCESS_API_H__

#include "patch_helpers.h"

// ============================================================================
// Yield reasons (must match host side)
// ============================================================================

#define YIELD_NORMAL     1
#define YIELD_STACKCHECK 2
#define YIELD_TERMINATE  3

// ============================================================================
// Core coroutine API
// ============================================================================

// Initialize the process/coroutine system
// Must be called before any other process functions
DECLARE_FUNC(void, recomp_process_init);

// Resume a process coroutine, creating it first if necessary
// process_key:  Stable MIPS Process* value used as the host slot key
// entry_func:   MIPS VRAM address of the process entry function
// initial_sp:   Initial MIPS stack pointer value
// end_func:     MIPS VRAM address of EndProcess for normal returns
DECLARE_FUNC(s32, recomp_process_resume, u32 process_key, u32 entry_func, u32 initial_sp, u32 end_func);

// Destroy a process's coroutine
// process_key: Stable MIPS Process* value whose coroutine should be destroyed
DECLARE_FUNC(void, recomp_process_destroy, u32 process_key);

// Yield from current process back to scheduler
// reason:  Why we're yielding (YIELD_NORMAL, YIELD_STACKCHECK, YIELD_TERMINATE)
// Returns: Value passed by scheduler when resuming
DECLARE_FUNC(s32, recomp_process_yield, s32 reason);

// ============================================================================
// Debug/query API
// ============================================================================

// Get the number of active process coroutines
DECLARE_FUNC(s32, recomp_process_get_count);

// Get the currently running process key, or 0 if the scheduler is active
DECLARE_FUNC(u32, recomp_process_get_current_key);

#endif // __PROCESS_API_H__
