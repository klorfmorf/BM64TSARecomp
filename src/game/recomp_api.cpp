#include <cmath>

#include "recomp.h"
#include "librecomp/overlays.hpp"
#include "librecomp/addresses.hpp"
#include "banjo_config.h"
#include "recompinput/recompinput.h"
#include "recompui/recompui.h"
#include "recompui/renderer.h"
#include "banjo_sound.h"
#include "librecomp/helpers.hpp"
#include "../patches/input.h"
#include "../patches/graphics.h"
#include "../patches/sound.h"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/config.hpp"
#include "../lib/N64ModernRuntime/thirdparty/xxHash/xxh3.h"

extern "C" void recomp_update_inputs(uint8_t* rdram, recomp_context* ctx) {
    recompinput::poll_inputs();
}

extern "C" void recomp_puts(uint8_t* rdram, recomp_context* ctx) {
    PTR(char) cur_str = _arg<0, PTR(char)>(rdram, ctx);
    u32 length = _arg<1, u32>(rdram, ctx);

    for (u32 i = 0; i < length; i++) {
        fputc(MEM_B(i, (gpr)cur_str), stdout);
    }
}

extern "C" void recomp_exit(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::quit();
}

extern "C" void recomp_error(uint8_t* rdram, recomp_context* ctx) {
    std::string str{};
    PTR(u8) str_ptr = _arg<0, PTR(u8)>(rdram, ctx);

    for (size_t i = 0; MEM_B(str_ptr, i) != '\x00'; i++) {
        str += (char)MEM_B(str_ptr, i);
    }

    recompui::message_box(str.c_str());
    assert(false);
    ultramodern::error_handling::quick_exit(__FILE__, __LINE__, __FUNCTION__);
}

extern "C" void recomp_get_gyro_deltas(uint8_t* rdram, recomp_context* ctx) {
    float* x_out = _arg<0, float*>(rdram, ctx);
    float* y_out = _arg<1, float*>(rdram, ctx);

    // TODO: use controller number
    recompinput::get_gyro_deltas(0, x_out, y_out);
}

extern "C" void recomp_get_mouse_deltas(uint8_t* rdram, recomp_context* ctx) {
    float* x_out = _arg<0, float*>(rdram, ctx);
    float* y_out = _arg<1, float*>(rdram, ctx);

    recompinput::get_mouse_deltas(x_out, y_out);
}

extern "C" void recomp_powf(uint8_t* rdram, recomp_context* ctx) {
    float a = _arg<0, float>(rdram, ctx);
    float b = ctx->f14.fl; //_arg<1, float>(rdram, ctx);

    _return(ctx, std::pow(a, b));
}

extern "C" void recomp_get_target_framerate(uint8_t* rdram, recomp_context* ctx) {
    int frame_divisor = _arg<0, u32>(rdram, ctx);

    _return(ctx, ultramodern::get_target_framerate(60 / frame_divisor));
}

extern "C" void recomp_get_window_resolution(uint8_t* rdram, recomp_context* ctx) {
    int width, height;
    recompui::get_window_size(width, height);

    gpr width_out = _arg<0, PTR(u32)>(rdram, ctx);
    gpr height_out = _arg<1, PTR(u32)>(rdram, ctx);

    MEM_W(0, width_out) = (u32)width;
    MEM_W(0, height_out) = (u32)height;
}

extern "C" void recomp_get_target_aspect_ratio(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::renderer::GraphicsConfig graphics_config = ultramodern::renderer::get_graphics_config();
    float original = _arg<0, float>(rdram, ctx);
    int width, height;
    recompui::get_window_size(width, height);

    switch (graphics_config.ar_option) {
        case ultramodern::renderer::AspectRatio::Original:
        default:
            _return(ctx, original);
            return;
        case ultramodern::renderer::AspectRatio::Expand:
            _return(ctx, std::max(static_cast<float>(width) / height, original));
            return;
    }
}

extern "C" void recomp_get_cutscene_aspect_ratio(uint8_t *rdram, recomp_context *ctx) {
    float original = _arg<0, float>(rdram, ctx);
    int width, height;
    recompui::get_window_size(width, height);

    switch (banjo::get_cutscene_aspect_ratio_mode()) {
        case banjo::CutsceneAspectRatioMode::Original:
            _return(ctx, original);
            return;
        case banjo::CutsceneAspectRatioMode::Clamp16x9:
        default:
            _return(ctx, 16.0f / 9.0f);
            return;
        case banjo::CutsceneAspectRatioMode::Full:
            _return(ctx, std::max(static_cast<float>(width) / height, original));
            return;
    }
}

extern "C" void recomp_get_bgm_volume(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, banjo::get_bgm_volume() / 100.0f);
}

extern "C" void recomp_get_analog_cam_sensitivity(uint8_t* rdram, recomp_context* ctx) {
    _return<uint32_t>(ctx, banjo::get_analog_cam_sensitivity());
}


extern "C" void recomp_time_us(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, static_cast<u32>(std::chrono::duration_cast<std::chrono::microseconds>(ultramodern::time_since_start()).count()));
}

extern "C" void recomp_load_overlays(uint8_t * rdram, recomp_context * ctx) {
    u32 rom = _arg<0, u32>(rdram, ctx);
    PTR(void) ram = _arg<1, PTR(void)>(rdram, ctx);
    u32 size = _arg<2, u32>(rdram, ctx);

    load_overlays(rom, ram, size);
}

// ---------------------------------------------------------------------------
// Overlay slot allocator (host-side, called from patches and from _tlb_lookup)
// ---------------------------------------------------------------------------
//
// The game's TLB allocator (`modulegetfreepages`) hands out 8 KB pages and
// lays overlays only 32 KB apart, which is too tight for several real
// overlays (ovl_coll_main alone is ~40 KB once .data/.bss are counted).
// With that layout each new overlay corrupts the data section of the
// previously-loaded overlay occupying the next page block.
//
// We sidestep this by giving every unique overlay virtual address its own
// well-separated 256 KB slot in a dedicated region of rdram (0x80400000+,
// which is above the game's normal heap and below the recomp heap at
// 0x81000000). The patched `fexecLoadAddress` loads data into that slot,
// and `_tlb_lookup` consults `overlay_slot_resolve` so every TLB-mapped
// virtual access is routed to the matching slot.
//
// The table lives natively on the host (NOT in emulated rdram) — putting it
// in the patch ELF would cause infinite recursion because `_tlb_lookup`
// itself would need a MEM_W to read the table.

namespace {
struct OverlaySlot {
    uint32_t virtual_addr  = 0;
    uint32_t physical_addr = 0;
    int32_t  file_id       = -1;
};

constexpr uint32_t kOverlaySlotBase  = 0x80900000U; // above patches.ld RAMBASE (0x80801000)+patch code (~50KB), below recomp heap (0x81000000)
constexpr uint32_t kOverlaySlotSize  = 0x00040000U; // 256 KB
constexpr int      kOverlaySlotCount = 27;          // 27 * 256 KB = ~6.75 MB, ends at 0x80FC0000 (256 KB buffer below recomp heap at 0x81000000)

std::array<OverlaySlot, kOverlaySlotCount> g_overlay_slots{};
std::mutex g_overlay_slots_mtx;
} // namespace

extern "C" uint32_t overlay_slot_resolve(uint32_t virtual_addr) {
    // Hot path: called from every MEM_* macro via _tlb_lookup. Cheap linear
    // scan over the small slot table.
    std::lock_guard<std::mutex> lock(g_overlay_slots_mtx);
    for (const auto& s : g_overlay_slots) {
        if (s.file_id == -1) {
            continue;
        }
        if (virtual_addr >= s.virtual_addr &&
            virtual_addr <  s.virtual_addr + kOverlaySlotSize) {
            return s.physical_addr + (virtual_addr - s.virtual_addr);
        }
    }
    return 0;
}

// Called from the recompiled MIPS patch in patches/required_patches.c via the
// 0x8F000110 stub. Returns the host-side KSEG0 address where the overlay for
// `virtual_addr` should be loaded.
extern "C" void recomp_overlay_slot_allocate(uint8_t* /*rdram*/, recomp_context* ctx) {
    uint32_t virtual_addr = static_cast<uint32_t>(ctx->r4);
    int32_t  file_id      = static_cast<int32_t>(ctx->r5);

    std::lock_guard<std::mutex> lock(g_overlay_slots_mtx);

    // Reuse an existing slot if this virtual address has already been
    // allocated one. The game frequently swaps overlays that share a virtual
    // address (e.g. ovl_blast_*), and they're expected to overwrite each
    // other in the same physical slot.
    int free_slot = -1;
    for (int i = 0; i < kOverlaySlotCount; i++) {
        auto& s = g_overlay_slots[i];
        if (s.file_id != -1 && s.virtual_addr == virtual_addr) {
            s.file_id = file_id;
            _return<uint32_t>(ctx, s.physical_addr);
            return;
        }
        if (free_slot == -1 && s.file_id == -1) {
            free_slot = i;
        }
    }

    if (free_slot < 0) {
        fprintf(stderr,
                "[overlay_slot] FATAL: no free slot for virtual_addr=0x%08X (file_id=%d)\n",
                virtual_addr, file_id);
        std::exit(EXIT_FAILURE);
    }

    auto& s = g_overlay_slots[free_slot];
    s.virtual_addr  = virtual_addr;
    s.physical_addr = kOverlaySlotBase + static_cast<uint32_t>(free_slot) * kOverlaySlotSize;
    s.file_id       = file_id;
    _return<uint32_t>(ctx, s.physical_addr);
}

extern "C" void recomp_high_precision_fb_enabled(uint8_t * rdram, recomp_context * ctx) {
    _return(ctx, static_cast<s32>(recompui::renderer::RT64HighPrecisionFBEnabled()));
}

extern "C" void recomp_get_resolution_scale(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, ultramodern::get_resolution_scale());
}

extern "C" void recomp_get_inverted_axes(uint8_t* rdram, recomp_context* ctx) {
    s32* x_out = _arg<0, s32*>(rdram, ctx);
    s32* y_out = _arg<1, s32*>(rdram, ctx);

    banjo::CameraInvertMode mode = banjo::get_camera_invert_mode();

    *x_out = (mode == banjo::CameraInvertMode::InvertX || mode == banjo::CameraInvertMode::InvertBoth);
    *y_out = (mode == banjo::CameraInvertMode::InvertY || mode == banjo::CameraInvertMode::InvertBoth);
}

extern "C" void recomp_get_analog_inverted_axes(uint8_t* rdram, recomp_context* ctx) {
    s32* x_out = _arg<0, s32*>(rdram, ctx);
    s32* y_out = _arg<1, s32*>(rdram, ctx);

    banjo::CameraInvertMode mode = banjo::get_third_person_camera_mode();

    *x_out = (mode == banjo::CameraInvertMode::InvertX || mode == banjo::CameraInvertMode::InvertBoth);
    *y_out = (mode == banjo::CameraInvertMode::InvertY || mode == banjo::CameraInvertMode::InvertBoth);
}

extern "C" void recomp_get_flying_and_swimming_inverted_axes(uint8_t* rdram, recomp_context* ctx) {
    s32* x_out = _arg<0, s32*>(rdram, ctx);
    s32* y_out = _arg<1, s32*>(rdram, ctx);

    banjo::CameraInvertMode mode = banjo::get_flying_and_swimming_invert_mode();

    *x_out = (mode == banjo::CameraInvertMode::InvertX || mode == banjo::CameraInvertMode::InvertBoth);
    *y_out = (mode == banjo::CameraInvertMode::InvertY || mode == banjo::CameraInvertMode::InvertBoth);
}

extern "C" void recomp_get_first_person_inverted_axes(uint8_t* rdram, recomp_context* ctx) {
    s32* x_out = _arg<0, s32*>(rdram, ctx);
    s32* y_out = _arg<1, s32*>(rdram, ctx);

    banjo::CameraInvertMode mode = banjo::get_first_person_invert_mode();

    *x_out = (mode == banjo::CameraInvertMode::InvertX || mode == banjo::CameraInvertMode::InvertBoth);
    *y_out = (mode == banjo::CameraInvertMode::InvertY || mode == banjo::CameraInvertMode::InvertBoth);
}

extern "C" void recomp_get_analog_cam_enabled(uint8_t* rdram, recomp_context* ctx) {
    _return<s32>(ctx, banjo::get_analog_cam_mode() == banjo::AnalogCamMode::On);
}

extern "C" void recomp_get_note_saving_enabled(uint8_t* rdram, recomp_context* ctx) {
    _return<s32>(ctx, banjo::get_note_saving_mode() == banjo::NoteSavingMode::On);
}

extern "C" void recomp_get_right_analog_inputs(uint8_t* rdram, recomp_context* ctx) {
    float* x_out = _arg<0, float*>(rdram, ctx);
    float* y_out = _arg<1, float*>(rdram, ctx);

    // Don't return right analog inputs while game input is disabled.
    if (recompinput::game_input_disabled()) {
        *x_out = 0.0f;
        *y_out = 0.0f;
        return;
    }

    // TODO: Use controller number.
    recompinput::get_right_analog(0, x_out, y_out);
}

extern "C" void recomp_set_right_analog_suppressed(uint8_t* rdram, recomp_context* ctx) {
    s32 suppressed = _arg<0, s32>(rdram, ctx);

    recompinput::set_right_analog_suppressed(suppressed);
}

constexpr uint32_t k1_to_phys(uint32_t addr) {
    return addr & 0x1FFFFFFF;
}

extern "C" void __osPfsGetStatus_recomp(RDRAM_ARG recomp_context * ctx) {
    // TODO
}

extern "C" void __osContAddressCrc_recomp(RDRAM_ARG recomp_context * ctx) {
    // TODO
}

extern "C" void osPfsIsPlug_recomp(RDRAM_ARG recomp_context * ctx) {
    // TODO
}

extern "C" void __osPfsSelectBank_recomp(RDRAM_ARG recomp_context * ctx) {
    // TODO
}

extern "C" void __osContRamRead_recomp(RDRAM_ARG recomp_context * ctx) {
    // TODO
}

extern "C" void __osContRamWrite_recomp(RDRAM_ARG recomp_context * ctx) {
    // TODO
}

extern "C" void longjmp_recomp(RDRAM_ARG recomp_context * ctx) {
    // TODO
}

extern "C" void osPiReadIo_recomp(RDRAM_ARG recomp_context * ctx) {
    uint32_t devAddr = recomp::rom_base | ctx->r4;
    gpr dramAddr = ctx->r5;
    uint32_t physical_addr = k1_to_phys(devAddr);

    if (physical_addr > recomp::rom_base) {
        // cart rom
        recomp::do_rom_pio(PASS_RDRAM dramAddr, physical_addr);
    } else {
        // sram
        assert(false && "SRAM ReadIo unimplemented");
    }

    ctx->r2 = 0;
}

extern "C" void osPfsInit_recomp(uint8_t * rdram, recomp_context* ctx) {
    ctx->r2 = 11; // PFS_ERR_DEVICE
}

// u32 rom_addr, void *ram_addr, u32 size
extern "C" void recomp_load_overlays_by_rom(uint8_t* rdram, recomp_context* ctx) {
    u32 rom_addr = _arg<0, u32>(rdram, ctx);
    PTR(void) ram_addr = _arg<1, PTR(void)>(rdram, ctx);
    u32 size = _arg<2, u32>(rdram, ctx);

    load_overlays(rom_addr, ram_addr, size);
}

extern "C" void recomp_abort(uint8_t* rdram, recomp_context* ctx) {
    std::string msg = _arg_string<0>(rdram, ctx);
    recompui::message_box(msg.c_str());
    assert(false);
    ultramodern::error_handling::quick_exit(__FILE__, __LINE__, __FUNCTION__);
}

extern "C" void recomp_xxh3(uint8_t* rdram, recomp_context* ctx) {
    PTR(void) data = _arg<0, PTR(void)>(rdram, ctx);
    u32 size = _arg<1, u32>(rdram, ctx);
    XXH3_state_t xxh3;
    XXH3_64bits_reset(&xxh3);

    // Hash 1 byte at a time to account for byteswapping.
    for (size_t i = 0; i < size; i++) {
        XXH3_64bits_update(&xxh3, TO_PTR(u8, data + i), 1);
    }

    uint64_t ret = XXH3_64bits_digest(&xxh3);
    
    ctx->r2 = (int32_t)(ret >> 32);
    ctx->r3 = (int32_t)(ret >> 0);
}
