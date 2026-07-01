#include "uart_com.h"
#include "stm32f1xx_hal.h"

#define RING_SIZE 256

UART_HandleTypeDef huart;
static volatile uint8_t rx_ring[RING_SIZE];
static volatile uint16_t rx_w;
static volatile uint16_t rx_r;
static uint8_t rx_byte;

static uint16_t inc(uint16_t idx)
{
    return (idx + 1) & (RING_SIZE - 1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        rx_ring[rx_w] = rx_byte;
        rx_w = inc(rx_w);
        HAL_UART_Receive_IT(huart, &rx_byte, 1);
    }
}

void UART_Com_Init(uint32_t baud)
{
    huart.Instance = USART1;
    huart.Init.BaudRate = baud;
    huart.Init.WordLength = UART_WORDLENGTH_8B;
    huart.Init.StopBits = UART_STOPBITS_1;
    huart.Init.Parity = UART_PARITY_NONE;
    huart.Init.Mode = UART_MODE_TX_RX;
    huart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart);

    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    rx_w = 0;
    rx_r = 0;
    HAL_UART_Receive_IT(&huart, &rx_byte, 1);
}

void UART_Com_Send(const uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&huart, (uint8_t *)data, len, 100);
}

uint8_t UART_Com_ReadPacket(Packet *pkt)
{
    uint16_t avail = (rx_w - rx_r) & (RING_SIZE - 1);
    if (avail < sizeof(Packet))
        return 0;

    uint16_t s = rx_r;
    uint16_t left = avail;

    while (left >= sizeof(Packet)) {
        uint8_t buf[sizeof(Packet)];
        for (unsigned i = 0; i < sizeof(Packet); i++)
            buf[i] = rx_ring[(s + i) & (RING_SIZE - 1)];

        Packet *candidate = (Packet *)buf;
        if ((candidate->magic == PACKET_MAGIC_CMD ||
             candidate->magic == PACKET_MAGIC_ACK) &&
            packet_verify(candidate)) {
            for (unsigned i = 0; i < sizeof(Packet); i++)
                ((uint8_t *)pkt)[i] = buf[i];
            rx_r = (s + sizeof(Packet)) & (RING_SIZE - 1);
            return 1;
        }
        s = inc(s);
        left--;
    }
    return 0;
}
