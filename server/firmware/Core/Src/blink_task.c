#include "stm32f1xx_hal.h"
#include "blink_task.h"
#include "FreeRTOS.h"
#include "task.h"

void BlinkTask(void *params)
{
    (void)params;
    for (;;) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        vTaskDelay(250);
    }
}