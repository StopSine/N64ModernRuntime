#include "recomp.h"
#include <cstdio>
#include <string>
#include <ultramodern/ultra64.h>
#include <ultramodern/ultramodern.hpp>
#include "librecomp/addresses.hpp"

#define VI_NTSC_CLOCK 48681812

extern "C" void osAiSetFrequency_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint32_t freq = ctx->r4;
    // This makes actual audio frequency more accurate to console, but may not be desirable
    uint32_t dacRate = (uint32_t)(((float)VI_NTSC_CLOCK / freq) + 0.5f);
    freq = VI_NTSC_CLOCK / dacRate;
    ctx->r2 = freq;
    ultramodern::set_audio_frequency(freq);
}

extern "C" void osAiSetNextBuffer_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint32_t addr = (uint32_t)ctx->r4;
    uint32_t len = (uint32_t)ctx->r5;

    {
        static int n = 0;
        if (n++ < 10) {
            fprintf(stderr, "[ai] queue buffer 0x%08X len 0x%X\n", addr, len);
        }
    }

    // Resolve and bounds check before anything dereferences this. TO_PTR maps a
    // guest address to rdram + (addr - 0x80000000) without masking, and rdram is
    // PAGE_NOACCESS above mem_size, so an address outside cached rdram faults
    // inside queue_samples rather than failing here -- observed as an access
    // violation at main.cpp:222. Resolving through osVirtualToPhysical also
    // makes a KSEG1 or TLB-mapped buffer work instead of crashing.
    uint32_t phys = osVirtualToPhysical((int32_t)addr);

    if (len == 0 || (len & 1) != 0 || (uint64_t)phys + len > recomp::mem_size) {
        static int reported = 0;
        if (reported++ < 32) {
            fprintf(stderr, "[ai] rejected buffer 0x%08X (phys 0x%08X) len 0x%X\n", addr, phys, len);
        }
        ctx->r2 = 0;
        return;
    }

    ultramodern::queue_audio_buffer(rdram, (int32_t)(phys | 0x80000000u), len);
    ctx->r2 = 0;
}

extern "C" void osAiGetLength_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = ultramodern::get_remaining_audio_bytes();
}

extern "C" void osAiGetStatus_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 0x00000000; // Pretend the audio DMAs finish instantly
}
