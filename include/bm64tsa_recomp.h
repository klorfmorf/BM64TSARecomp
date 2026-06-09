#ifndef __RECOMP_H__
#define __RECOMP_H__

#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <fenv.h>
#include <assert.h>
#include <stdio.h>

// Compiler definition to disable inter-procedural optimization, allowing multiple functions to be in a single file without breaking interposition.
#if defined(_MSC_VER) && !defined(__clang__) && !defined(__INTEL_COMPILER)
    // MSVC's __declspec(noinline) seems to disable inter-procedural optimization entirely, so it's all that's needed.
    #define RECOMP_FUNC __declspec(noinline)
    
    // Use MSVC's fenv_access pragma.
    #define SET_FENV_ACCESS() _Pragma("fenv_access(on)")
#elif defined(__clang__)
    // Clang has no dedicated IPO attribute, so we use a combination of other attributes to give the desired behavior.
    // The inline keyword allows multiple definitions during linking, and extern forces clang to emit an externally visible definition.
    // Weak forces Clang to not perform any IPO as the symbol can be interposed, which prevents actual inlining due to the inline keyword.
    // Add noinline on for good measure, which doesn't conflict with the inline keyword as they have different meanings.
    #define RECOMP_FUNC extern inline __attribute__((weak,noinline))

    // Use the standard STDC FENV_ACCESS pragma.
    #define SET_FENV_ACCESS() _Pragma("STDC FENV_ACCESS ON")
#elif defined(__GNUC__) && !defined(__INTEL_COMPILER)
    // Use GCC's attribute for disabling inter-procedural optimizations. Also enable the rounding-math compiler flag to disable
    // constant folding so that arithmetic respects the floating point environment. This is needed because gcc doesn't implement
    // any FENV_ACCESS pragma.
    #define RECOMP_FUNC __attribute__((noipa, optimize("rounding-math")))

    // There's no FENV_ACCESS pragma in gcc, so this can be empty.
    #define SET_FENV_ACCESS()
#else
    #error "No RECOMP_FUNC definition for this compiler"
#endif

// Implementation of 64-bit multiply and divide instructions
#if defined(__SIZEOF_INT128__)

static inline void DMULT(int64_t a, int64_t b, int64_t* lo64, int64_t* hi64) {
    __int128 full128 = ((__int128)a) * ((__int128)b);

    *hi64 = (int64_t)(full128 >> 64);
    *lo64 = (int64_t)(full128 >> 0);
}

static inline void DMULTU(uint64_t a, uint64_t b, uint64_t* lo64, uint64_t* hi64) {
    unsigned __int128 full128 = ((unsigned __int128)a) * ((unsigned __int128)b);

    *hi64 = (uint64_t)(full128 >> 64);
    *lo64 = (uint64_t)(full128 >> 0);
}

#elif defined(_MSC_VER)

#include <intrin.h>
#pragma intrinsic(_mul128)
#pragma intrinsic(_umul128)

static inline void DMULT(int64_t a, int64_t b, int64_t* lo64, int64_t* hi64) {
    *lo64 = _mul128(a, b, hi64);
}

static inline void DMULTU(uint64_t a, uint64_t b, uint64_t* lo64, uint64_t* hi64) {
    *lo64 = _umul128(a, b, hi64);
}

#else
#error "128-bit integer type not found"
#endif

static inline void DDIV(int64_t a, int64_t b, int64_t* quot, int64_t* rem) {
    int overflow = ((uint64_t)a == 0x8000000000000000ull) && (b == -1ll);
    *quot = overflow ? a : (a / b);
    *rem = overflow ? 0 : (a % b);
}

static inline void DDIVU(uint64_t a, uint64_t b, uint64_t* quot, uint64_t* rem) {
    *quot = a / b;
    *rem = a % b;
}

typedef uint64_t gpr;

/**
 * TLB Lookup addresses for Bomberman 64: The Second Attack
 */
#define RELOC_SECTION_60000000 0x80250000
#define RELOC_SECTION_40000000 0x80258000
#define RELOC_SECTION_41000000 0x80280000
#define RELOC_SECTION_42000000 0x8027A000
#define RELOC_SECTION_43000000 0x802A8000
#define RELOC_SECTION_44000000 0x802A0000
#define RELOC_SECTION_45000000 0x80288000

// ----------------------------------------------------------------------------------
// SLOP QUARANTINE ZONE
// You deserve what happens to you if you touch anything in this zone and it breaks.
// ----------------------------------------------------------------------------------

/**
 * The game uses an array of 64 TLB entries to track the mapped pages.
 */
struct UnkStruct800CA668 {
    /* 0x00 */ long unk0;          // Module identifier?
    /* 0x04 */ long page_count;    // How many 4KB pages this mapping takes up (for example, 1 means the pages each have a size of 4KB, 4 means they have a size of 16KB, etc.)
    /* 0x08 */ long virtual_addr;  // Virtual address (KUSEG) of the overlay
    /* 0x0C */ long physical_addr; // Physical address of the overlay, corresponds to the KSEG0 address with the first bit being zero (for example, physical is 0x00250000 and KSEG0 is 0x80250000)
}; // size:0x10

// TLB array table. Declared here for documentation, but not accessed here as it lives in RDRAM-space.
extern struct UnkStruct800CA668 D_800CA668[64]; // 0x800CA668

// Defined in src/game/recomp_api.cpp. Returns the host-side KSEG0 physical
// address for a TLB-mapped overlay virtual address, or 0 if the address is not
// covered by any active overlay slot. The slot allocator gives each overlay
// its own 256 KB region in rdram so multiple loaded overlays cannot collide
// the way the game's 8 KB TLB pages do.
#ifdef __cplusplus
extern "C" {
#endif
uint32_t overlay_slot_resolve(uint32_t virtual_addr);
#ifdef __cplusplus
}
#endif

#define _D_800CA668_RDRAM_OFFSET 0xCA668ULL

/**
 * Translate a MIPS virtual address to a sign-extended KSEG0 physical address.
 *
 * - Addresses already in normal RDRAM (0x80xxxxxx) or the zerojmp marker (0x10000000)
 *   are returned as-is (sign-extended).
 * - Other addresses (TLB-mapped overlay regions like 0x40xxxxxx-0x6Fxxxxxx) are
 *   resolved through the game's own TLB table at D_800CA668, read directly from
 *   rdram to avoid recursion via MEM_W.
 *
 * The TLB comparison is performed in 32-bit space: a sign-extended 64-bit virtual
 * address like 0xFFFFFFFF45ABCDEF would otherwise always compare greater than the
 * 32-bit virtual_addr field.
 */
static inline int64_t _tlb_lookup(uint8_t* rdram, int64_t eff_addr) {
    uint32_t addr32 = (uint32_t)eff_addr;

    // Fast path: normal RDRAM or zerojmp marker — no TLB walk needed.
    if ((addr32 >> 24) == 0x80 || addr32 == 0x10000000) {
        return (int64_t)(int64_t)(int32_t)addr32; // we repeatedly cast in order to readd the upper FFFFFFFF value(s).
    }

    // First, consult our per-overlay slot allocator (src/game/recomp_api.cpp).
    // It covers the case where an overlay is bigger than the game's TLB page
    // window and so wouldn't be findable in D_800CA668.
    uint32_t slot_phys = overlay_slot_resolve(addr32);
    if (slot_phys != 0) {
        return (int64_t)(int64_t)(int32_t)slot_phys; // same here.
    }

    // Fall back to walking the game's TLB table directly via rdram (no
    // recursion through MEM_W). This handles addresses the game has mapped
    // but that we haven't tracked as an overlay slot (e.g. zerojmp pages).
    uint8_t* tlb_base = rdram + _D_800CA668_RDRAM_OFFSET;
    for (int page = 0; page < 64; page++) {
        uint8_t* entry = tlb_base + (page * 0x10);
        uint32_t virtual_addr  = *(uint32_t*)(entry + 0x8);
        uint32_t physical_addr = *(uint32_t*)(entry + 0xC);

        // Page covers a 0x8000-byte (32 KiB) range starting at virtual_addr.
        if (addr32 >= virtual_addr && (virtual_addr + 0x8000U) > addr32) {
            uint32_t phys = (addr32 - virtual_addr) + physical_addr + 0x80000000U;
            return (int64_t)(int64_t)(int32_t)phys; // same here.
        }
    }

    // No mapping found — return the original (sign-extended). Will most likely
    // segfault downstream. Let N64Recomp crash; we're never supposed to get here.
    printf("[_tlb_translate] WARNING: no TLB mapping found for address 0x%jX. Recomp is likely to crash\n", (int64_t)addr32);
    return (int64_t)(int64_t)(int32_t)addr32; // same here.
}

// ----------------------------------------------------------------------------------
// SLOP QUARANTINE ZONE END
// ----------------------------------------------------------------------------------

#define SIGNED(val) \
    ((int64_t)(val))

#define ADD32(a, b) \
    ((gpr)(int32_t)((a) + (b)))

#define SUB32(a, b) \
    ((gpr)(int32_t)((a) - (b)))

// MEM_* macros: _tlb_lookup now returns the fully-resolved sign-extended KSEG0
// physical address (effective address with TLB translation already applied and
// the offset already folded in), so we no longer add `+ (offset)` after the call.

#define MEM_W(offset, reg) \
    (*(int32_t*)(rdram + (_tlb_lookup(rdram, offset + reg) - 0xFFFFFFFF80000000)))

#define MEM_H(offset, reg) \
    (*(int16_t*)(rdram + ((_tlb_lookup(rdram, offset + reg) ^ 2) - 0xFFFFFFFF80000000)))

#define MEM_B(offset, reg) \
    (*(int8_t*)(rdram + ((_tlb_lookup(rdram, offset + reg) ^ 3) - 0xFFFFFFFF80000000)))

#define MEM_WU(offset, reg) \
    (*(uint32_t*)(rdram + (_tlb_lookup(rdram, offset + reg) - 0xFFFFFFFF80000000)))

#define MEM_HU(offset, reg) \
    (*(uint16_t*)(rdram + ((_tlb_lookup(rdram, offset + reg) ^ 2) - 0xFFFFFFFF80000000)))

#define MEM_BU(offset, reg) \
    (*(uint8_t*)(rdram + ((_tlb_lookup(rdram, offset + reg) ^ 3) - 0xFFFFFFFF80000000)))

#define SD(val, offset, reg) { \
    uint64_t _sd_phys = _tlb_lookup(rdram, offset + reg); \
    *(uint32_t*)(rdram + ((_sd_phys + 4) - 0xFFFFFFFF80000000)) = (uint32_t)((gpr)(val) >> 0); \
    *(uint32_t*)(rdram + ((_sd_phys + 0) - 0xFFFFFFFF80000000)) = (uint32_t)((gpr)(val) >> 32); \
}

static inline uint64_t load_doubleword(uint8_t* rdram, gpr reg, gpr offset) {
    uint64_t ret = 0;
    uint64_t lo = (uint64_t)(uint32_t)MEM_W(reg, offset + 4);
    uint64_t hi = (uint64_t)(uint32_t)MEM_W(reg, offset + 0);
    ret = (lo << 0) | (hi << 32);
    return ret;
}

#define LD(offset, reg) \
    load_doubleword(rdram, offset, reg)

static inline gpr do_lwl(uint8_t* rdram, gpr initial_value, gpr offset, gpr reg) {
    // Calculate the overall address
    gpr address = (offset + reg);

    // Load the aligned word
    gpr word_address = address & ~0x3;
    uint32_t loaded_value = MEM_W(0, word_address);

    // Mask the existing value and shift the loaded value appropriately
    gpr misalignment = address & 0x3;
    gpr masked_value = initial_value & (gpr)(uint32_t)~(0xFFFFFFFFu << (misalignment * 8));
    loaded_value <<= (misalignment * 8);

    // Cast to int32_t to sign extend first
    return (gpr)(int32_t)(masked_value | loaded_value);
}

static inline gpr do_lwr(uint8_t* rdram, gpr initial_value, gpr offset, gpr reg) {
    // Calculate the overall address
    gpr address = (offset + reg);
    
    // Load the aligned word
    gpr word_address = address & ~0x3;
    uint32_t loaded_value = MEM_W(0, word_address);

    // Mask the existing value and shift the loaded value appropriately
    gpr misalignment = address & 0x3;
    gpr masked_value = initial_value & (gpr)(uint32_t)~(0xFFFFFFFFu >> (24 - misalignment * 8));
    loaded_value >>= (24 - misalignment * 8);

    // Cast to int32_t to sign extend first
    return (gpr)(int32_t)(masked_value | loaded_value);
}

static inline void do_swl(uint8_t* rdram, gpr offset, gpr reg, gpr val) {
    // Calculate the overall address
    gpr address = (offset + reg);

    // Get the initial value of the aligned word
    gpr word_address = address & ~0x3;
    uint32_t initial_value = MEM_W(0, word_address);

    // Mask the initial value and shift the input value appropriately
    gpr misalignment = address & 0x3;
    uint32_t masked_initial_value = initial_value & ~(0xFFFFFFFFu >> (misalignment * 8));
    uint32_t shifted_input_value = ((uint32_t)val) >> (misalignment * 8);
    MEM_W(0, word_address) = masked_initial_value | shifted_input_value;
}

static inline void do_swr(uint8_t* rdram, gpr offset, gpr reg, gpr val) {
    // Calculate the overall address
    gpr address = (offset + reg);

    // Get the initial value of the aligned word
    gpr word_address = address & ~0x3;
    uint32_t initial_value = MEM_W(0, word_address);

    // Mask the initial value and shift the input value appropriately
    gpr misalignment = address & 0x3;
    uint32_t masked_initial_value = initial_value & ~(0xFFFFFFFFu << (24 - misalignment * 8));
    uint32_t shifted_input_value = ((uint32_t)val) << (24 - misalignment * 8);
    MEM_W(0, word_address) = masked_initial_value | shifted_input_value;
}

static inline gpr do_ldl(uint8_t* rdram, gpr initial_value, gpr offset, gpr reg) {
    // Calculate the overall address
    gpr address = (offset + reg);

    // Load the aligned dword
    gpr dword_address = address & ~0x7;
    uint64_t loaded_value = load_doubleword(rdram, 0, dword_address);

    // Mask the existing value and shift the loaded value appropriately
    gpr misalignment = address & 0x7;
    gpr masked_value = initial_value & ~(0xFFFFFFFFFFFFFFFFu << (misalignment * 8));
    loaded_value <<= (misalignment * 8);

    return masked_value | loaded_value;
}

static inline gpr do_ldr(uint8_t* rdram, gpr initial_value, gpr offset, gpr reg) {
    // Calculate the overall address
    gpr address = (offset + reg);
    
    // Load the aligned dword
    gpr dword_address = address & ~0x7;
    uint64_t loaded_value = load_doubleword(rdram, 0, dword_address);

    // Mask the existing value and shift the loaded value appropriately
    gpr misalignment = address & 0x7;
    gpr masked_value = initial_value & ~(0xFFFFFFFFFFFFFFFFu >> (56 - misalignment * 8));
    loaded_value >>= (56 - misalignment * 8);

    return masked_value | loaded_value;
}

static inline void do_sdl(uint8_t* rdram, gpr offset, gpr reg, gpr val) {
    // Calculate the overall address
    gpr address = (offset + reg);

    // Get the initial value of the aligned dword
    gpr dword_address = address & ~0x7;
    uint64_t initial_value = load_doubleword(rdram, 0, dword_address);

    // Mask the initial value and shift the input value appropriately
    gpr misalignment = address & 0x7;
    uint64_t masked_initial_value = initial_value & ~(0xFFFFFFFFFFFFFFFFu >> (misalignment * 8));
    uint64_t shifted_input_value = val >> (misalignment * 8);

    uint64_t ret = masked_initial_value | shifted_input_value;
    uint32_t lo = (uint32_t)ret;
    uint32_t hi = (uint32_t)(ret >> 32);

    MEM_W(0, dword_address + 4) = lo;
    MEM_W(0, dword_address + 0) = hi;
}

static inline void do_sdr(uint8_t* rdram, gpr offset, gpr reg, gpr val) {
    // Calculate the overall address
    gpr address = (offset + reg);

    // Get the initial value of the aligned dword
    gpr dword_address = address & ~0x7;
    uint64_t initial_value = load_doubleword(rdram, 0, dword_address);

    // Mask the initial value and shift the input value appropriately
    gpr misalignment = address & 0x7;
    uint64_t masked_initial_value = initial_value & ~(0xFFFFFFFFFFFFFFFFu << (56 - misalignment * 8));
    uint64_t shifted_input_value = val << (56 - misalignment * 8);
    
    uint64_t ret = masked_initial_value | shifted_input_value;
    uint32_t lo = (uint32_t)ret;
    uint32_t hi = (uint32_t)(ret >> 32);

    MEM_W(0, dword_address + 4) = lo;
    MEM_W(0, dword_address + 0) = hi;
}

static inline uint32_t get_cop1_cs() {
    uint32_t rounding_mode = 0;
    switch (fegetround()) {
        // round to nearest value
        case FE_TONEAREST:
        default:
            rounding_mode = 0;
            break;
        // round to zero (truncate)
        case FE_TOWARDZERO:
            rounding_mode = 1;
            break;
        // round to positive infinity (ceil)
        case FE_UPWARD:
            rounding_mode = 2;
            break;
        // round to negative infinity (floor)
        case FE_DOWNWARD:
            rounding_mode = 3;
            break;
    }
    return rounding_mode;
}

static inline void set_cop1_cs(uint32_t val) {
    uint32_t rounding_mode = val & 0x3;
    int round = FE_TONEAREST;
    switch (rounding_mode) {
        case 0: // round to nearest value
            round = FE_TONEAREST;
            break;
        case 1: // round to zero (truncate)
            round = FE_TOWARDZERO;
            break;
        case 2: // round to positive infinity (ceil)
            round = FE_UPWARD;
            break;
        case 3: // round to negative infinity (floor)
            round = FE_DOWNWARD;
            break;
    }
    fesetround(round);
}

#define S32(val) \
    ((int32_t)(val))
    
#define U32(val) \
    ((uint32_t)(val))

#define S64(val) \
    ((int64_t)(val))

#define U64(val) \
    ((uint64_t)(val))

#define MUL_S(val1, val2) \
    ((val1) * (val2))

#define MUL_D(val1, val2) \
    ((val1) * (val2))

#define DIV_S(val1, val2) \
    ((val1) / (val2))

#define DIV_D(val1, val2) \
    ((val1) / (val2))

#define CVT_S_W(val) \
    ((float)((int32_t)(val)))

#define CVT_D_W(val) \
    ((double)((int32_t)(val)))

#define CVT_D_L(val) \
    ((double)((int64_t)(val)))

#define CVT_S_L(val) \
    ((float)((int64_t)(val)))

#define CVT_D_S(val) \
    ((double)(val))

#define CVT_S_D(val) \
    ((float)(val))

#define TRUNC_W_S(val) \
    ((int32_t)(val))

#define TRUNC_W_D(val) \
    ((int32_t)(val))

#define TRUNC_L_S(val) \
    ((int64_t)(val))

#define TRUNC_L_D(val) \
    ((int64_t)(val))

#define DEFAULT_ROUNDING_MODE 0

static inline int32_t do_cvt_w_s(float val) {
    // Rounding mode aware float to 32-bit int conversion.
    return (int32_t)lrintf(val);
}

#define CVT_W_S(val) \
    do_cvt_w_s(val)

static inline int64_t do_cvt_l_s(float val) {
    // Rounding mode aware float to 64-bit int conversion.
    return (int64_t)llrintf(val);
}

#define CVT_L_S(val) \
    do_cvt_l_s(val);

static inline int32_t do_cvt_w_d(double val) {
    // Rounding mode aware double to 32-bit int conversion.
    return (int32_t)lrint(val);
}

#define CVT_W_D(val) \
    do_cvt_w_d(val)

static inline int64_t do_cvt_l_d(double val) {
    // Rounding mode aware double to 64-bit int conversion.
    return (int64_t)llrint(val);
}

#define CVT_L_D(val) \
    do_cvt_l_d(val)

#define NAN_CHECK(val) \
    assert(val == val)

//#define NAN_CHECK(val)

typedef union {
    double d;
    struct {
        float fl;
        float fh;
    };
    struct {
        uint32_t u32l;
        uint32_t u32h;
    };
    uint64_t u64;
} fpr;

typedef struct {
    gpr r0,  r1,  r2,  r3,  r4,  r5,  r6,  r7,
        r8,  r9,  r10, r11, r12, r13, r14, r15,
        r16, r17, r18, r19, r20, r21, r22, r23,
        r24, r25, r26, r27, r28, r29, r30, r31;
    fpr f0,  f1,  f2,  f3,  f4,  f5,  f6,  f7,
        f8,  f9,  f10, f11, f12, f13, f14, f15,
        f16, f17, f18, f19, f20, f21, f22, f23,
        f24, f25, f26, f27, f28, f29, f30, f31;
    uint64_t hi, lo;
    uint32_t* f_odd;
    uint32_t status_reg;
    uint8_t mips3_float_mode;
} recomp_context;

// Checks if the target is an even float register or that mips3 float mode is enabled
#define CHECK_FR(ctx, idx) \
    assert(((idx) & 1) == 0 || (ctx)->mips3_float_mode)

#ifdef __cplusplus
extern "C" {
#endif

void cop0_status_write(recomp_context* ctx, gpr value);
gpr cop0_status_read(recomp_context* ctx);
void switch_error(const char* func, uint32_t vram, uint32_t jtbl);
void do_break(uint32_t vram);

// The function signature for all recompiler output functions.
typedef void (recomp_func_t)(uint8_t* rdram, recomp_context* ctx);
// The function signature for special functions that need a third argument.
// These get called via generated shims to allow providing some information about the caller, such as mod id.
typedef void (recomp_func_ext_t)(uint8_t* rdram, recomp_context* ctx, uintptr_t arg);

recomp_func_t* get_function(int32_t vram);

// Translate the TLB values. Pass a 0 for offset
#define LOOKUP_FUNC(val)         \
    get_function((int32_t)(_tlb_lookup(rdram, 0 + val)))

extern int32_t* section_addresses;

#define LO16(x) \
    ((x) & 0xFFFF)

#define HI16(x) \
    (((x) >> 16) + (((x) >> 15) & 1))

#define RELOC_HI16(section_index, offset) \
    HI16(section_addresses[section_index] + (offset))

#define RELOC_LO16(section_index, offset) \
    LO16(section_addresses[section_index] + (offset))

void recomp_syscall_handler(uint8_t* rdram, recomp_context* ctx, int32_t instruction_vram);

void pause_self(uint8_t *rdram);

#ifdef __cplusplus
}
#endif

#endif
