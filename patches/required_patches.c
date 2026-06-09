#include "patches.h"
#include "misc_funcs.h"
#include "PR/os_pi.h"

unsigned long long g_dummy = 0x0123456789ABCDEFllu;

extern void* D_80097D10;
extern OSMesgQueue D_80097D30;
extern s32 D_80097D48;
extern OSMesgQueue D_80097D68;
extern void* D_80097D80;
extern OSIoMesg D_80097D50;

// ------------------------
// DMA functions
// ------------------------

RECOMP_PATCH void dmaInit(void) {
    D_80097D48 = 0;
    osCreatePiManager_recomp(0x96, &D_80097D30, &D_80097D10, 8);
    osCreateMesgQueue(&D_80097D68, &D_80097D80, 1);
}

RECOMP_PATCH void dmaCheckTrans(void) {
    osRecvMesg(&D_80097D68, NULL, 0);
}

// formerly load_from_rom_to_addr
RECOMP_PATCH void dmaRead(void* vAddr, s32 size, u32 devAddr) {
    if (D_80097D48 != 0) {
        do {
            osYieldThread();
        } while (D_80097D48 != 0);
    }

    recomp_printf("[dmaRead] vaddr 0x%08X size 0x%08X devAddr 0x%08X\n", (u32)vAddr, size, devAddr);

    D_80097D48 = 1;
    osWritebackDCache(vAddr, size);
    osInvalDCache(vAddr, size);
    osInvalICache(vAddr, size);
    osPiStartDma(&D_80097D50, 0, 0, devAddr, vAddr, (u32) size, &D_80097D68);
    osRecvMesg(&D_80097D68, NULL, 1);
    D_80097D48 = 0;
}

RECOMP_PATCH void dmaWrite(s32 arg0, u32 arg1, void* arg2) {
    if (D_80097D48 != 0) {
        do {
            osYieldThread();
        } while (D_80097D48 != 0);
    }
    recomp_printf("[dmaWrite] arg0 0x%08X arg1 0x%08X arg2 0x%08X\n", arg0, arg1, (u32)arg2);

    D_80097D48 = 1;
    osWritebackDCache(arg2, arg0);
    osPiStartDma(&D_80097D50, 0, 1, (u32) arg0, arg2, arg1, &D_80097D68);
    osRecvMesg(&D_80097D68, NULL, 1);
    D_80097D48 = 0;
}

RECOMP_PATCH void dmaWaitBlock(void) {
    osRecvMesg(&D_80097D68, NULL, 1);
}

RECOMP_PATCH void dmaReadNonBlock(void* arg0, u32 arg1, u32 arg2) {
    recomp_printf("[dmaReadNonBlock] arg0 0x%08X arg1 0x%08X arg2 0x%08X\n", (u32)arg0, (u32)arg1, (u32)arg2);
    osPiStartDma(&D_80097D50, 0, 0, arg2, arg0, arg1, &D_80097D68);
}

RECOMP_PATCH void dmaWriteNonBlock(u32 arg0, u32 arg1, void* arg2) {
    recomp_printf("[dmaWriteNonBlock] arg0 0x%08X arg1 0x%08X arg2 0x%08X\n", (u32)arg0, (u32)arg1, (u32)arg2);
    osPiStartDma(&D_80097D50, 0, 1, arg0, arg2, arg1, &D_80097D68);
}

RECOMP_PATCH void dmaStart(void* arg0, u32 arg1, u32 arg2, s32 arg3) {
    recomp_printf("[dmaStart] arg0 0x%08X arg1 0x%08X arg2 0x%08X arg3 0x%08X\n", (u32)arg0, (u32)arg1, (u32)arg2, (u32)arg3);
    osPiStartDma(&D_80097D50, 0, arg3, arg2, arg0, arg1, &D_80097D68);
}

// ---------------------------------
// FBLOCK
// ---------------------------------

extern void errstop(const char *fmt, ...);

typedef struct HuFILE {
    /* 0x00 */ long unk0;
    /* 0x04 */ u8 *unk4;
    /* 0x08 */ unsigned long pos;
    /* 0x0C */ long unkC;
    /* 0x10 */ long unk10;
} HuFILE;

struct UnkStruct800EE2C8 {
    /* 0x00 */ long unk0;
    /* 0x00 */ long unk4;
};

extern struct UnkStruct800EE2C8 D_800EE2C8[];
extern HuFILE D_800EEAC8[4];

#define SEEK_SET 0 // It denotes the end of the file.
#define SEEK_CUR 1 // It denotes starting of the file.
#define SEEK_END 2 // It denotes the file pointer's current position.

void finit_game(s32 arg0);
int fsize_game(HuFILE *stream);
HuFILE *fopen_game(u32 arg0, s32 arg1);
u32 fcreate_game(void);
void fclose_game(HuFILE* stream);
int fgetc_game(HuFILE* stream);
size_t fread_game(u8 *buffer, size_t size, size_t count, HuFILE *stream);
void readromtobuff_game(HuFILE* stream, s32 pos);
int fseek_game(HuFILE *pointer, long int offset, int position);
long ftell_game(HuFILE* stream);

// fblock.c

extern void decode(HuFILE *arg0, u8* arg1, s32 arg2);

extern const char *D_80094ADC;
extern const char *D_80094B04;

extern s32 D_800A0130;
extern u32 D_800A0134;

extern s32 D_800A0138;
extern s32 (*D_800A013C)();

extern const char *D_80094A90;
extern const char *D_80094AB8;

extern u8 D_800F0250[][3];
extern u8 D_800EF250[][2];



extern s32 D_8008E750[];

// FUNCTIONS

void info_setup_export(void) {
    dmaRead(&D_800EF250, 0x1000, 0xFF000);
}

void fchangeblockaddr_export(s32 arg0) {
    D_800A0134 = arg0;
    finit_game(arg0);
}

u32 fexecLoadAddress(s32 id, u32 (*func)());

RECOMP_PATCH void fexecLoad(s32 id) {
    if (id != D_800A0138) {
        D_800A0138 = id;
        D_800A013C = (void*)fexecLoadAddress(id, (void*)0x80250000);
    }
}

extern s32 func_ovl_menu_title_60000000(void);
extern void overlay_apply_relocations(u32 file_id, u8 *load_addr);

struct UnkStruct800CA668 {
    long unk0;          // Module identifier?
    long page_count;    // How many 4KB pages this mapping takes up (for example, 1 means the pages each have a size of 4KB, 4 means they have a size of 16KB, etc.)
    long virtual_addr;  // Virtual address (KUSEG) of the overlay
    long physical_addr; // Physical address of the overlay, corresponds to the KSEG0 address with the first bit being zero (for example, physical is 0x00250000 and KSEG0 is 0x80250000)
};

extern struct UnkStruct800CA668 D_800CA668[64];

s32 tlb_resolve_virtual_to_physical(s32 virtual_addr) {
    s32 current_page = 0;

    while (current_page < 64) {
        if (D_800CA668[current_page].virtual_addr == virtual_addr) {
            return D_800CA668[current_page].physical_addr;
        }
        
        if (D_800CA668[current_page].page_count != 0) {
            current_page += D_800CA668[current_page].page_count;
        } else {
            current_page++;
        }

    }

    return -1;
}

// Implemented natively in src/game/recomp_api.cpp. Allocates (or reuses) a
// dedicated 256 KB slot in rdram for the overlay identified by virtual_addr,
// and returns the host-side KSEG0 physical address of that slot.
//
// This works around the fact that the game's TLB allocator (modulegetfreepages)
// only hands out 8 KB pages and lays overlays only 32 KB apart, which is not
// enough room for several real overlays (e.g. ovl_coll_main is ~40 KB once
// .data/.bss are counted). With the original layout, loading one overlay
// silently corrupted the data section of the previously loaded overlay
// occupying the next slot in the TLB region.
extern u32 recomp_overlay_slot_allocate(u32 virtual_addr, s32 file_id);

RECOMP_PATCH u32 fexecLoadAddress(s32 id, u32 (*func)()) {
    HuFILE *stream;
    s32 i;
    int evfile_found;
    s32 temp_a2;
    s32 pad[5];
    int size = 0x2A0000;
    int fileID = 0;
    u32 (*funcBackup)();

    recomp_printf("[fexecLoadAddress] id 0x%08X func 0x%08X\n", id, (u32)func);

    temp_a2 = (D_800EF250[id][0] << 0x11) + (D_800EF250[id][1] << 0xB);
    if (temp_a2 != D_800A0134) {
        D_800A0134 = temp_a2;
        finit_game(temp_a2);
    }
    stream = fopen_game(0, 1);
    i = fsize_game(stream);

    if (i >= 0x202) {
        errstop(D_80094A90, D_800EF250[id][0]); // "too long file size : evinfo : block %d\n"
    }

    fread_game((void*)&D_800F0250, 1, i, stream);

    for (i = 0; i < D_800F0250[0][0]; i++) {
        evfile_found = (D_800F0250[i][1] << 8) + D_800F0250[i][2];
        if (evfile_found == id) {
            evfile_found = D_800F0250[i + 1][0];
            fileID = D_800F0250[i][2];
            break;
        }
    }

    if (i >= D_800F0250[0][0]) {
        errstop(D_80094AB8, id); // "Can not find evfile number : %d\n"
    }

    fclose_game(stream);
    stream = fopen_game(evfile_found, 1);

    recomp_printf("[fexecLoadAddress] [file] id   0x%08X\n", stream->unk0);
    recomp_printf("[fexecLoadAddress] [file] ptr  0x%08X\n", stream->unk4);
    recomp_printf("[fexecLoadAddress] [file] pos  0x%08X\n", stream->pos);
    recomp_printf("[fexecLoadAddress] [file] size 0x%08X\n", stream->unkC);
    recomp_printf("[fexecLoadAddress] [file] mode 0x%08X\n", stream->unk10);

    fread_game((u8*)&i, 4, 1, stream);
    fseek_game(stream, 4, 1);
    osInvalICache((void*)(((u32) ((u32)func + 0xF) >> 4) * 0x10), 0x80000);
    osInvalDCache((void*)(((u32) ((u32)func + 0xF) >> 4) * 0x10), 0x80000);

    recomp_printf("[fexecLoadAddress] executing decode with args: 0x%08X, 0x%08X, 0x%08X\n", (u32)stream, (u32)func, i);

    funcBackup = func;
    recomp_printf("[fexecLoadAddress] func pre-TLB lookup: 0x%08X\n", (u32)func);
    func = (u32 (*)())(u32)recomp_overlay_slot_allocate((u32)funcBackup, fileID);
    recomp_printf("[fexecLoadAddress] func after-TLB lookup: 0x%08X\n", (u32)func);

    // TODO: Rest of overlays
    switch((u32)stream->unk4) {
        case 0x10800C: recomp_load_overlays(0x1000010, func, i); break; // ovl_actor_main
        case 0x12800C: recomp_load_overlays(0x10201E0, func, i); break; // ovl_item_main
        case 0x13800C: recomp_load_overlays(0x10273D0, func, i); break; // ovl_coll_main
        case 0x14800C: recomp_load_overlays(0x102E100, func, i); break; // ovl_mobj_main
        case 0x16801E: recomp_load_overlays(0x1040E10, func, i); break; // ovl_blast_flame
        case 0x169E50: recomp_load_overlays(0x1044470, func, i); break; // ovl_blast_ice
        case 0x16C2FA: recomp_load_overlays(0x1048430, func, i); break; // ovl_blast_wind
        case 0x16EB1E: recomp_load_overlays(0x104C8E0, func, i); break; // ovl_blast_ground
        case 0x17150C: recomp_load_overlays(0x1050FF0, func, i); break; // ovl_blast_elec
        case 0x173D0A: recomp_load_overlays(0x1055450, func, i); break; // ovl_blast_light
        case 0x176344: recomp_load_overlays(0x1059700, func, i); break; // ovl_blast_dark
        case 0x188024: recomp_load_overlays(0x105DE50, func, i); break; // ovl_enemy_world1
        case 0x18E7F8: recomp_load_overlays(0x10685D0, func, i); break; // ovl_enemy_world2
        case 0x195020: recomp_load_overlays(0x1072EF0, func, i); break; // ovl_enemy_world3
        case 0x19B6DA: recomp_load_overlays(0x107D500, func, i); break; // ovl_enemy_world4
        case 0x1A1946: recomp_load_overlays(0x1087490, func, i); break; // ovl_enemy_world5
        case 0x1A7EBC: recomp_load_overlays(0x1091710, func, i); break; // ovl_enemy_world6
        case 0x1AE4CC: recomp_load_overlays(0x109BBF0, func, i); break; // ovl_enemy_world7
        case 0x1B49AE: recomp_load_overlays(0x10A5E80, func, i); break; // ovl_enemy_world8
        case 0x1BB4E2: recomp_load_overlays(0x10B0AF0, func, i); break; // ovl_enemy_battle
        case 0x1E8012: recomp_load_overlays(0x10BA3A0, func, i); break; // ovl_boss_demon
        case 0x1EEFFC: recomp_load_overlays(0x10C4B90, func, i); break; // ovl_boss_devil
        case 0x1F6EAA: recomp_load_overlays(0x10D0330, func, i); break; // ovl_boss_angel
        case 0x22801C: recomp_load_overlays(0x10DC340, func, i); break; // ovl_menu_card
        case 0x229D0E: recomp_load_overlays(0x10DFA80, func, i); break; // ovl_menu_title
        case 0x22C92E: recomp_load_overlays(0x10E4440, func, i); break; // ovl_menu_file
        case 0x22E668: recomp_load_overlays(0x10E77A0, func, i); break; // ovl_menu_battle
        case 0x236118: recomp_load_overlays(0x10F37E0, func, i); break; // ovl_menu_custom
        case 0x239668: recomp_load_overlays(0x10F8F10, func, i); break; // ovl_menu_stage
        case 0x248010: recomp_load_overlays(0x10FF850, func, i); break; // ovl_demo_story
        case 0x24C094: recomp_load_overlays(0x1107250, func, i); break; // ovl_demo_guide
        case 0x258022: recomp_load_overlays(0x1108F70, func, i); break; // ovl_stage_world1
        case 0x25A13E: recomp_load_overlays(0x110D180, func, i); break; // ovl_stage_world2
        case 0x25D006: recomp_load_overlays(0x1112E30, func, i); break; // ovl_stage_world3
        case 0x2608A6: recomp_load_overlays(0x11198D0, func, i); break; // ovl_stage_world4
        case 0x263C62: recomp_load_overlays(0x1120E00, func, i); break; // ovl_stage_world5
        case 0x265A32: recomp_load_overlays(0x1124A30, func, i); break; // ovl_stage_world6
        case 0x269008: recomp_load_overlays(0x112B010, func, i); break; // ovl_stage_world7
        case 0x26BF18: recomp_load_overlays(0x1130B60, func, i); break; // ovl_stage_world8
        default:
            if ((u32)func >= 0x40000000 && (u32)func <= 0x60000000) {
                recomp_printf("[fexecLoadAddress] Unrecognized load from 0x%08X func 0x%08X (size 0x%08X)\n", stream->unk4, (u32)func, i);
            }
            break;
    }

    recomp_printf("[fexecLoadAddress] decode stream 0x%08X func 0x%08X (size 0x%08X)\n", stream, (u32)func, i);

    decode(stream, (u8*)func, i);

    overlay_apply_relocations(fileID, (u8*)func);

    osWritebackDCache((void*)(((u32) ((u32)func + 0xF) >> 4) * 0x10), 0x80000);
    fclose_game(stream);
    if (D_800A0134 != size) {
        D_800A0134 = size;
        finit_game(size);
    }
    return func();
}

RECOMP_PATCH s32 fexecGetFileSize(s32 id) {
    HuFILE *stream;
    s32 i;
    s32 sp3C;
    int evfile_found;
    s32 temp_a2;
    s32 pad[4];
    int size = 0x2A0000;

    temp_a2 = (D_800EF250[id][0] << 0x11) + (D_800EF250[id][1] << 0xB);
    if (temp_a2 != D_800A0134) {
        D_800A0134 = temp_a2;
        finit_game(temp_a2);
    }
    stream = fopen_game(0, 1);
    i = fsize_game(stream);

    if (i >= 0x202) {
        errstop(D_80094ADC, D_800EF250[id][0]);
    }

    fread_game((void*)&D_800F0250, 1, i, stream);

    for (i = 0; i < D_800F0250[0][0]; i++) {
        evfile_found = (D_800F0250[i][1] << 8) + D_800F0250[i][2];
        if (evfile_found == id) {
            evfile_found = D_800F0250[i + 1][0];
            break;
        }
    }

    if (i >= D_800F0250[0][0]) {
        errstop(D_80094B04, id);
    }

    fclose_game(stream);
    stream = fopen_game(evfile_found, 1);
    fread_game((u8*)&i, 4, 1, stream);
    fread_game((u8*)&sp3C, 4, 1, stream);
    fclose_game(stream);

    if (D_800A0134 != size) {
        D_800A0134 = size;
        finit_game(size);
    }
    return i + sp3C;
}

RECOMP_PATCH void fexecCall(void) {
    D_800A013C();
}

RECOMP_PATCH void fexecInit(void) {
    int size = 0x2A0000;

    zjSetVec(4, D_8008E750);
    D_800A0130 = -1;
    D_800A0134 = -1;
    D_800A0138 = -1;
    if (D_800A0130 == -1) {
        info_setup_export();
        D_800A0130 = 0;
    }
    if (D_800A0134 != size) {
        D_800A0134 = size;
        finit_game(0x2A0000);
    }
}

RECOMP_PATCH void fexecChangeBlock(s32 arg0) {
    u32 temp = arg0 << 0x11;

    if (D_800A0134 != temp) {
        fchangeblockaddr_export(temp);
    }
}

// PATCH ZEROJMP LOAD

extern u32 zerojump_ROM_START;
extern u32 zerojump_ROM_END;

extern u32 D_8008E640[];
extern u32 D_80097910[];

RECOMP_PATCH void zerojumpinit(void) {
    dmaRead((void*)0x801D0000, (u32)&zerojump_ROM_END - (u32)&zerojump_ROM_START, (u32)&zerojump_ROM_START);
    recomp_load_overlays(0x00098510, (void*)0x10000000, 0x130); // load zerojmp manually to RAM with a hack.
    D_80097910[0] = (u32)&D_8008E640; // map the secure call manually.
}

#if 0

// decode.c

extern void *malloc(s32);
extern void free(void *);
extern void hmemset(u8 *, s32, s32);

extern s32 D_8008E760;
extern u8* D_800A0140;
extern u16 D_800A0144;

RECOMP_PATCH void decode(HuFILE *arg0, u8* arg1, s32 arg2) {
    s32 temp_a0;
    s32 temp_v0_3;
    s32 i;
    u16 var_s5;
    s32 temp_s4;

    D_8008E760 = 1;
    D_800A0140 = malloc(0x400);
    D_800A0144 = 0x3BE;
    hmemset(D_800A0140, 0, 0x400);
    var_s5 = 0;
    while (arg2 > 0) {
        var_s5 >>= 1;
        if (!(var_s5 & 0x100)) {
            var_s5 = fgetc_game(arg0) | 0xFF00;
        }
        if (var_s5 & 1) {
            D_800A0140[D_800A0144] = *arg1++ = fgetc_game(arg0);
            arg2--;
            D_800A0144++;
            D_800A0144 &= 0x3FF;
        } else {
            temp_s4 = fgetc_game(arg0);
            temp_v0_3 = fgetc_game(arg0);
            temp_s4 |= ((temp_v0_3 & ~0x3F) * 4);
            temp_a0 = (temp_v0_3 & 0x3F) + 3;

            // Cap the copy length so we don't write past the output buffer
            if (temp_a0 > arg2) {
                temp_a0 = arg2;
            }

            for (i = 0; i < temp_a0; i++) {
                D_800A0140[D_800A0144] = *arg1++ = D_800A0140[(temp_s4 + i) & 0x3FF];
                D_800A0144++;
                D_800A0144 &= 0x3FF;
            }
            arg2 -= i;
        }
    }
    free(D_800A0140);
    D_8008E760 = 0;
}

#endif

struct UnkStruct80008F90 {
    char filler0[0x4];
    u32 unk4;
    u32 unk8;
    u32 unkC;
    u32 unk10;
};

// Sets screen coordinates
RECOMP_PATCH void func_80008F90(struct UnkStruct80008F90* arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4) {
    arg1 = 0;
    arg2 = 0;
    arg3 = 320;
    arg4 = 240;
    arg0->unk4 = arg1;
    arg0->unk8 = arg2;
    arg0->unkC = arg3;
    arg0->unk10 = arg4;
}
