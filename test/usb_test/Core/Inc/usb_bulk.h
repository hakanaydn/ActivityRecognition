#ifndef USB_BULK_H
#define USB_BULK_H

#include <stdint.h>

#define USB_BULK_EP_IN   0x81
#define USB_BULK_EP_OUT  0x02
#define USB_BULK_SIZE    64

void  USB_Bulk_Init_Poll(int pll48);
void  USB_Bulk_Poll(void);
uint8_t USB_Bulk_IsConfigured(void);

#endif
