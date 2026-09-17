#include "librecomp/overlays.hpp"
#include <ultramodern/ultra64.h>
#include <ultramodern/ultramodern.hpp>
#include "recomp.h"

// None of these functions need to be reimplemented, so stub them out
extern "C" void osMapTLB_recomp(uint8_t* rdram, recomp_context* ctx) {
    // void osMapTLB(s32 index, OSPageMask pm, void *vaddr, u32 odd, u32 even, s32 asid)
    //
    // The pair's lower page is the `odd` argument and the upper one is `even`,
    // which reads oddly but is what the hardware register order produces.
    ultramodern::tlb_map((int)ctx->r4, (uint32_t)ctx->r5, (uint32_t)ctx->r6,
                         (uint32_t)ctx->r7, (uint32_t)MEM_W(0x10, ctx->r29));
    // The window now points somewhere else; rebuild what it resolves to.
    recomp::overlays::alias_loaded_sections_to_mapping();
}

extern "C" void osUnmapTLB_recomp(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::tlb_unmap((uint32_t)ctx->r4);
}

extern "C" void osUnmapTLBAll_recomp(uint8_t * rdram, recomp_context * ctx) {
    ultramodern::tlb_unmap_all();
    // TODO this will need to be implemented in the future for any games that actually use the TLB
}

extern "C" void osVoiceInit_recomp(uint8_t * rdram, recomp_context * ctx) {
    ctx->r2 = 11; // CONT_ERR_DEVICE
}

extern "C" void osVoiceSetWord_recomp(uint8_t * rdram, recomp_context * ctx) {
    assert(false);
}

extern "C" void osVoiceCheckWord_recomp(uint8_t * rdram, recomp_context * ctx) {
    assert(false);
}

extern "C" void osVoiceStopReadData_recomp(uint8_t * rdram, recomp_context * ctx) {
    assert(false);
}

extern "C" void osVoiceMaskDictionary_recomp(uint8_t * rdram, recomp_context * ctx) {
    assert(false);
}

extern "C" void osVoiceStartReadData_recomp(uint8_t * rdram, recomp_context * ctx) {
    assert(false);
}

extern "C" void osVoiceControlGain_recomp(uint8_t * rdram, recomp_context * ctx) {
    assert(false);
}

extern "C" void osVoiceGetReadData_recomp(uint8_t * rdram, recomp_context * ctx) {
    assert(false);
}

extern "C" void osVoiceClearDictionary_recomp(uint8_t * rdram, recomp_context * ctx) {
    assert(false);
}

