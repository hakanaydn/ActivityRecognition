#include "stm32f1xx_hal.h"
#include "stm32f1xx_it.h"
#include "FreeRTOS.h"
#include "task.h"

volatile int scheduler_started = 0;

void NMI_Handler(void)         { for (;;); }
void HardFault_Handler(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    for (;;) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        for (volatile uint32_t i = 0; i < 500000; i++);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        for (volatile uint32_t i = 0; i < 500000; i++);
    }
}
void MemManage_Handler(void)   { for (;;); }
void BusFault_Handler(void)    { for (;;); }
void UsageFault_Handler(void)  { for (;;); }
void DebugMon_Handler(void)    {}

extern UART_HandleTypeDef huart;

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart);
}

extern void xPortSysTickHandler(void);

void SysTick_Handler(void)
{
    HAL_IncTick();
    if (scheduler_started) {
        xPortSysTickHandler();
    }
}
