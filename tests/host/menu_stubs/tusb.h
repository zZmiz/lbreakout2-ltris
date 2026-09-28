
#pragma once
#include <cstdint>
#define TUH_CFGID_RPI_PIO_USB_CONFIGURATION 0
#define HID_ITF_PROTOCOL_KEYBOARD 1
#define HID_KEY_ARROW_UP 82
#define HID_KEY_ARROW_DOWN 81
#define HID_KEY_ARROW_LEFT 80
#define HID_KEY_ARROW_RIGHT 79
#define HID_KEY_Z 29
#define HID_KEY_X 27
#define HID_KEY_ESCAPE 41
#define HID_KEY_ENTER 40
inline void tuh_configure(int,int,const void *) {}
inline bool tuh_init(int) {return true;}
inline void tuh_task() {}
inline void tuh_vid_pid_get(uint8_t,uint16_t *vid,uint16_t *pid) {*vid=0x79; *pid=6;}
inline int tuh_hid_interface_protocol(uint8_t,uint8_t) {return 0;}
inline void tuh_hid_receive_report(uint8_t,uint8_t) {}
