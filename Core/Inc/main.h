#ifndef __MAIN_H
#define __MAIN_H

#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"

extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_i2c1_tx;
extern DMA_HandleTypeDef hdma_i2c1_rx;
extern DMA_HandleTypeDef hdma_usart1_tx;

void SystemClock_Config(void);
void MX_GPIO_Init(void);
void MX_DMA_Init(void);
void MX_I2C1_Init(void);
void MX_USART1_UART_Init(void);

#endif
