#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"

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

void ultramodern::tlb_map(int index, uint32_t page_mask, uint32_t vaddr, uint32_t phys_lo, uint32_t phys_hi) {
    if ((index < 0) || (index >= TlbEntryCount)) {
        return;
    }
    TlbEntry& e = tlb_entries[index];
    e.page_size = (((page_mask | 0x1FFF) + 1) >> 1);
    e.vaddr = vaddr;
    e.phys_lo = phys_lo;
    e.phys_hi = phys_hi;
    e.valid = e.page_size != 0;
}

void ultramodern::tlb_unmap(uint32_t vaddr) {
    for (TlbEntry& e : tlb_entries) {
        if (e.valid && (vaddr >= e.vaddr) && (vaddr < e.vaddr + e.page_size * 2)) {
            e.valid = false;
        }
    }
}

void ultramodern::tlb_unmap_all() {
    for (TlbEntry& e : tlb_entries) {
        e.valid = false;
    }
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

