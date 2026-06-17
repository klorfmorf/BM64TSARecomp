#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "ultramodern/error_handling.hpp"

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>

#define MINICORO_IMPL
#include "minicoro.h"

static constexpr int32_t YIELD_NORMAL     = 1;
static constexpr int32_t YIELD_STACKCHECK = 2;
static constexpr int32_t YIELD_TERMINATE  = 3;

static constexpr size_t MIN_NATIVE_STACK = 0x80000;

// #define PROCESS_DEBUG

#ifdef PROCESS_DEBUG
static void process_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::fprintf(stderr, "[Process] ");
    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
    va_end(args);
}
#else
#define process_log(...) ((void)0)
#endif

[[noreturn]] static void process_fatal(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::fprintf(stderr, "[Process] FATAL: ");
    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
    va_end(args);

    assert(false);
    ultramodern::error_handling::quick_exit(__FILE__, __LINE__, __FUNCTION__);
}

enum class ProcessState {
    Created,
    Running,
    Suspended,
    Dead,
};

struct ProcessSlot {
    ProcessState state = ProcessState::Created;
    uint32_t process_key = 0;
    uint32_t entry_func = 0;
    uint32_t end_func = 0;
    uint32_t initial_sp = 0;
    mco_coro* coro = nullptr;
    recomp_context ctx{};

    ~ProcessSlot() {
        destroy_coro();
    }

    void destroy_coro() {
        if (coro != nullptr) {
            mco_destroy(coro);
            coro = nullptr;
        }
    }
};

struct ProcessRuntime {
    std::unordered_map<uint32_t, std::unique_ptr<ProcessSlot>> slots;
    ProcessSlot* current_slot = nullptr;
    uint8_t* current_rdram = nullptr;
    int32_t yield_reason = 0;
    int32_t resume_value = 0;
    bool initialized = false;

    void reset() {
        if (current_slot != nullptr) {
            process_fatal("reset while process 0x%08X is running", current_slot->process_key);
        }

        slots.clear();
        current_slot = nullptr;
        current_rdram = nullptr;
        yield_reason = 0;
        resume_value = 0;
        initialized = true;
    }

    ProcessSlot* find(uint32_t process_key) {
        auto it = slots.find(process_key);
        if (it == slots.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    ProcessSlot& create(uint32_t process_key, uint32_t entry_func, uint32_t initial_sp,
                        uint32_t end_func);

    void destroy(uint32_t process_key) {
        ProcessSlot* slot = find(process_key);
        if (slot == nullptr) {
            return;
        }

        if (slot == current_slot) {
            process_fatal("attempted to destroy running process 0x%08X", process_key);
        }

        slots.erase(process_key);
    }
};

static ProcessRuntime g_runtime;

static void yield_running_coro() {
    mco_coro* running = mco_running();
    if (running == nullptr) {
        process_fatal("yield requested without a running coroutine");
    }

    mco_result result = mco_yield(running);
    if (result != MCO_SUCCESS) {
        process_fatal("mco_yield failed: %s", mco_result_description(result));
    }
}

static recomp_func_t* lookup_func(uint32_t func_vram, const char* label, uint32_t process_key) {
    recomp_func_t* func = get_function(static_cast<int32_t>(func_vram));
    if (func == nullptr) {
        process_fatal("could not resolve %s function 0x%08X for process 0x%08X",
                      label, func_vram, process_key);
    }
    return func;
}

static void process_coro_entry(mco_coro* co) {
    (void)co;

    ProcessSlot* slot = g_runtime.current_slot;
    if (slot == nullptr) {
        process_fatal("process_coro_entry called without a current slot");
    }

    uint8_t* rdram = g_runtime.current_rdram;
    if (rdram == nullptr) {
        process_fatal("process_coro_entry called without RDRAM for process 0x%08X",
                      slot->process_key);
    }

    recomp_func_t* entry_func = lookup_func(slot->entry_func, "entry", slot->process_key);

    std::memset(&slot->ctx, 0, sizeof(slot->ctx));
    slot->ctx.r29 = slot->initial_sp;
    slot->ctx.mips3_float_mode = 0;
    slot->ctx.f_odd = &slot->ctx.f0.u32h;

    process_log("starting process 0x%08X at 0x%08X sp=0x%08X",
                slot->process_key, slot->entry_func, slot->initial_sp);

    slot->state = ProcessState::Running;
    entry_func(rdram, &slot->ctx);

    process_log("process 0x%08X returned normally; calling EndProcess", slot->process_key);

    recomp_func_t* end_func = lookup_func(slot->end_func, "EndProcess", slot->process_key);
    end_func(rdram, &slot->ctx);

    process_fatal("EndProcess returned for process 0x%08X", slot->process_key);
}

ProcessSlot& ProcessRuntime::create(uint32_t process_key, uint32_t entry_func, uint32_t initial_sp,
                                    uint32_t end_func) {
    if (process_key == 0) {
        process_fatal("refusing to create a process slot for key 0");
    }

    auto slot = std::make_unique<ProcessSlot>();
    slot->process_key = process_key;
    slot->entry_func = entry_func;
    slot->end_func = end_func;
    slot->initial_sp = initial_sp;
    slot->state = ProcessState::Created;

    mco_desc desc = mco_desc_init(process_coro_entry, MIN_NATIVE_STACK);
    mco_result result = mco_create(&slot->coro, &desc);
    if (result != MCO_SUCCESS) {
        process_fatal("failed to create coroutine for process 0x%08X: %s",
                      process_key, mco_result_description(result));
    }

    ProcessSlot* raw_slot = slot.get();
    auto [_, inserted] = slots.emplace(process_key, std::move(slot));
    if (!inserted) {
        process_fatal("duplicate process slot for key 0x%08X", process_key);
    }

    process_log("created process slot 0x%08X native_stack=%zu",
                process_key, MIN_NATIVE_STACK);

    return *raw_slot;
}

static bool is_known_yield_reason(int32_t reason) {
    return reason == YIELD_NORMAL || reason == YIELD_STACKCHECK || reason == YIELD_TERMINATE;
}

extern "C" void recomp_process_init(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;

    g_runtime.reset();
}

extern "C" void recomp_process_resume(uint8_t* rdram, recomp_context* ctx) {
    uint32_t process_key = _arg<0, uint32_t>(rdram, ctx);
    uint32_t entry_func = _arg<1, uint32_t>(rdram, ctx);
    uint32_t initial_sp = _arg<2, uint32_t>(rdram, ctx);
    uint32_t end_func = _arg<3, uint32_t>(rdram, ctx);

    if (!g_runtime.initialized) {
        process_fatal("resume before process runtime initialization");
    }

    if (g_runtime.current_slot != nullptr) {
        process_fatal("nested resume while process 0x%08X is running",
                      g_runtime.current_slot->process_key);
    }

    if (process_key == 0) {
        process_fatal("resume requested for process key 0");
    }

    ProcessSlot* slot = g_runtime.find(process_key);
    if (slot == nullptr) {
        slot = &g_runtime.create(process_key, entry_func, initial_sp, end_func);
    } else {
        if (slot->state == ProcessState::Dead) {
            process_fatal("resume requested for dead process 0x%08X", process_key);
        }

        if (slot->entry_func != entry_func || slot->end_func != end_func ||
            slot->initial_sp != initial_sp) {
            process_fatal("resume metadata changed for process 0x%08X", process_key);
        }
    }

    if (slot->coro == nullptr) {
        process_fatal("process 0x%08X has no coroutine", process_key);
    }

    if (mco_status(slot->coro) == MCO_DEAD) {
        process_fatal("resume requested for dead coroutine 0x%08X", process_key);
    }

    g_runtime.current_slot = slot;
    g_runtime.current_rdram = rdram;
    g_runtime.resume_value = 1;
    g_runtime.yield_reason = 0;

    mco_result result = mco_resume(slot->coro);

    g_runtime.current_slot = nullptr;
    g_runtime.current_rdram = nullptr;

    if (result != MCO_SUCCESS) {
        slot->state = ProcessState::Dead;
        process_fatal("mco_resume failed for process 0x%08X: %s",
                      process_key, mco_result_description(result));
    }

    if (!is_known_yield_reason(g_runtime.yield_reason)) {
        process_fatal("process 0x%08X returned invalid yield reason %d",
                      process_key, g_runtime.yield_reason);
    }

    _return(ctx, g_runtime.yield_reason);
}

extern "C" void recomp_process_destroy(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;

    uint32_t process_key = _arg<0, uint32_t>(rdram, ctx);
    if (process_key == 0) {
        return;
    }

    g_runtime.destroy(process_key);
}

extern "C" void recomp_process_yield(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;

    int32_t reason = _arg<0, int32_t>(rdram, ctx);
    if (!is_known_yield_reason(reason)) {
        process_fatal("invalid yield reason %d", reason);
    }

    ProcessSlot* slot = g_runtime.current_slot;
    if (slot == nullptr) {
        process_fatal("yield called with no current process");
    }

    process_log("process 0x%08X yielding with reason %d", slot->process_key, reason);

    slot->state = (reason == YIELD_TERMINATE) ? ProcessState::Dead : ProcessState::Suspended;
    g_runtime.yield_reason = reason;

    yield_running_coro();

    if (reason == YIELD_TERMINATE) {
        process_fatal("terminated process 0x%08X was resumed", slot->process_key);
    }

    slot->state = ProcessState::Running;
    _return(ctx, g_runtime.resume_value);
}

extern "C" void recomp_process_get_count(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;

    _return(ctx, static_cast<int32_t>(g_runtime.slots.size()));
}

extern "C" void recomp_process_get_current_key(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;

    uint32_t current_key = g_runtime.current_slot != nullptr ? g_runtime.current_slot->process_key : 0;
    _return(ctx, current_key);
}

extern "C" void setjmp_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;

    process_fatal("setjmp_recomp called");
}
