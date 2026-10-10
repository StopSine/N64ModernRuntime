#ifndef __ULTRAMODERN_RECOMP_OVERRIDES_H__
#define __ULTRAMODERN_RECOMP_OVERRIDES_H__

#include <stdint.h>

#define RECOMP_TLB_PAGE_SHIFT 12
#define RECOMP_TLB_MAX_PAGES 256

#ifdef __cplusplus
extern "C" {
#endif

extern uint64_t recomp_tlb_window_begin;
extern uint64_t recomp_tlb_window_size;
extern uint32_t recomp_tlb_page_offsets[RECOMP_TLB_MAX_PAGES];

// Published for external debuggers.
extern uint8_t* recomp_rdram_base;

#ifdef __cplusplus
}
#endif

// Translates rather than mirrors: a mirror into a flat backing loses any write
// the game makes through the mapping.
static inline uint64_t recomp_mem_offset(uint64_t addr) {
    uint64_t offset = addr - 0xFFFFFFFF80000000ull;
    uint64_t relative = offset - recomp_tlb_window_begin;

    if (relative >= recomp_tlb_window_size) {
        return offset;
    }

    uint32_t page = recomp_tlb_page_offsets[relative >> RECOMP_TLB_PAGE_SHIFT];
    if (page == UINT32_MAX) {
        return offset;
    }

    return (uint64_t)page + (relative & ((1u << RECOMP_TLB_PAGE_SHIFT) - 1));
}

#define RECOMP_MEM_OFFSET(addr) recomp_mem_offset(addr)

#endif
