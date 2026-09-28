
#pragma once
struct pio_usb_configuration_t {int pin_dp,pio_tx_num,pio_rx_num,tx_ch;};
#define PIO_USB_DEFAULT_CONFIG {0,0,0,0}
#define PIO_USB_PINOUT_DPDM 0
inline void pio_usb_host_add_port(int,int) {}
