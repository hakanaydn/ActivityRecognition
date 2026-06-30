#include "stm32f1xx.h"

uint32_t SystemCoreClock = 72000000;

const uint8_t AHBPrescTable[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
const uint8_t APBPrescTable[8]  = {0, 0, 0, 0, 1, 2, 3, 4};

void SystemInit(void)
{
    SCB->VTOR = FLASH_BASE;
}

void SystemCoreClockUpdate(void)
{
}
