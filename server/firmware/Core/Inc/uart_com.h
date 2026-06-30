#ifndef UART_COM_H
#define UART_COM_H

#include <stdint.h>
#include "packet.h"
#include "stm32f1xx_hal.h"

extern UART_HandleTypeDef huart;

void    UART_Com_Init(uint32_t baud);
void    UART_Com_Send(const uint8_t *data, uint16_t len);
uint8_t UART_Com_ReadPacket(Packet *pkt);

#endif
