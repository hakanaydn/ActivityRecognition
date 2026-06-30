#ifndef NRF24_H
#define NRF24_H

#include <stdint.h>

#define NRF24_MAX_PAYLOAD       32
#define NRF24_ADDR_LEN          5

typedef enum { NRF24_TX, NRF24_RX } NRF24_Mode_t;

typedef struct {
    uint8_t addr[NRF24_ADDR_LEN];
} NRF24_t;

uint8_t NRF24_Init(NRF24_t *dev, const uint8_t *addr);
void    NRF24_SetMode(NRF24_Mode_t mode);
uint8_t NRF24_Transmit(const uint8_t *data, uint8_t len);
uint8_t NRF24_TransmitAck(const uint8_t *data, uint8_t len,
                          uint8_t *ack_data, uint8_t *ack_len, uint32_t timeout_ms);
void    NRF24_SendAckPayload(const uint8_t *data, uint8_t len);
#define REG_CONFIG      0x00
#define REG_EN_AA       0x01
#define REG_EN_RXADDR   0x02
#define REG_SETUP_AW    0x03
#define REG_SETUP_RETR  0x04
#define REG_RF_CH       0x05
#define REG_RF_SETUP    0x06
#define REG_STATUS      0x07
#define REG_RX_ADDR_P0  0x0A
#define REG_TX_ADDR     0x10
#define REG_RX_PW_P0    0x11
#define REG_FIFO_STATUS 0x17
#define REG_DYNPD       0x1C
#define REG_FEATURE     0x1D

uint8_t NRF24_Available(void);
uint8_t NRF24_Receive(uint8_t *data, uint8_t *len);
uint8_t NRF24_ReadReg(uint8_t reg);
void    NRF24_WriteReg(uint8_t reg, uint8_t val);

#endif
