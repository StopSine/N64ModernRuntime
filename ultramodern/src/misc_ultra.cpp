#include <cstdio>

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/recomp_overrides.h"
#include "recomp.h"

// The page table the recompiled code's memory accesses resolve through. Zero
// size leaves every access flat, so a game that does not map memory pays only
// one compare.
extern "C" {
    uint64_t recomp_tlb_window_begin = 0;
    uint64_t recomp_tlb_window_size = 0;
    uint32_t recomp_tlb_page_offsets[RECOMP_TLB_MAX_PAGES];

    uint8_t* recomp_rdram_base = nullptr;
}

#define K0BASE        0x80000000
#define K1BASE        0xA0000000
#define K2BASE        0xC0000000
#define IS_KSEG0(x)   ((u32)(x) >= K0BASE && (u32)(x) < K1BASE)
#define IS_KSEG1(x)   ((u32)(x) >= K1BASE && (u32)(x) < K2BASE)
#define K0_TO_PHYS(x) ((u32)(x)&0x1FFFFFFF)
#define K1_TO_PHYS(x) ((u32)(x)&0x1FFFFFFF)

// A minimal TLB, for games that map memory rather than addressing it directly
// through KSEG0. Goemon's Great Adventure maps its overlay window at virtual
// 0x08000000 onto wherever the overlay was loaded, so its pointers are only
// meaningful once translated.
//
// osMapTLB takes physical addresses and maps a pair of pages per entry: the
// page at vaddr, then the page above it. PageMask encodes the size of that
// pair, so a single page is half of it.
namespace {
    struct TlbEntry {
        uint32_t vaddr = 0;
        uint32_t page_size = 0;
        uint32_t phys_lo = 0;   // page at vaddr
        uint32_t phys_hi = 0;   // page at vaddr + page_size
        bool valid = false;
    };
    constexpr int TlbEntryCount = 32;
    TlbEntry tlb_entries[TlbEntryCount];
}

namespace {
    uint32_t translation_vaddr = 0;

    void rebuild_translation_pages() {
        constexpr uint32_t page_size = 1u << RECOMP_TLB_PAGE_SHIFT;
        size_t page_count = (size_t)(recomp_tlb_window_size >> RECOMP_TLB_PAGE_SHIFT);

        for (size_t page = 0; page < page_count; page++) {
            uint32_t phys = ultramodern::tlb_translate(translation_vaddr + (uint32_t)page * page_size);
            recomp_tlb_page_offsets[page] = (phys != 0) ? phys : UINT32_MAX;
        }
    }
}

// Register the range the recompiled code should resolve through the TLB rather
// than address flatly. Until this is called every access stays flat.
void ultramodern::tlb_set_translation_range(uint32_t vaddr, uint32_t size) {
    size_t page_count = size >> RECOMP_TLB_PAGE_SHIFT;
    if (page_count > RECOMP_TLB_MAX_PAGES) {
        return;
    }

    translation_vaddr = vaddr;
    // A mapped address is not KSEG0, so its rdram offset is the address plus
    // the KSEG0 base rather than minus it.
    recomp_tlb_window_begin = (uint64_t)vaddr + 0x80000000ull;
    recomp_tlb_window_size = size;

    rebuild_translation_pages();
}

void ultramodern::tlb_map(int index, uint32_t page_mask, uint32_t vaddr, uint32_t phys_lo, uint32_t phys_hi) {
    if ((index < 0) || (index >= TlbEntryCount)) {
        return;
    }
    TlbEntry& e = tlb_entries[index];
    uint32_t page_size = (((page_mask | 0x1FFF) + 1) >> 1);
    bool valid = page_size != 0;

    if (e.page_size == page_size && e.vaddr == vaddr && e.phys_lo == phys_lo &&
        e.phys_hi == phys_hi && e.valid == valid) {
        return;
    }

    e.page_size = page_size;
    e.vaddr = vaddr;
    e.phys_lo = phys_lo;
    e.phys_hi = phys_hi;
    e.valid = valid;

    rebuild_translation_pages();
}

void ultramodern::tlb_unmap(uint32_t vaddr) {
    for (TlbEntry& e : tlb_entries) {
        if (e.valid && (vaddr >= e.vaddr) && (vaddr < e.vaddr + e.page_size * 2)) {
            e.valid = false;
        }
    }

    rebuild_translation_pages();
}

void ultramodern::tlb_unmap_all() {
    for (TlbEntry& e : tlb_entries) {
        e.valid = false;
    }

    rebuild_translation_pages();
}

// Returns the physical address, or 0 when nothing maps this address.
uint32_t ultramodern::tlb_translate(uint32_t vaddr) {
    for (const TlbEntry& e : tlb_entries) {
        if (!e.valid) {
            continue;
        }
        uint32_t offset = vaddr - e.vaddr;
        if (offset < e.page_size) {
            return e.phys_lo + offset;
        }
        if (offset < e.page_size * 2) {
            return e.phys_hi + (offset - e.page_size);
        }
    }
    return 0;
}

u32 osVirtualToPhysical(PTR(void) addr) {
    uintptr_t addr_val = (uintptr_t)addr;
    if (IS_KSEG0(addr_val)) {
        return K0_TO_PHYS(addr_val);
    } else if (IS_KSEG1(addr_val)) {
        return K1_TO_PHYS(addr_val);
    } else {
        // Mapped address: translate it through the TLB the game programmed.
        // Without this, an address handed to the RSP or RDP stays virtual and
        // points nowhere, which is how malformed display lists arise.
        uint32_t translated = ultramodern::tlb_translate((uint32_t)addr_val);
        if (translated != 0) {
            return translated;
        }
        return (u32)addr_val;
    }
}

