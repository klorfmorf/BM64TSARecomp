#ifndef TLB_H
#define TLB_H

#define OS_PM_4K	0x0000000
#define OS_PM_16K	0x0006000
#define OS_PM_64K	0x001E000
#define OS_PM_256K	0x007E000
#define OS_PM_1M	0x01FE000
#define OS_PM_4M	0x07FE000
#define OS_PM_16M	0x1FFE000

typedef uint32_t OSPageMask;

// TODO
struct TLBEntry {
    OSPageMask pm;
    uint32_t pagesize; // pre-computed for performance reasons
    uint32_t vaddr;
    uint32_t evenpaddr;
    uint32_t oddpaddr;
};

#define TLB_ENTRY_COUNT 32

extern struct TLBEntry gTLBTable[TLB_ENTRY_COUNT];

#endif // TLB_H
