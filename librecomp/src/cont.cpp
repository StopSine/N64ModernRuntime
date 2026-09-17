#include "ultramodern/ultramodern.hpp"

#include "helpers.hpp"

#define MAXCONTROLLERS 4

extern "C" void recomp_set_current_frame_poll_id(uint8_t* rdram, recomp_context* ctx) {
    // TODO reimplement the system for tagging polls with IDs to handle games with multithreaded input polling.
}

extern "C" void recomp_measure_latency(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::measure_input_latency();
}

// Raw SI DMA and a minimal joybus, for games that talk to the PIF directly.
//
// The osCont* entry points above cover games whose libultra controller calls
// are named, but Goemon's Great Adventure has no decompilation and its own
// controller layer reaches __osSiRawStartDma instead, which programs the SI
// registers at 0xA4800000. MMIO is not modelled, so the recompiled body
// faults. Implementing it here keeps the game on the runtime's input state.
//
// Only what GGA's boot path needs is emulated: controller identification and
// button/stick reads. Controller-pak and EEPROM commands report "no device",
// so raw-SI saving is not supported yet; games reaching those will need the
// remaining joybus commands filled in.

static uint8_t pif_ram[64];

// Joybus result flags, ORed into a command block's rx length byte.
constexpr uint8_t JOYBUS_ERR_NO_DEVICE = 0x80;

// libultra's direction values; the runtime headers do not define them and
// pi.cpp compares against the literals too.
constexpr s32 SI_DIR_READ = 0;   // OS_READ
constexpr s32 SI_DIR_WRITE = 1;  // OS_WRITE

static void joybus_process() {
    int channel = 0;
    size_t i = 0;

    OSContPad pads[MAXCONTROLLERS];
    osContGetReadData(pads);

    // The buffer holds a sequence of command blocks; the final byte is the
    // control byte rather than command data.
    while (i < sizeof(pif_ram) - 1) {
        uint8_t tx = pif_ram[i];

        if (tx == 0x00) {        // skip this channel
            channel++;
            i++;
            continue;
        }
        if (tx == 0xFE) {        // end of command list
            break;
        }
        if (tx == 0xFD || tx == 0xFF) {  // channel reset / padding
            i++;
            continue;
        }

        if (i + 1 >= sizeof(pif_ram) - 1) {
            break;
        }
        uint8_t rx = pif_ram[i + 1] & 0x3F;
        size_t cmd_off = i + 2;
        size_t res_off = cmd_off + tx;
        if (res_off + rx > sizeof(pif_ram) - 1) {
            break;
        }

        uint8_t cmd = pif_ram[cmd_off];
        bool present = channel == 0 && channel < MAXCONTROLLERS && pads[channel].err_no == 0;

        if (!present) {
            pif_ram[i + 1] |= JOYBUS_ERR_NO_DEVICE;
        }
        else switch (cmd) {
            case 0x00:  // request info
            case 0xFF:  // reset and request info
                if (rx >= 3) {
                    // Type 0x0500 is a standard controller; no pak attached.
                    pif_ram[res_off + 0] = 0x05;
                    pif_ram[res_off + 1] = 0x00;
                    pif_ram[res_off + 2] = 0x00;
                }
                break;
            case 0x01:  // read button and stick state
                if (rx >= 4) {
                    uint16_t button = (uint16_t)pads[channel].button;
                    pif_ram[res_off + 0] = (uint8_t)(button >> 8);
                    pif_ram[res_off + 1] = (uint8_t)(button & 0xFF);
                    pif_ram[res_off + 2] = (uint8_t)pads[channel].stick_x;
                    pif_ram[res_off + 3] = (uint8_t)pads[channel].stick_y;
                }
                break;
            default:    // controller pak (0x02/0x03) and EEPROM (0x04/0x05)
                pif_ram[i + 1] |= JOYBUS_ERR_NO_DEVICE;
                break;
        }

        channel++;
        i = res_off + rx;
    }
}

extern "C" void __osSiRawStartDma_recomp(uint8_t* rdram, recomp_context* ctx) {
    // s32 __osSiRawStartDma(s32 dir, void *dramAddr)
    s32 dir = _arg<0, s32>(rdram, ctx);
    PTR(void) dram_addr = _arg<1, PTR(void)>(rdram, ctx);

    if (dir == SI_DIR_WRITE) {
        // RDRAM to PIF: take the command block and answer it.
        for (size_t i = 0; i < sizeof(pif_ram); i++) {
            pif_ram[i] = MEM_B((int32_t)i, dram_addr);
        }
        joybus_process();
    }
    else {
        // PIF to RDRAM: hand back the answers.
        for (size_t i = 0; i < sizeof(pif_ram); i++) {
            MEM_B((int32_t)i, dram_addr) = pif_ram[i];
        }
    }

    // Hardware raises the SI interrupt on completion and the caller waits on
    // that event, exactly as the PI raw path does.
    ultramodern::send_si_message();

    _return<s32>(ctx, 0);
}

extern "C" void osContInit_recomp(uint8_t* rdram, recomp_context* ctx) {
    PTR(OSMesgQueue) mq = _arg<0, PTR(OSMesgQueue)>(rdram, ctx);
    PTR(u8) bitpattern = _arg<1, PTR(u8)>(rdram, ctx);
    PTR(OSContStatus) data = _arg<2, PTR(OSContStatus)>(rdram, ctx);
    u8 bitpattern_local = 0;

    s32 ret = osContInit(PASS_RDRAM mq, &bitpattern_local, data);

    MEM_B(0, bitpattern) = bitpattern_local;

    _return<s32>(ctx, ret);
}

extern "C" void osContReset_recomp(uint8_t* rdram, recomp_context* ctx) {
    PTR(OSMesgQueue) mq = _arg<0, PTR(OSMesgQueue)>(rdram, ctx);
    PTR(OSContStatus) data = _arg<1, PTR(OSContStatus)>(rdram, ctx);

    s32 ret = osContReset(PASS_RDRAM mq, data);

    _return<s32>(ctx, ret);
}

extern "C" void osContStartReadData_recomp(uint8_t* rdram, recomp_context* ctx) {
    PTR(OSMesgQueue) mq = _arg<0, PTR(OSMesgQueue)>(rdram, ctx);

    s32 ret = osContStartReadData(PASS_RDRAM mq);

    _return<s32>(ctx, ret);
}

extern "C" void osContGetReadData_recomp(uint8_t* rdram, recomp_context* ctx) {
    PTR(OSContPad) data = _arg<0, PTR(OSContPad)>(rdram, ctx);

    OSContPad dummy_data[MAXCONTROLLERS];

    osContGetReadData(dummy_data);

    for (int controller = 0; controller < MAXCONTROLLERS; controller++) {
        if (dummy_data[controller].err_no == 0) {
            MEM_H(6 * controller + 0, data) = dummy_data[controller].button;
            MEM_B(6 * controller + 2, data) = dummy_data[controller].stick_x;
            MEM_B(6 * controller + 3, data) = dummy_data[controller].stick_y;
            MEM_B(6 * controller + 4, data) = dummy_data[controller].err_no;
        }
    }
}

extern "C" void osContStartQuery_recomp(uint8_t * rdram, recomp_context * ctx) {
    PTR(OSMesgQueue) mq = _arg<0, PTR(OSMesgQueue)>(rdram, ctx);

    s32 ret = osContStartQuery(PASS_RDRAM mq);

    _return<s32>(ctx, ret);
}

extern "C" void osContGetQuery_recomp(uint8_t * rdram, recomp_context * ctx) {
    PTR(OSContStatus) data = _arg<0, PTR(OSContStatus)>(rdram, ctx);

    osContGetQuery(PASS_RDRAM data);
}

extern "C" void osContSetCh_recomp(uint8_t* rdram, recomp_context* ctx) {
    u8 ch = _arg<0, u8>(rdram, ctx);

    s32 ret = osContSetCh(PASS_RDRAM ch);

    _return<s32>(ctx, ret);
}

extern "C" void __osMotorAccess_recomp(uint8_t* rdram, recomp_context* ctx) {
    PTR(OSPfs) pfs = _arg<0, PTR(OSPfs)>(rdram, ctx);
    s32 flag = _arg<1, s32>(rdram, ctx);

    s32 ret = __osMotorAccess(PASS_RDRAM pfs, flag);

    _return<s32>(ctx, ret);
}

extern "C" void osMotorInit_recomp(uint8_t* rdram, recomp_context* ctx) {
    PTR(OSMesgQueue) mq = _arg<0, PTR(OSMesgQueue)>(rdram, ctx);
    PTR(OSPfs) pfs = _arg<1, PTR(OSPfs)>(rdram, ctx);
    int channel = _arg<2, s32>(rdram, ctx);

    s32 ret = osMotorInit(PASS_RDRAM mq, pfs, channel);

    _return<s32>(ctx, ret);
}

extern "C" void osMotorStart_recomp(uint8_t* rdram, recomp_context* ctx) {
    PTR(OSPfs) pfs = _arg<0, PTR(OSPfs)>(rdram, ctx);

    s32 ret = osMotorStart(PASS_RDRAM pfs);

    _return<s32>(ctx, ret);
}

extern "C" void osMotorStop_recomp(uint8_t* rdram, recomp_context* ctx) {
    PTR(OSPfs) pfs = _arg<0, PTR(OSPfs)>(rdram, ctx);

    s32 ret = osMotorStop(PASS_RDRAM pfs);

    _return<s32>(ctx, ret);
}
