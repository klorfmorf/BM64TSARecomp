#include "patches.h"
#include "relocs.h"

// Reverse mapping of the auto-generated RELOC_SECTION_* macros (physical
// addresses the overlays USED to be loaded to under the broken TLB scheme)
// back to their VIRTUAL bases (0x40000000, 0x41000000, ...). The relocation
// data in each overlay's .data section contains *virtual* pointers like
// 0x41001C38; to turn them into the real host-side physical addresses where
// the overlay slot allocator has placed the overlay, we need to apply
// `delta = load_addr - virtual_base` (not `load_addr - RELOC_SECTION_xxx`).
static u32 reloc_virtual_base_for(u32 original_vaddr) {
    if (original_vaddr == RELOC_SECTION_40000000) return 0x40000000;
    if (original_vaddr == RELOC_SECTION_41000000) return 0x41000000;
    if (original_vaddr == RELOC_SECTION_42000000) return 0x42000000;
    if (original_vaddr == RELOC_SECTION_43000000) return 0x43000000;
    if (original_vaddr == RELOC_SECTION_44000000) return 0x44000000;
    if (original_vaddr == RELOC_SECTION_45000000) return 0x45000000;
    if (original_vaddr == RELOC_SECTION_60000000) return 0x60000000;
    // Fallback: assume already a virtual base (or a static address that
    // doesn't need translation).
    return original_vaddr;
}

void overlay_apply_relocations(u32 file_id, u8 *load_addr) {
    //recomp_printf("[overlay_apply_relocations] file_id 0x%08X load_addr 0x%08X\n", file_id, load_addr);

    if (file_id >= RELOC_TABLE_SIZE) {
        return;
    }

    const RelocInfo *info = &g_relocs[file_id];
    if (info->offsets == NULL || info->count == 0) {
        return;
    }

    const u32 virtual_base = reloc_virtual_base_for(info->original_vaddr);
    const s32 delta = (s32)((u32)load_addr - virtual_base);
    if (delta == 0) {
        return;
    }

    for (int i = 0; i < info->count; i++) {
        u32 offset = info->offsets[i];
        u32 *value_addr = (u32 *)(load_addr + offset);
        u32 value = *value_addr;

        *value_addr = value + (u32)delta;

        //recomp_printf("[overlay_apply_relocations] 0x%08X -> 0x%08X\n", value, *value_addr);
    }
}