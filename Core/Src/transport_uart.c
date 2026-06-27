#include "transport.h"
#include "main.h"

#define RING_SIZE      256
#define TX_BUF_SIZE    64

static uint8_t ring[RING_SIZE];
static volatile uint16_t wp, rp;
static uint8_t tx_buf[TX_BUF_SIZE];
static volatile uint8_t active;

static uint16_t ring_avail(void)
{
    return (wp - rp) & (RING_SIZE - 1);
}

static uint16_t ring_read(uint8_t *dst, uint16_t max)
{
    uint16_t n = 0;
    while (n < max && rp != wp)
    {
        dst[n++] = ring[rp];
        rp = (rp + 1) & (RING_SIZE - 1);
    }
    return n;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    (void)huart;
    uint16_t n = ring_read(tx_buf, TX_BUF_SIZE);
    if (n > 0)
    {
        active = 1;
        HAL_UART_Transmit_DMA(&huart1, tx_buf, n);
    }
    else
    {
        active = 0;
    }
}

static void uart_init(void)
{
    wp = 0; rp = 0; active = 0;
}

static void uart_write(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        ring[wp] = data[i];
        wp = (wp + 1) & (RING_SIZE - 1);
    }
    if (!active)
    {
        uint16_t n = ring_read(tx_buf, TX_BUF_SIZE);
        if (n > 0)
        {
            active = 1;
            HAL_UART_Transmit_DMA(&huart1, tx_buf, n);
        }
    }
}

static Output_t transport = {
    .init  = uart_init,
    .write = uart_write,
};

Output_t *g_output = &transport;

void Output_Init(void)
{
    g_output->init();
}

void Output_Write(const uint8_t *data, uint16_t len)
{
    g_output->write(data, len);
}
