#include "main.h"
#include "stm32f1xx_it.h"

void NMI_Handler(void) {}
void HardFault_Handler(void) { for(;;); }
void MemManage_Handler(void) { for(;;); }
void BusFault_Handler(void) { for(;;); }
void UsageFault_Handler(void) { for(;;); }
void DebugMon_Handler(void) {}

void DMA1_Channel4_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart1_tx);
}

void DMA1_Channel6_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_i2c1_tx);
}

void DMA1_Channel7_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_i2c1_rx);
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

void I2C1_EV_IRQHandler(void)
{
    HAL_I2C_EV_IRQHandler(&hi2c1);
}

void I2C1_ER_IRQHandler(void)
{
    HAL_I2C_ER_IRQHandler(&hi2c1);
}
