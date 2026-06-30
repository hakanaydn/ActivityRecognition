#ifndef VCP_H
#define VCP_H

#include <stdint.h>

#define VCP_BULK_SIZE 64

void VCP_Init(int pll48);
void VCP_Poll(void);
uint8_t VCP_IsConfigured(void);
uint8_t VCP_RxAvail(void);
uint8_t VCP_Read(uint8_t *data, uint8_t *len);

#endif
