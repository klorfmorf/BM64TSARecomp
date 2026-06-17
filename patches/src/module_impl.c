#include "patches.h"

/* Macro definitions */

#define MODULE_PAGE_COUNT 0x40
#define MODULE_PAGE_SIZE  0x2000      /* 8KiB */
#define MODULE_PAGE_BASE  0x80250000U

/* Struct definitions */

struct UnkStruct800CA668 {
    s32 module_id;
    s32 page_count; /* 1 (8KiB mapping), 4 (32KiB mapping), or 0 for continuation slots */
    s32 vaddr;
    s32 paddr;
};

/* Data declarations */

extern struct UnkStruct800CA668 D_800CA668[];

/* Function declarations */

extern void errstop(const char *fmt, ...);

#if 0
s32 modulegetfreetlb_impl(s32 id) {
    s32 i;

    for(i = 1; i != 31; i++) {
        if (D_800AD1F0[i] == -1) {
            D_800AD1F0[i] = id;
            return i;
        }
    }
    errstop("module : no free TLB ID\n");
    return 0;
}

void moduleMapTLB_impl(s32 id) {
    s32 i;

    for(i = 0; i < 64; i++) {
        if (id == D_800CA668[i].module_id) {
            if (D_800CA668[i].page_count == 4) {
                osMapTLB(modulegetfreetlb_impl(id), 0x6000U, (void *)D_800CA668[i].vaddr, D_800CA668[i].paddr, D_800CA668[i].paddr + 0x4000, -1);
            } else if (D_800CA668[i].page_count == 1) {
                osMapTLB(modulegetfreetlb_impl(id),       0, (void *)D_800CA668[i].vaddr, D_800CA668[i].paddr, D_800CA668[i].paddr + 0x1000, -1);
            }
        }
    }
}
#endif

/* Static function definitions */

static s32 module_page_paddr(s32 index) {
    return (s32)(((u32)MODULE_PAGE_BASE + ((u32)index << 13)) & 0x7FFFFFFFU);
}

static s32 module_pages_are_free(s32 start, s32 count) {
    s32 i;

    if (start < 0 || start + count > MODULE_PAGE_COUNT) {
        return 0;
    }

    for (i = 0; i < count; i++) {
        if (D_800CA668[start + i].module_id != -1) {
            return 0;
        }
    }

    return 1;
}

static s32 module_find_contiguous_pages(s32 count) {
    s32 start;
    s32 step;

    /*
     * If we will emit 4-page TLB mappings, the first 4-page block should
     * remain 0x8000-aligned physically. Original code enforced this by
     * scanning 4-page allocations with i += 4.
     */
    step = (count >= 4) ? 4 : 1;

    for (start = 0; start + count <= MODULE_PAGE_COUNT; start += step) {
        if (module_pages_are_free(start, count)) {
            return start;
        }
    }

    return -1;
}

/* Function definitions */

/* @recomp This function has been modified to keep physical addresses strictly contigous in memory. */
void modulegetpages_impl(s32 page_count, s32 vaddr, s32 module_id) {
    s32 start;
    s32 remaining;
    s32 table_index;
    s32 cur_vaddr;
    s32 j;

    if (page_count == 0) {
        return;
    }

    /* Find a contigous block of free pages for the given page count. */
    start = module_find_contiguous_pages(page_count);

    if (start == -1) {
        errstop("module : no free pages\n");
        return;
    }

    remaining   = page_count;
    table_index = start;
    cur_vaddr   = vaddr;

    /* If there are four or more pages remaining map as many of them as a single 32KiB block. */
    while (remaining >= 4) {
        /*
         * One 4-page mapping:
         *   page_count = 4 on the first slot
         *   page_count = 0 on continuation slots
         */
        for (j = 0; j < 4; j++) {
            D_800CA668[table_index + j].module_id  = module_id;
            D_800CA668[table_index + j].page_count = (j == 0) ? 4 : 0;
            D_800CA668[table_index + j].vaddr      = cur_vaddr + j * MODULE_PAGE_SIZE;
            D_800CA668[table_index + j].paddr      = module_page_paddr(table_index + j);
        }

        table_index += 4;
        cur_vaddr   += 0x8000;
        remaining   -= 4;
    }

    /* Now handle the mappings smaller than 32KiB. */
    while (remaining > 0) {
        D_800CA668[table_index].module_id  = module_id;
        D_800CA668[table_index].page_count = 1;
        D_800CA668[table_index].vaddr      = cur_vaddr;
        D_800CA668[table_index].paddr      = module_page_paddr(table_index);

        table_index++;
        cur_vaddr += 0x2000;
        remaining--;
    }
}
