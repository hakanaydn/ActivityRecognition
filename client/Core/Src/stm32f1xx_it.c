#include "main.h"
#include "stm32f1xx_it.h"

static void fault_putc(char c)
{
    huart1.gState = HAL_UART_STATE_READY;
    HAL_UART_Transmit(&huart1, (uint8_t*)&c, 1, HAL_MAX_DELAY);
}

static void fault_puts(const char *s)
{
    while (*s) fault_putc(*s++);
}

static void fault_puthex8(uint32_t val)
{
    static const char hex[] = "0123456789ABCDEF";
    fault_putc(hex[(val >> 4) & 0xF]);
    fault_putc(hex[val & 0xF]);
}

static void fault_puthex32(uint32_t val)
{
    fault_puthex8(val >> 24);
    fault_puthex8(val >> 16);
    fault_puthex8(val >> 8);
    fault_puthex8(val);
}

static void fault_putdec(int val)
{
    if (val >= 10) fault_putc('0' + val / 10);
    fault_putc('0' + val % 10);
}

void fault_entry(uint32_t *fault_stack)
{
    uint32_t ipsr;
    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));

    const char *type;
    switch (ipsr)
    {
        case 3:  type = "HARD FAULT";      break;
        case 4:  type = "MEMMANAGE FAULT"; break;
        case 5:  type = "BUS FAULT";       break;
        case 6:  type = "USAGE FAULT";     break;
        default: type = "UNKNOWN FAULT";   break;
    }

    uint32_t r0  = fault_stack[0];
    uint32_t r1  = fault_stack[1];
    uint32_t r2  = fault_stack[2];
    uint32_t r3  = fault_stack[3];
    uint32_t r12 = fault_stack[4];
    uint32_t lr  = fault_stack[5];
    uint32_t pc  = fault_stack[6];
    uint32_t psr = fault_stack[7];

    uint32_t cfsr  = SCB->CFSR;
    uint32_t hfsr  = SCB->HFSR;
    uint32_t mmfar = SCB->MMFAR;
    uint32_t bfar  = SCB->BFAR;

    fault_puts("\r\n\r\n========================================\r\n  ");
    fault_puts(type);
    fault_puts("\r\n========================================\r\n");

    fault_puts("CFSR: 0x"); fault_puthex32(cfsr);
    fault_puts("  HFSR: 0x"); fault_puthex32(hfsr);
    fault_puts("\r\n");

    if (cfsr & (1UL << 0))  fault_puts("  IACCVIOL\r\n");
    if (cfsr & (1UL << 1))  fault_puts("  DACCVIOL\r\n");
    if (cfsr & (1UL << 3))  fault_puts("  MUNSTKERR\r\n");
    if (cfsr & (1UL << 4))  fault_puts("  MSTKERR\r\n");
    if (cfsr & (1UL << 7))  fault_puts("  MMARVALID\r\n");
    if (cfsr & (1UL << 8))  fault_puts("  IBUSERR\r\n");
    if (cfsr & (1UL << 9))  fault_puts("  PRECISERR\r\n");
    if (cfsr & (1UL << 10)) fault_puts("  IMPRECISERR\r\n");
    if (cfsr & (1UL << 11)) fault_puts("  UNSTKERR\r\n");
    if (cfsr & (1UL << 12)) fault_puts("  STKERR\r\n");
    if (cfsr & (1UL << 15)) fault_puts("  BFARVALID\r\n");
    if (cfsr & (1UL << 16)) fault_puts("  UNDEFINSTR\r\n");
    if (cfsr & (1UL << 17)) fault_puts("  INVSTATE\r\n");
    if (cfsr & (1UL << 18)) fault_puts("  INVPC\r\n");
    if (cfsr & (1UL << 19)) fault_puts("  NOCP\r\n");
    if (cfsr & (1UL << 24)) fault_puts("  DIVBYZERO\r\n");
    if (cfsr & (1UL << 25)) fault_puts("  UNALIGNED\r\n");

    if (hfsr & (1UL << 1))  fault_puts("  VECTBL\r\n");
    if (hfsr & (1UL << 30)) fault_puts("  FORCED\r\n");
    if (hfsr & (1UL << 31)) fault_puts("  DEBUGEVT\r\n");

    if (cfsr & (1UL << 7)) {
        fault_puts("MMFAR: 0x"); fault_puthex32(mmfar); fault_puts("\r\n");
    }
    if (cfsr & (1UL << 15)) {
        fault_puts("BFAR: 0x"); fault_puthex32(bfar); fault_puts("\r\n");
    }

    fault_puts("\r\nRegisters:\r\n");
    fault_puts("  PC:  0x"); fault_puthex32(pc); fault_puts("\r\n");
    fault_puts("  LR:  0x"); fault_puthex32(lr); fault_puts("\r\n");
    fault_puts("  xPSR:0x"); fault_puthex32(psr); fault_puts("\r\n");
    fault_puts("  R0:  0x"); fault_puthex32(r0);  fault_puts("\r\n");
    fault_puts("  R1:  0x"); fault_puthex32(r1);  fault_puts("\r\n");
    fault_puts("  R2:  0x"); fault_puthex32(r2);  fault_puts("\r\n");
    fault_puts("  R3:  0x"); fault_puthex32(r3);  fault_puts("\r\n");
    fault_puts("  R12: 0x"); fault_puthex32(r12); fault_puts("\r\n");

    fault_puts("\r\nCall stack:\r\n");
    fault_puts("  #0  PC=0x"); fault_puthex32(pc); fault_puts("\r\n");
    fault_puts("  #1  LR=0x"); fault_puthex32(lr); fault_puts("\r\n");

    uint32_t *sp = fault_stack;
    int frame = 2;
    for (uint32_t *p = sp; p < (uint32_t *)0x20005000 && frame < 20; p++)
    {
        uint32_t val = *p;
        if (val >= 0x08000000 && val <= 0x0801FFFF && (val & 1))
        {
            fault_puts("  #"); fault_putdec(frame);
            fault_puts("  0x"); fault_puthex32(val);
            fault_putc('\r'); fault_putc('\n');
            frame++;
        }
    }

    fault_puts("\r\nStack dump:\r\n");
    for (int i = 0; i < 64; i += 4)
    {
        fault_puthex32((uint32_t)&sp[i]); fault_puts(": ");
        fault_puthex32(sp[i]);   fault_putc(' ');
        fault_puthex32(sp[i+1]); fault_putc(' ');
        fault_puthex32(sp[i+2]); fault_putc(' ');
        fault_puthex32(sp[i+3]); fault_putc('\r'); fault_putc('\n');
    }

    for (;;) {}
}

#define DEFINE_FAULT_HANDLER(name)                     \
    void name(void)                                    \
    {                                                  \
        __asm volatile(                                \
            " tst lr, #4                        \n"    \
            " ite eq                            \n"    \
            " mrseq r0, msp                     \n"    \
            " mrsne r0, psp                     \n"    \
            " b fault_entry                     \n"    \
        );                                               \
    }

DEFINE_FAULT_HANDLER(HardFault_Handler)
DEFINE_FAULT_HANDLER(MemManage_Handler)
DEFINE_FAULT_HANDLER(BusFault_Handler)
DEFINE_FAULT_HANDLER(UsageFault_Handler)

void NMI_Handler(void) {}
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
