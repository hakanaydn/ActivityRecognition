#include "nrf24.h"
#include "stm32f1xx.h"

#ifdef NRF24_USE_HAL
#define CSN_LOW()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET)
#define CSN_HIGH()  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET)
#define CE_LOW()    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET)
#define CE_HIGH()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET)
#else
#define CSN_LOW()   GPIOB->BRR = (1 << 0)
#define CSN_HIGH()  GPIOB->BSRR = (1 << 0)
#define CE_LOW()    GPIOB->BRR = (1 << 1)
#define CE_HIGH()   GPIOB->BSRR = (1 << 1)
#endif

#ifdef NRF24_USE_HAL
#include "stm32f1xx_hal.h"
extern SPI_HandleTypeDef hspi1;

static void spi_init(void) { }

static uint8_t spi_xfer(uint8_t tx)
{
    uint8_t rx;
    HAL_SPI_TransmitReceive(&hspi1, &tx, &rx, 1, HAL_MAX_DELAY);
    return rx;
}
#else

static void spi_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    GPIOA->CRL = (GPIOA->CRL & 0x000FFFFF) | 0xB4B00000;

    RCC->APB2RSTR |= RCC_APB2RSTR_SPI1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_SPI1RST;

    SPI1->CR1 = (1 << 8) | (1 << 9) | (1 << 2) | (3 << 3);
    SPI1->CR2 = 0;
    (void)SPI1->SR;
    (void)SPI1->DR;
    SPI1->CR1 |= (1 << 6);
}

static uint8_t spi_xfer(uint8_t tx)
{
    while (!(SPI1->SR & (1 << 1)));
    *(uint8_t *)&SPI1->DR = tx;
    while (!(SPI1->SR & (1 << 0)));
    return *(uint8_t *)&SPI1->DR;
}

#endif

#define CMD_R_REG          0x00
#define CMD_W_REG          0x20
#define CMD_R_RX_PAYLOAD   0x61
#define CMD_W_TX_PAYLOAD   0xA0
#define CMD_FLUSH_TX       0xE1
#define CMD_FLUSH_RX       0xE2
#define CMD_R_RX_PL_WID    0x60
#define CMD_W_ACK_PAYLOAD  0xA8
#define CMD_NOP            0xFF

#define CNF_EN_CRC      (1 << 3)
#define CNF_PWR_UP      (1 << 1)
#define CNF_PRIM_RX     (1 << 0)
#define ST_RX_DR        (1 << 6)
#define ST_TX_DS        (1 << 5)
#define ST_MAX_RT       (1 << 4)
#define ST_RX_P_NO      0x0E
#define RF_DR_HIGH      (1 << 3)
#define RF_PWR          (3 << 1)

static NRF24_t nrf;
static uint8_t ack_pending;
static uint8_t ack_len;

static void addr_cpy(uint8_t *d, const uint8_t *s)
{
    for (int i = 0; i < NRF24_ADDR_LEN; i++) d[i] = s[i];
}

static void wreg(uint8_t reg, uint8_t val)
{
    CSN_LOW(); spi_xfer(CMD_W_REG | reg); spi_xfer(val); CSN_HIGH();
}

static uint8_t rreg(uint8_t reg)
{
    CSN_LOW(); spi_xfer(CMD_R_REG | reg); uint8_t v = spi_xfer(CMD_NOP); CSN_HIGH();
    return v;
}

static void wbuf(uint8_t reg, const uint8_t *b, uint8_t len)
{
    CSN_LOW(); spi_xfer(CMD_W_REG | reg);
    for (uint8_t i = 0; i < len; i++) spi_xfer(b[i]);
    CSN_HIGH();
}

uint8_t NRF24_Init(NRF24_t *dev, const uint8_t *addr)
{
    addr_cpy(dev->addr, addr);
    addr_cpy(nrf.addr, addr);
    ack_pending = 0;
    ack_len = 0;

#ifndef NRF24_USE_HAL
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN;
    GPIOB->CRL = (GPIOB->CRL & 0xFFFFFF00) | 0x11;
#endif
    CSN_HIGH(); CE_LOW();

    spi_init();

    for (volatile int i = 0; i < 10000; i++);

    for (volatile int i = 0; i < 10; i++) { CSN_LOW(); CSN_HIGH(); }

    CSN_LOW(); spi_xfer(CMD_FLUSH_TX); CSN_HIGH();
    CSN_LOW(); spi_xfer(CMD_FLUSH_RX); CSN_HIGH();

    uint8_t cfg = 0xFF;
    for (int retry = 0; retry < 3; retry++) {
        cfg = rreg(REG_CONFIG);
        if (cfg != 0xFF && cfg != 0x00) break;
        for (volatile int i = 0; i < 5000; i++);
    }
    if (cfg == 0xFF || cfg == 0x00) return 0;

    wreg(REG_CONFIG, 0x00);
    for (volatile int i = 0; i < 100; i++);

    wreg(REG_EN_AA,      0x3F);
    wreg(REG_EN_RXADDR,  0x03);
    wreg(REG_SETUP_AW,   0x03);
    wreg(REG_SETUP_RETR, 0x1A);
    wreg(REG_RF_CH,      0x4E);
    wreg(REG_RF_SETUP,   RF_DR_HIGH | RF_PWR);
    wreg(REG_FEATURE,    0x07);
    wreg(REG_DYNPD,      0x3F);

    wbuf(REG_RX_ADDR_P0, addr, NRF24_ADDR_LEN);
    wbuf(REG_TX_ADDR,    addr, NRF24_ADDR_LEN);
    wreg(REG_RX_PW_P0,   NRF24_MAX_PAYLOAD);

    wreg(REG_STATUS, 0x70);
    wreg(REG_CONFIG, CNF_EN_CRC | CNF_PWR_UP);
    for (volatile int i = 0; i < 15000; i++);

    return 1;
}

void NRF24_SetMode(NRF24_Mode_t mode)
{
    CE_LOW();
    uint8_t c = rreg(REG_CONFIG) & ~CNF_PRIM_RX;
    if (mode == NRF24_RX) c |= CNF_PRIM_RX;
    wreg(REG_CONFIG, c);
    if (mode == NRF24_RX) CE_HIGH();
}

uint8_t NRF24_Transmit(const uint8_t *data, uint8_t len)
{
    if (len > NRF24_MAX_PAYLOAD) return 0;

    CE_LOW();
    wreg(REG_STATUS, ST_TX_DS | ST_MAX_RT);

    CSN_LOW(); spi_xfer(CMD_FLUSH_TX); CSN_HIGH();

    CSN_LOW();
    spi_xfer(CMD_W_TX_PAYLOAD);
    for (uint8_t i = 0; i < len; i++) spi_xfer(data[i]);
    CSN_HIGH();

    CE_HIGH();
    for (volatile int i = 0; i < 20; i++);
    CE_LOW();

    uint8_t st = rreg(REG_STATUS);
    wreg(REG_STATUS, ST_TX_DS | ST_MAX_RT);

    if (st & ST_MAX_RT) {
        CSN_LOW(); spi_xfer(CMD_FLUSH_TX); CSN_HIGH();
        return 0;
    }
    return (st & ST_TX_DS) ? 1 : 0;
}

uint8_t NRF24_TransmitAck(const uint8_t *data, uint8_t len,
                          uint8_t *ack_data, uint8_t *ack_len, uint32_t timeout_ms)
{
    if (len > NRF24_MAX_PAYLOAD) return 0;

    CE_LOW();
    wreg(REG_STATUS, ST_TX_DS | ST_MAX_RT | ST_RX_DR);

    CSN_LOW(); spi_xfer(CMD_FLUSH_TX); CSN_HIGH();

    CSN_LOW();
    spi_xfer(CMD_W_TX_PAYLOAD);
    for (uint8_t i = 0; i < len; i++) spi_xfer(data[i]);
    CSN_HIGH();
    CE_HIGH();

    for (uint32_t i = 0; i < timeout_ms * 8; i++) {
        uint8_t st = rreg(REG_STATUS);
        if (st & (ST_TX_DS | ST_MAX_RT)) {
            wreg(REG_STATUS, ST_TX_DS | ST_MAX_RT);
            if (st & ST_MAX_RT) {
                CSN_LOW(); spi_xfer(CMD_FLUSH_TX); CSN_HIGH();
                CE_LOW();
                return 0;
            }
            if (st & ST_RX_DR) {
                uint8_t pipe = (st & ST_RX_P_NO) >> 1;
                if (pipe < 6) {
                    uint8_t pl;
                    CSN_LOW(); spi_xfer(CMD_R_RX_PL_WID); pl = spi_xfer(CMD_NOP); CSN_HIGH();
                    if (pl > 0 && pl <= *ack_len) {
                        CSN_LOW(); spi_xfer(CMD_R_RX_PAYLOAD);
                        for (uint8_t i = 0; i < pl; i++) ack_data[i] = spi_xfer(CMD_NOP);
                        CSN_HIGH();
                        *ack_len = pl;
                    }
                }
                wreg(REG_STATUS, ST_RX_DR);
            }
            CE_LOW();
            return 1;
        }
        for (volatile int d = 0; d < 2000; d++);
    }
    CE_LOW();
    return 0;
}

uint8_t NRF24_ReadReg(uint8_t reg)
{
    return rreg(reg);
}

void NRF24_WriteReg(uint8_t reg, uint8_t val)
{
    wreg(reg, val);
}

void NRF24_SendAckPayload(const uint8_t *data, uint8_t len)
{
    if (len > NRF24_MAX_PAYLOAD) return;
    CSN_LOW();
    spi_xfer(CMD_W_ACK_PAYLOAD | 0);
    for (uint8_t i = 0; i < len; i++) spi_xfer(data[i]);
    CSN_HIGH();
}

uint8_t NRF24_Available(void)
{
    return (rreg(REG_STATUS) & ST_RX_DR) ? 1 : 0;
}

uint8_t NRF24_Receive(uint8_t *data, uint8_t *len)
{
    if (!NRF24_Available()) return 0;

    uint8_t st = rreg(REG_STATUS);
    if ((st & ST_RX_P_NO) >= (6 << 1)) {
        wreg(REG_STATUS, ST_RX_DR);
        return 0;
    }

    uint8_t pl;
    CSN_LOW(); spi_xfer(CMD_R_RX_PL_WID); pl = spi_xfer(CMD_NOP); CSN_HIGH();

    if (pl > NRF24_MAX_PAYLOAD || pl == 0) {
        CSN_LOW(); spi_xfer(CMD_FLUSH_RX); CSN_HIGH();
        wreg(REG_STATUS, ST_RX_DR);
        return 0;
    }

    *len = pl;
    CSN_LOW(); spi_xfer(CMD_R_RX_PAYLOAD);
    for (uint8_t i = 0; i < pl; i++) data[i] = spi_xfer(CMD_NOP);
    CSN_HIGH();

    wreg(REG_STATUS, ST_RX_DR);
    return 1;
}
