#include "main.h"
#include "mpu6050.h"
#include "transport.h"
#include "version.h"
#include "har_model.h"
#include <stdint.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_i2c1_tx;
DMA_HandleTypeDef hdma_i2c1_rx;
DMA_HandleTypeDef hdma_usart1_tx;

static uint8_t mpu_buf[14];
static volatile uint8_t i2c_busy;
static MPU6050_Calib_t mpu_calib;
static HAR_RingBuf_t har_rb;
static volatile uint8_t scheduler_running;

void xPortSysTickHandler(void);

void SysTick_Handler(void)
{
    if (scheduler_running)
        xPortSysTickHandler();
    else
        HAL_IncTick();
}

static void print_reset_cause(void)
{
    const char *cause = "Unknown";
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST))      cause = "Power-on (POR)";
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST))   cause = "Software";
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST))  cause = "IWDG";
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST))  cause = "WWDG";
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST))   cause = "NRST pin";
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST))  cause = "Low-power";

    HAL_UART_Transmit(&huart1, (uint8_t *)"Reset: ", 7, 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)cause, strlen(cause), 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2, 100);
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

static void startup_banner(void)
{
    const char *lines[] = {
        "\r\n",
        "  Activity Recognition\r\n",
        "\r\n",
        "  " FW_NAME " v" FW_VERSION "\r\n",
        "  Build: " FW_BUILD "\r\n",
        "  MCU:   STM32F103C8T6 (72MHz)\r\n",
        "  IMU:   MPU6050 (I2C+DMA @ 100kHz)\r\n",
        "  Link:  UART1 (DMA TX @ 115200 baud)\r\n",
        "\r\n",
    };
    for (int i = 0; i < (int)(sizeof(lines) / sizeof(lines[0])); i++)
    {
        HAL_UART_Transmit(&huart1, (uint8_t *)lines[i],
                          strlen(lines[i]), 100);
    }
}

static void i2c_read_next(void)
{
    i2c_busy = 1;
    HAL_I2C_Mem_Read_DMA(&hi2c1, MPU6050_ADDR, MPU6050_ACCEL_XOUT_H,
                         1, mpu_buf, 14);
}

static char *fmt_fixed(char *p, int16_t raw, int16_t divisor, int dec)
{
    int32_t v = raw;
    if (v < 0) { *p++ = '-'; v = -v; }
    int32_t ip = v / divisor;
    int32_t fp = v % divisor;
    for (int i = 0; i < dec; i++) fp *= 10;
    fp /= divisor;

    char buf[8], *q = buf;
    do { *q++ = '0' + ip % 10; ip /= 10; } while (ip);
    do { *p++ = *--q; } while (q > buf);
    *p++ = '.';
    for (int i = dec; i > 0; i--) { buf[i-1] = '0' + fp % 10; fp /= 10; }
    for (int i = 0; i < dec; i++) *p++ = buf[i];
    return p;
}

static void format_imu_line(int16_t *s)
{
    static char line[64];
    char *p = line;
    p = fmt_fixed(p, s[0], 16384, 4); *p++ = ',';  // ax (g)
    p = fmt_fixed(p, s[1], 16384, 4); *p++ = ',';  // ay (g)
    p = fmt_fixed(p, s[2], 16384, 4); *p++ = ',';  // az (g)
    p = fmt_fixed(p, s[4], 131,   2); *p++ = ',';  // gx (°/s)
    p = fmt_fixed(p, s[5], 131,   2); *p++ = ',';  // gy (°/s)
    p = fmt_fixed(p, s[6], 131,   2);              // gz (°/s)
    *p++ = '\r'; *p++ = '\n';
    Output_Write((uint8_t *)line, p - line);
}

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C1) return;
    MPU6050_Correct(mpu_buf, &mpu_calib);
    format_imu_line((int16_t *)mpu_buf);
    i2c_read_next();
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C1) return;
    i2c_busy = 0;
    i2c_read_next();
}

void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void MX_DMA_Init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    hdma_i2c1_tx.Instance = DMA1_Channel6;
    hdma_i2c1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_i2c1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_i2c1_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_i2c1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_i2c1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_i2c1_tx.Init.Mode = DMA_NORMAL;
    hdma_i2c1_tx.Init.Priority = DMA_PRIORITY_LOW;
    HAL_DMA_Init(&hdma_i2c1_tx);
    __HAL_LINKDMA(&hi2c1, hdmatx, hdma_i2c1_tx);

    hdma_i2c1_rx.Instance = DMA1_Channel7;
    hdma_i2c1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_i2c1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_i2c1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_i2c1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_i2c1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_i2c1_rx.Init.Mode = DMA_NORMAL;
    hdma_i2c1_rx.Init.Priority = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&hdma_i2c1_rx);
    __HAL_LINKDMA(&hi2c1, hdmarx, hdma_i2c1_rx);

    hdma_usart1_tx.Instance = DMA1_Channel4;
    hdma_usart1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_tx.Init.MemDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_tx.Init.Mode = DMA_NORMAL;
    hdma_usart1_tx.Init.Priority = DMA_PRIORITY_LOW;
    HAL_DMA_Init(&hdma_usart1_tx);
    __HAL_LINKDMA(&huart1, hdmatx, hdma_usart1_tx);

    HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);
}

void MX_I2C1_Init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c1);
}

void MX_USART1_UART_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);

    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1)
    {
        HAL_NVIC_SetPriority(I2C1_EV_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
        HAL_NVIC_SetPriority(I2C1_ER_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
    }
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) return;
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

void HAL_DMA_MspInit(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
}

void HAL_MspInit(void)
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

void vApplicationTickHook(void)
{
    HAL_IncTick();
}

static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
static StaticTask_t idle_tcb;
static StackType_t blink_stack[configMINIMAL_STACK_SIZE];
static StaticTask_t blink_tcb;

static void blink_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        vTaskDelay(pdMS_TO_TICKS(500));
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &idle_tcb;
    *ppxIdleTaskStackBuffer = idle_stack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void _init(void) {}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_I2C1_Init();
    MX_USART1_UART_Init();
    Output_Init();

    print_reset_cause();

    startup_banner();

    if (!MPU6050_Init(&hi2c1))
    {
        uint8_t msg[] = "MPU6050 init failed, continuing anyway\r\n";
        HAL_UART_Transmit(&huart1, msg, sizeof(msg) - 1, 100);
    }
    else
    {
        MPU6050_SetDLPF(&hi2c1, MPU6050_DLPF_21HZ);
        MPU6050_SetSampleRate(&hi2c1, 50);

        HAL_UART_Transmit(&huart1, (uint8_t *)"Calibrating... ", 16, 100);
        if (MPU6050_Calibrate(&hi2c1, &mpu_calib, 100))
        {
            uint8_t msg[] = "done\r\n";
            HAL_UART_Transmit(&huart1, msg, sizeof(msg) - 1, 100);
        }

        HAR_Init(&har_rb);
        i2c_read_next();
    }
    xTaskCreateStatic(blink_task, "blink", configMINIMAL_STACK_SIZE,
                      NULL, 1, blink_stack, &blink_tcb);
    HAL_UART_Transmit(&huart1, (uint8_t *)"Starting scheduler...\r\n", 23, 100);
    scheduler_running = 1;
    vTaskStartScheduler();

    HAL_UART_Transmit(&huart1, (uint8_t *)"Scheduler FAILED to start!\r\n", 28, 100);
    for (;;) {}
}
