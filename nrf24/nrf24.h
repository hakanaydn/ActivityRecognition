#ifndef NRF24_H
#define NRF24_H

#include <stdint.h>

#define NRF24_PIPE_COUNT        6
#define NRF24_MAX_PAYLOAD       32
#define NRF24_ADDR_LEN          5

typedef enum { NRF24_TX, NRF24_RX } NRF24_Mode_t;

typedef struct {
    uint8_t addr[NRF24_ADDR_LEN];
} NRF24_t;

uint8_t NRF24_Init(NRF24_t *dev, const uint8_t *addr);
void    NRF24_SetMode(NRF24_Mode_t mode);
uint8_t NRF24_Transmit(const uint8_t *data, uint8_t len);
uint8_t NRF24_Available(void);
uint8_t NRF24_Receive(uint8_t *data, uint8_t *len);

#endif
