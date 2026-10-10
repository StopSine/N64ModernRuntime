#ifndef __ULTRAMODERN_RECOMP_OVERRIDES_H__
#define __ULTRAMODERN_RECOMP_OVERRIDES_H__

// Reached from the stock recomp.h through RECOMP_OVERRIDES_HEADER, so N64Recomp
// itself needs no fork. Any target that compiles recompiled code, or uses the
// MEM_* macros, must define that macro or its accesses stay untranslated.

#include <stdint.h>

#define RECOMP_TLB_PAGE_SHIFT 12
#define RECOMP_TLB_MAX_PAGES 256

#ifdef __cplusplus
extern "C" {
#endif

// rdram offset of the mapped range, and its size in bytes.
extern uint64_t recomp_tlb_window_begin;
extern uint64_t recomp_tlb_window_size;
// rdram offset each page of the range currently resolves to, or UINT32_MAX when
// that page is unmapped.
extern uint32_t recomp_tlb_page_offsets[RECOMP_TLB_MAX_PAGES];

// Base of the rdram allocation, published for external debuggers.
extern uint8_t* recomp_rdram_base;

#ifdef __cplusplus
}
#endif

// A game that maps memory through the TLB rather than addressing it through
// KSEG0 needs its mapped range translated on every access, because the flat
// rdram model has no other way to reach the pages behind it. Translating here
// keeps one copy of the data; mirroring the mapped range into a flat backing
// instead loses any write the game makes through the mapping.
//
// recomp_tlb_window_size is zero unless a game registers a range, so the test
// below folds to a single compare that is never taken.
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

// The byte and halfword swizzles only touch the low two bits, so recomp.h
// applying them after translation stays within the same page.
#define RECOMP_MEM_OFFSET(addr) recomp_mem_offset(addr)

#endif
