#include "nrf24.h"
#include "stm32f1xx.h"

#define nrf_CSN_LOW()   GPIOB->BRR = (1 << 0)
#define nrf_CSN_HIGH()  GPIOB->BSRR = (1 << 0)
#define nrf_CE_LOW()    GPIOB->BRR = (1 << 1)
#define nrf_CE_HIGH()   GPIOB->BSRR = (1 << 1)

#define NRF24_CMD_R_REGISTER      0x00
#define NRF24_CMD_W_REGISTER      0x20
#define NRF24_CMD_R_RX_PAYLOAD    0x61
#define NRF24_CMD_W_TX_PAYLOAD    0xA0
#define NRF24_CMD_FLUSH_TX        0xE1
#define NRF24_CMD_FLUSH_RX        0xE2
#define NRF24_CMD_R_RX_PL_WID     0x60
#define NRF24_CMD_NOP             0xFF

#define REG_CONFIG          0x00
#define REG_EN_AA           0x01
#define REG_EN_RXADDR       0x02
#define REG_SETUP_AW        0x03
#define REG_SETUP_RETR      0x04
#define REG_RF_CH           0x05
#define REG_RF_SETUP        0x06
#define REG_STATUS          0x07
#define REG_RX_ADDR_P0      0x0A
#define REG_TX_ADDR         0x10
#define REG_RX_PW_P0        0x11
#define REG_FIFO_STATUS     0x17
#define REG_DYNPD           0x1C
#define REG_FEATURE         0x1D

#define CONFIG_EN_CRC       (1 << 3)
#define CONFIG_PWR_UP       (1 << 1)
#define CONFIG_PRIM_RX      (1 << 0)
#define STATUS_RX_DR        (1 << 6)
#define STATUS_TX_DS        (1 << 5)
#define STATUS_MAX_RT       (1 << 4)
#define STATUS_RX_P_NO      0x0E
#define RF_DR_HIGH          (1 << 3)
#define RF_PWR              (3 << 1)

static NRF24_t nrf;

static void addr_cpy(uint8_t *d, const uint8_t *s)
{
    for (int i = 0; i < NRF24_ADDR_LEN; i++) d[i] = s[i];
}

static void spi_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    GPIOA->CRL &= ~(0xF << (5 * 4));
    GPIOA->CRL |= (0xB << (5 * 4));
    GPIOA->CRL &= ~(0xF << (6 * 4));
    GPIOA->CRL |= (0x4 << (6 * 4));
    GPIOA->CRL &= ~(0xF << (7 * 4));
    GPIOA->CRL |= (0xB << (7 * 4));

    SPI1->CR1 = SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_MSTR
              | (2 << 3) | SPI_CR1_SPE;
    SPI1->CR2 = 0;
}

static uint8_t spi_xfer(uint8_t tx)
{
    while (!(SPI1->SR & SPI_SR_TXE));
    *(uint8_t *)&SPI1->DR = tx;
    while (!(SPI1->SR & SPI_SR_RXNE));
    return *(uint8_t *)&SPI1->DR;
}

static void write_reg(uint8_t reg, uint8_t val)
{
    nrf_CSN_LOW();
    spi_xfer(NRF24_CMD_W_REGISTER | reg);
    spi_xfer(val);
    nrf_CSN_HIGH();
}

static uint8_t read_reg(uint8_t reg)
{
    nrf_CSN_LOW();
    spi_xfer(NRF24_CMD_R_REGISTER | reg);
    uint8_t v = spi_xfer(NRF24_CMD_NOP);
    nrf_CSN_HIGH();
    return v;
}

static void write_buf(uint8_t reg, const uint8_t *buf, uint8_t len)
{
    nrf_CSN_LOW();
    spi_xfer(NRF24_CMD_W_REGISTER | reg);
    for (uint8_t i = 0; i < len; i++) spi_xfer(buf[i]);
    nrf_CSN_HIGH();
}

static void read_buf(uint8_t reg, uint8_t *buf, uint8_t len)
{
    nrf_CSN_LOW();
    spi_xfer(NRF24_CMD_R_REGISTER | reg);
    for (uint8_t i = 0; i < len; i++) buf[i] = spi_xfer(NRF24_CMD_NOP);
    nrf_CSN_HIGH();
}

uint8_t NRF24_Init(NRF24_t *dev, const uint8_t *addr)
{
    addr_cpy(dev->addr, addr);
    addr_cpy(nrf.addr, addr);

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN;

    GPIOB->CRL &= ~(0xFF << (0 * 4));
    GPIOB->CRL |=  (0x1 << (0 * 4));
    GPIOB->CRL |=  (0x1 << (1 * 4));
    nrf_CSN_HIGH();
    nrf_CE_LOW();

    spi_init();

    for (volatile int i = 0; i < 1000; i++);

    uint8_t who = read_reg(REG_CONFIG);
    if (who == 0xFF || who == 0x00) return 0;

    write_reg(REG_CONFIG, 0x00);
    for (volatile int i = 0; i < 100; i++);

    write_reg(REG_EN_AA,      0x3F);
    write_reg(REG_EN_RXADDR,  0x03);
    write_reg(REG_SETUP_AW,   0x03);
    write_reg(REG_SETUP_RETR, 0x1A);
    write_reg(REG_RF_CH,      0x4E);
    write_reg(REG_RF_SETUP,   RF_DR_HIGH | RF_PWR);
    write_reg(REG_FEATURE,    0x07);
    write_reg(REG_DYNPD,      0x3F);

    write_buf(REG_RX_ADDR_P0, addr, NRF24_ADDR_LEN);
    write_buf(REG_TX_ADDR,    addr, NRF24_ADDR_LEN);
    write_reg(REG_RX_PW_P0,   NRF24_MAX_PAYLOAD);

    write_reg(REG_STATUS, 0x70);

    write_reg(REG_CONFIG, CONFIG_EN_CRC | CONFIG_PWR_UP);
    for (volatile int i = 0; i < 100; i++);

    return 1;
}

void NRF24_SetMode(NRF24_Mode_t mode)
{
    nrf_CE_LOW();
    uint8_t c = read_reg(REG_CONFIG) & ~CONFIG_PRIM_RX;
    if (mode == NRF24_RX) c |= CONFIG_PRIM_RX;
    write_reg(REG_CONFIG, c);
    if (mode == NRF24_RX) nrf_CE_HIGH();
}

uint8_t NRF24_Transmit(const uint8_t *data, uint8_t len)
{
    if (len > NRF24_MAX_PAYLOAD) return 0;

    nrf_CE_LOW();
    write_reg(REG_STATUS, STATUS_TX_DS | STATUS_MAX_RT);

    nrf_CSN_LOW();
    spi_xfer(NRF24_CMD_FLUSH_TX);
    nrf_CSN_HIGH();

    nrf_CSN_LOW();
    spi_xfer(NRF24_CMD_W_TX_PAYLOAD);
    for (uint8_t i = 0; i < len; i++) spi_xfer(data[i]);
    nrf_CSN_HIGH();

    nrf_CE_HIGH();
    for (volatile int i = 0; i < 20; i++);
    nrf_CE_LOW();

    uint8_t st = read_reg(REG_STATUS);
    write_reg(REG_STATUS, STATUS_TX_DS | STATUS_MAX_RT);

    if (st & STATUS_MAX_RT) {
        nrf_CSN_LOW(); spi_xfer(NRF24_CMD_FLUSH_TX); nrf_CSN_HIGH();
        return 0;
    }
    return (st & STATUS_TX_DS) ? 1 : 0;
}

uint8_t NRF24_Available(void)
{
    return (read_reg(REG_STATUS) & STATUS_RX_DR) ? 1 : 0;
}

uint8_t NRF24_Receive(uint8_t *data, uint8_t *len)
{
    if (!NRF24_Available()) return 0;

    uint8_t st = read_reg(REG_STATUS);
    if ((st & STATUS_RX_P_NO) >= (NRF24_PIPE_COUNT << 1)) {
        write_reg(REG_STATUS, STATUS_RX_DR);
        return 0;
    }

    nrf_CSN_LOW();
    spi_xfer(NRF24_CMD_R_RX_PL_WID);
    uint8_t pl = spi_xfer(NRF24_CMD_NOP);
    nrf_CSN_HIGH();

    if (pl > NRF24_MAX_PAYLOAD || pl == 0) {
        nrf_CSN_LOW(); spi_xfer(NRF24_CMD_FLUSH_RX); nrf_CSN_HIGH();
        write_reg(REG_STATUS, STATUS_RX_DR);
        return 0;
    }

    *len = pl;
    nrf_CSN_LOW();
    spi_xfer(NRF24_CMD_R_RX_PAYLOAD);
    for (uint8_t i = 0; i < pl; i++) data[i] = spi_xfer(NRF24_CMD_NOP);
    nrf_CSN_HIGH();

    write_reg(REG_STATUS, STATUS_RX_DR);
    return 1;
}
