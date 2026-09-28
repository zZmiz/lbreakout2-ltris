
#include "rp_platform.h"
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "pio_usb.h"
#include "tusb.h"
#include <cstdio>

namespace {
struct Pad { uint8_t address, instance; uint16_t vid, pid, state; } pads[4]{};
bool ready = false;
}

extern "C" void rp_usb_init()
{
    pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
    cfg.pin_dp = 28;
    cfg.pio_tx_num = cfg.pio_rx_num = 0;

    for (cfg.tx_ch = 0; cfg.tx_ch < NUM_DMA_CHANNELS; ++cfg.tx_ch)
        if (!dma_channel_is_claimed(cfg.tx_ch)) break;
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &cfg);
    ready = tuh_init(1);
    if (ready) pio_usb_host_add_port(26, PIO_USB_PINOUT_DPDM);
    std::printf("USB host: %s, DP=28/26\n", ready ? "ready" : "failed");
}

extern "C" void rp_usb_task()
{
    if (ready) tuh_task();
}

extern "C" uint16_t rp_usb_pad(int index)
{
    uint16_t state = 0;
    if (index == 0) for (const auto &p : pads) state |= p.state;
    return state;
}

extern "C" uint32_t rp_usb_identity(void)
{
    for(const auto &p:pads) if(p.address) return (uint32_t(p.vid)<<16)|p.pid;
    return 0;
}

extern "C" void tuh_hid_mount_cb(uint8_t address, uint8_t instance,
                                  uint8_t const *, uint16_t)
{
    uint16_t vid, pid;
    tuh_vid_pid_get(address, &vid, &pid);
    for (auto &p : pads) if (!p.address) {
        p = {address, instance, vid, pid, 0};
        break;
    }
    std::printf("USB HID mounted: %04x:%04x protocol=%u\n", vid, pid,
                tuh_hid_interface_protocol(address, instance));
    tuh_hid_receive_report(address, instance);
}

extern "C" void tuh_hid_umount_cb(uint8_t address, uint8_t instance)
{
    for (auto &p : pads) if (p.address == address && p.instance == instance) p = {};
}

extern "C" void tuh_hid_report_received_cb(uint8_t address, uint8_t instance,
                                            uint8_t const *r, uint16_t len)
{
    for (auto &p : pads) if (p.address == address && p.instance == instance) {
        uint16_t s = 0;

        if (p.vid == 0x0079 && p.pid == 0x0006 && len >= 7) {
            if (r[0] < 64) s |= 1u << 6;
            if (r[0] > 192) s |= 1u << 7;
            if (r[1] < 64) s |= 1u << 4;
            if (r[1] > 192) s |= 1u << 5;
            if (r[5] & 0x20) s |= 1u << 0;
            if (r[5] & 0x40) s |= 1u << 1;
            if (r[6] & 0x10) s |= 1u << 2;
            if (r[6] & 0x20) s |= 1u << 3;
            if (r[5] & 0x10) s |= 1u << 8;
            if (r[5] & 0x80) s |= 1u << 9;
        } else if (tuh_hid_interface_protocol(address, instance) == HID_ITF_PROTOCOL_KEYBOARD && len >= 8) {
            for (int i = 2; i < 8; ++i) switch (r[i]) {
                case HID_KEY_ARROW_UP: s |= 1u << 4; break;
                case HID_KEY_ARROW_DOWN: s |= 1u << 5; break;
                case HID_KEY_ARROW_LEFT: s |= 1u << 6; break;
                case HID_KEY_ARROW_RIGHT: s |= 1u << 7; break;
                case HID_KEY_Z: s |= 1u << 0; break;
                case HID_KEY_X: s |= 1u << 1; break;
                case HID_KEY_ESCAPE: s |= 1u << 2; break;
                case HID_KEY_ENTER: s |= 1u << 3; break;
            }
        }

        rp_system_menu_sample(s);
        p.state = s;
    }
    tuh_hid_receive_report(address, instance);
}
