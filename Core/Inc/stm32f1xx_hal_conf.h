#ifndef __STM32F1XX_HAL_CONF_H
#define __STM32F1XX_HAL_CONF_H

#include "stm32f1xx.h"
#include "stm32f1xx_hal_def.h"

#define HAL_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED

#if defined(HAL_DMA_MODULE_ENABLED)
#include "stm32f1xx_hal_dma.h"
#endif
#if defined(HAL_I2C_MODULE_ENABLED)
#include "stm32f1xx_hal_i2c.h"
#endif
#if defined(HAL_GPIO_MODULE_ENABLED)
#include "stm32f1xx_hal_gpio.h"
#endif
#if defined(HAL_RCC_MODULE_ENABLED)
#include "stm32f1xx_hal_rcc.h"
#endif
#if defined(HAL_CORTEX_MODULE_ENABLED)
#include "stm32f1xx_hal_cortex.h"
#endif
#if defined(HAL_UART_MODULE_ENABLED)
#include "stm32f1xx_hal_uart.h"
#endif
#if defined(HAL_FLASH_MODULE_ENABLED)
#include "stm32f1xx_hal_flash.h"
#endif
#if defined(HAL_PWR_MODULE_ENABLED)
#include "stm32f1xx_hal_pwr.h"
#endif
#if defined(HAL_EXTI_MODULE_ENABLED)
#include "stm32f1xx_hal_exti.h"
#endif

#define HAL_MAX_DELAY      0xFFFFFFFFU
#define HAL_TICK_FREQ      1000U
#define TICK_INT_PRIORITY  0x0FU
#define USE_HAL_I2C_REGISTER_CALLBACKS     0U
#define USE_HAL_UART_REGISTER_CALLBACKS    0U

#define USE_FULL_ASSERT    0U

#if USE_FULL_ASSERT
#define assert_param(expr)  ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr)  ((void)0U)
#endif

#endif
