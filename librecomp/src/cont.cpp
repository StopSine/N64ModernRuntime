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

// Joybus data CRC over a 32 byte pak block, as the controller computes it.
// The game verifies this, so a response with the wrong value is rejected.
static uint8_t pak_data_crc(const uint8_t* data) {
    uint8_t crc = 0;
    for (int i = 0; i <= 32; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            uint8_t xor_val = ((crc & 0x80) != 0) ? 0x85 : 0x00;
            crc <<= 1;
            if (i < 32 && (data[i] & (1 << bit)) != 0) {
                crc |= 1;
            }
            crc ^= xor_val;
        }
    }
    return crc;
}

// Backing store for the pak, one per channel. The rumble pak identifies itself
// by reading back 0x80 from its probe region, which is what the game looks for.
constexpr uint32_t PakSize = 32 * 1024;
constexpr uint32_t RumbleProbeAddr = 0x8000;
constexpr uint32_t RumbleControlAddr = 0xC000;
static uint8_t pak_memory[MAXCONTROLLERS][PakSize];
static bool pak_probed[MAXCONTROLLERS];

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
        {
            // Log only status and button reads; the pak traffic drowns them out.
            static int n = 0;
            if ((cmd == 0x00 || cmd == 0x01 || cmd == 0xFF) && n++ < 25) {
                fprintf(stderr, "[joy] ch%d cmd 0x%02X buttons 0x%04X x %d\n",
                        channel, cmd, (unsigned)(uint16_t)pads[0].button, pads[0].stick_x);
            }
        }
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
                    // A pak is attached; osContInit reports one too, and the
                    // two have to agree.
                    pif_ram[res_off + 2] = 0x01;
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
            case 0x02: {  // read 32 bytes from the pak
                // The two address bytes hold an 11 bit block address in their
                // top bits; the low 5 bits are a check code.
                uint32_t addr = ((uint32_t)pif_ram[cmd_off + 1] << 8 | pif_ram[cmd_off + 2]) & 0xFFE0;
                if (rx >= 33) {
                    for (int b = 0; b < 32; b++) {
                        uint8_t value;
                        if (addr >= RumbleProbeAddr && addr < RumbleControlAddr) {
                            value = pak_probed[channel] ? 0x80 : 0x00;
                        }
                        else {
                            value = pak_memory[channel][(addr + b) % PakSize];
                        }
                        pif_ram[res_off + b] = value;
                    }
                    pif_ram[res_off + 32] = pak_data_crc(&pif_ram[res_off]);
                }
                break;
            }
            case 0x03: {  // write 32 bytes to the pak
                uint32_t addr = ((uint32_t)pif_ram[cmd_off + 1] << 8 | pif_ram[cmd_off + 2]) & 0xFFE0;
                const uint8_t* payload = &pif_ram[cmd_off + 3];
                if (addr >= RumbleProbeAddr && addr < RumbleControlAddr) {
                    // Probing for a rumble pak; remember so the read back
                    // identifies one.
                    pak_probed[channel] = true;
                }
                else if (addr == RumbleControlAddr) {
                    ultramodern::set_rumble(channel, payload[0] != 0);
                }
                else {
                    for (int b = 0; b < 32; b++) {
                        pak_memory[channel][(addr + b) % PakSize] = payload[b];
                    }
                }
                if (rx >= 1) {
                    pif_ram[res_off] = pak_data_crc(payload);
                }
                break;
            }
            default:    // EEPROM (0x04/0x05) and anything else
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
