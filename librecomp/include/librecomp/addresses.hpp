#ifndef __RECOMP_ADDRESSES_HPP__
#define __RECOMP_ADDRESSES_HPP__

#include <cstdint>
#include "ultramodern/ultra64.h"
#include "recomp.h"

namespace recomp {
    // 512GB (kseg0 size)
    constexpr size_t mem_size = 512ULL * 1024ULL * 1024ULL;
    // 4GB (the full address space)
    constexpr size_t allocation_size = 4096ULL * 1024ULL * 1024ULL;

    // Hardware register window, made accessible as scratch rather than left
    // unmapped. Nothing models these registers, but a game that touches them
    // otherwise faults, because the address lands above mem_size in the
    // reserved space. Reads then return zero and writes are discarded, which
    // is what "unmodelled hardware" should look like; Goemon's Great Adventure
    // programs the VI, PI and SI directly during startup.
    //
    // 0xA4000000..0xA5000000 covers SP, DP, VI, AI, PI and SI.
    constexpr size_t mmio_scratch_offset = 0xA4000000ULL - 0x80000000ULL;
    constexpr size_t mmio_scratch_size = 0x01000000ULL;

    // Window backing a game's TLB-mapped overlay area. MEM_* maps a guest
    // address to rdram + (addr - 0x80000000), so guest 0x08000000 lands here.
    // Goemon's Great Adventure keeps its overlays at 0x08000000 and derives
    // offsets by masking that address, so the section has to stay at its link
    // base and the window has to be readable for the code to run from it.
    constexpr size_t overlay_window_guest = 0x08000000ULL;
    constexpr size_t overlay_window_offset = overlay_window_guest - 0x80000000ULL + 0x100000000ULL;
    constexpr size_t overlay_window_size = 0x00040000ULL;
    static_assert(overlay_window_offset + overlay_window_size <= allocation_size);
    static_assert(mmio_scratch_offset + mmio_scratch_size <= allocation_size);
    static_assert(mmio_scratch_offset >= mem_size, "the scratch window must sit above real rdram");
    // We need a place in rdram to hold the PI handles, so pick an address in extended rdram
    constexpr int32_t cart_handle = 0x80800000;
    constexpr int32_t drive_handle = (int32_t)(cart_handle + sizeof(OSPiHandle));
    constexpr int32_t flash_handle = (int32_t)(drive_handle + sizeof(OSPiHandle));
    constexpr int32_t flash_handle_end = (int32_t)(flash_handle + sizeof(OSPiHandle));
    constexpr int32_t patch_rdram_start = 0x80801000;
    static_assert(patch_rdram_start >= flash_handle_end);
    constexpr int32_t mod_rdram_start = 0x81000000;

    // Flashram occupies the same physical address as sram, but that issue is avoided because libultra exposes
    // a high-level interface for flashram. Because that high-level interface is reimplemented, low level accesses
    // that involve physical addresses don't need to be handled for flashram.
    constexpr uint32_t sram_base = 0x08000000;
    constexpr uint32_t rom_base = 0x10000000;
    constexpr uint32_t drive_base = 0x06000000;

    void register_heap_exports();
    void init_heap(uint8_t* rdram, uint32_t address);
    void* alloc(uint8_t* rdram, size_t size);
    void free(uint8_t* rdram, void* mem);
}

extern "C" void recomp_alloc(uint8_t* rdram, recomp_context* ctx);
extern "C" void recomp_free(uint8_t* rdram, recomp_context* ctx);

#endif
