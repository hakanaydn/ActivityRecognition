#include "uart_com.h"
#include "stm32f1xx_hal.h"

#define RING_SIZE 256

UART_HandleTypeDef huart;
DMA_HandleTypeDef hdma_tx;
static volatile uint8_t rx_ring[RING_SIZE];
static volatile uint16_t rx_w;
static volatile uint16_t rx_r;
static uint8_t rx_byte;
static volatile uint8_t tx_busy;

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

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
        tx_busy = 0;
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

    __HAL_RCC_DMA1_CLK_ENABLE();

    hdma_tx.Instance = DMA1_Channel4;
    hdma_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_tx.Init.Mode = DMA_NORMAL;
    hdma_tx.Init.Priority = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&hdma_tx);

    __HAL_LINKDMA(&huart, hdmatx, hdma_tx);

    HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    tx_busy = 0;
    rx_w = 0;
    rx_r = 0;
    HAL_UART_Receive_IT(&huart, &rx_byte, 1);
}

void UART_Com_Send(const uint8_t *data, uint16_t len)
{
    while (tx_busy)
        ;
    tx_busy = 1;
    HAL_UART_Transmit_DMA(&huart, (uint8_t *)data, len);
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
