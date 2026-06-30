#include "main.h"
#include "mpu6050.h"
#include "transport.h"
#include "nrf24.h"
#include "packet.h"
#include "version.h"
#include <stdint.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

SPI_HandleTypeDef hspi1;
I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_i2c1_tx;
DMA_HandleTypeDef hdma_i2c1_rx;
DMA_HandleTypeDef hdma_usart1_tx;

static uint8_t mpu_buf[14];
static MPU6050_Calib_t mpu_calib;
static NRF24_t nrf_dev;
static uint32_t pkt_index;
static TaskHandle_t sensor_task_handle;
static volatile uint8_t scheduler_running;

#define DEBUG_RING_LEN 16
static char debug_ring[DEBUG_RING_LEN][48];
static volatile uint16_t debug_wp, debug_rp;
static volatile uint8_t debug_enabled = 1;

static void my_strcpy(char *d, const char *s, int max)
{
    int i = 0;
    while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}

void debug_puts(const char *s)
{
    uint16_t nwp = (debug_wp + 1) & (DEBUG_RING_LEN - 1);
    if (nwp == debug_rp) return;
    my_strcpy(debug_ring[debug_wp], s, sizeof(debug_ring[0]));
    debug_wp = nwp;
}

static const char *debug_gets(void)
{
    if (debug_rp == debug_wp) return 0;
    const char *s = debug_ring[debug_rp];
    debug_rp = (debug_rp + 1) & (DEBUG_RING_LEN - 1);
    return s;
}

void xPortSysTickHandler(void);

static void prog_bar(int pct)
{
    char buf[28];
    uint8_t n = 0;
    buf[n++] = '\r'; buf[n++] = '[';
    int seg = pct / 5;
    for (int i = 0; i < 20; i++)
        buf[n++] = (i < seg) ? '=' : ' ';
    buf[n++] = ']'; buf[n++] = ' ';
    if (pct >= 100) buf[n++] = '1';
    if (pct >= 10)  buf[n++] = '0' + (pct / 10) % 10;
    buf[n++] = '0' + pct % 10;
    buf[n++] = '%';
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, n, 100);
}

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
        "  Link:  nRF24L01+ (SPI1 @ 9MHz) full-duplex\r\n",
        "  UART:  USB-UART (DMA TX @ 115200)\r\n",
        "\r\n",
    };
    for (int i = 0; i < (int)(sizeof(lines) / sizeof(lines[0])); i++)
    {
        HAL_UART_Transmit(&huart1, (uint8_t *)lines[i],
                          strlen(lines[i]), 100);
    }
}

static void i2c_start_read(void)
{
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



void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C1) return;
    BaseType_t higher = pdFALSE;
    xTaskNotifyGive(sensor_task_handle);
    portYIELD_FROM_ISR(higher);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C1) return;
    i2c_start_read();
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

    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_1, GPIO_PIN_SET);
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
    hdma_usart1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
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

void MX_SPI1_Init(void)
{
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 10;
    HAL_SPI_Init(&hspi1);
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
    __HAL_RCC_SPI1_CLK_ENABLE();
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

void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1) {
        __HAL_RCC_SPI1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        GPIO_InitTypeDef gpio = {0};
        gpio.Pin = GPIO_PIN_5 | GPIO_PIN_7;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &gpio);
        gpio.Pin = GPIO_PIN_6;
        gpio.Mode = GPIO_MODE_INPUT;
        gpio.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &gpio);
    }
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

static StackType_t idle_stack[256];
static StaticTask_t idle_tcb;
static StackType_t blink_stack[128];
static StaticTask_t blink_tcb;
static StackType_t sensor_stack[512];
static StaticTask_t sensor_tcb;

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

static void handle_ack_command(CmdPayload *cmd)
{
    switch (cmd->cmd_id)
    {
        case CMD_DEBUG_ON:
            debug_enabled = 1;
            debug_puts("dbg:on");
            break;
        case CMD_DEBUG_OFF:
            debug_enabled = 0;
            debug_puts("dbg:off");
            break;
        case CMD_CALIBRATE:
            debug_puts("cal:start");
            break;
        case CMD_RESET:
            NVIC_SystemReset();
            break;
    }
}

static void sensor_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    vTaskDelay(pdMS_TO_TICKS(10));
    i2c_start_read();
    int16_t gxf = 0, gyf = 0, gzf = 0;

    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(20));

        MPU6050_Correct(mpu_buf, &mpu_calib);
        int16_t *s = (int16_t *)mpu_buf;

        gxf = (int16_t)(((int32_t)gxf * 7 + (int32_t)s[4]) / 8);
        gyf = (int16_t)(((int32_t)gyf * 7 + (int32_t)s[5]) / 8);
        gzf = (int16_t)(((int32_t)gzf * 7 + (int32_t)s[6]) / 8);
        s[4] = gxf; s[5] = gyf; s[6] = gzf;

        Packet tx_pkt;
        uint32_t magic;
        uint8_t *pload;
        uint8_t plen;
        IMUPayload imu;
        DebugPayload dp;

        const char *dmsg = debug_enabled ? debug_gets() : 0;
        if (dmsg)
        {
            int di = 0;
            while (di < (int)sizeof(dp.text) - 1 && dmsg[di]) {
                dp.text[di] = dmsg[di];
                di++;
            }
            dp.text[di] = 0;
            dp.len = di;
            magic = PACKET_MAGIC_DEBUG;
            pload = (uint8_t *)&dp;
            plen = sizeof(dp);
        }
        else
        {
            imu.ax = s[0]; imu.ay = s[1]; imu.az = s[2];
            imu.gx = s[4]; imu.gy = s[5]; imu.gz = s[6];
            imu.lpf_enabled = 1;
            imu.batt_mv = 3300;
            memset(imu._pad, 0, sizeof(imu._pad));
            imu._pad[0] = 0xCC; imu._pad[1] = 0xDD; imu._pad[2] = 0xEE;
            magic = PACKET_MAGIC_IMU;
            pload = (uint8_t *)&imu;
            plen = sizeof(imu);
        }

        packet_fill(&tx_pkt, pkt_index++, magic, pload, plen);

        uint8_t ack_buf[NRF24_MAX_PAYLOAD];
        uint8_t ack_len = sizeof(ack_buf);
        NRF24_TransmitAck((uint8_t *)&tx_pkt, sizeof(Packet),
                          ack_buf, &ack_len, 2);

        if (ack_len == sizeof(Packet))
        {
            Packet *ap = (Packet *)ack_buf;
            if (packet_verify(ap) && ap->magic == PACKET_MAGIC_CMD)
            {
                handle_ack_command((CmdPayload *)ap->payload);
            }
        }

        i2c_start_read();
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
    MX_SPI1_Init();
    MX_DMA_Init();
    MX_I2C1_Init();
    MX_USART1_UART_Init();
    Output_Init();

    print_reset_cause();
    startup_banner();

    const uint8_t nrf_addr[5] = {0xAA, 0x00, 0x00, 0x00, 0x01};
    uint8_t nrf_ok = NRF24_Init(&nrf_dev, nrf_addr);
    if (!nrf_ok)
    {
        uint8_t msg[] = "nRF24L01 init failed\r\n";
        HAL_UART_Transmit(&huart1, msg, sizeof(msg) - 1, 100);
    }
    else
    {
        HAL_UART_Transmit(&huart1, (uint8_t *)"nRF24L01+ OK\r\n", 14, 100);
        NRF24_SetMode(NRF24_TX);
    }

    if (!MPU6050_Init(&hi2c1))
    {
        uint8_t msg[] = "MPU6050 init failed\r\n";
        HAL_UART_Transmit(&huart1, msg, sizeof(msg) - 1, 100);
    }
    else
    {
        MPU6050_SetDLPF(&hi2c1, MPU6050_DLPF_21HZ);
        MPU6050_SetSampleRate(&hi2c1, 50);

        HAL_UART_Transmit(&huart1, (uint8_t *)
            "Place sensor flat on a table, chip facing up.\r\n", 50, 100);

        for (;;)
        {
            HAL_UART_Transmit(&huart1, (uint8_t *)
                "Waiting for stable position...\r\n", 34, 100);

#define SWIN 20
            int16_t sx[SWIN], sy[SWIN], sz[SWIN];
            uint8_t sci = 0, cnt = 0, tout = 0;

            for (;;)
            {
                uint8_t buf[14];
                HAL_I2C_Mem_Read(&hi2c1, MPU6050_ADDR, MPU6050_ACCEL_XOUT_H,
                                 1, buf, 14, 100);
                sx[sci] = (int16_t)((buf[0]  << 8) | buf[1]);
                sy[sci] = (int16_t)((buf[2]  << 8) | buf[3]);
                sz[sci] = (int16_t)((buf[4]  << 8) | buf[5]);
                sci = (sci + 1) % SWIN;

                HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

                if (++cnt % 50 == 0)
                {
                    int16_t ax = sx[(sci + SWIN - 1) % SWIN];
                    int16_t ay = sy[(sci + SWIN - 1) % SWIN];
                    int16_t az = sz[(sci + SWIN - 1) % SWIN];
                    int16_t gx = (int16_t)((buf[8]  << 8) | buf[9]);
                    int16_t gy = (int16_t)((buf[10] << 8) | buf[11]);
                    int16_t gz = (int16_t)((buf[12] << 8) | buf[13]);
                    char line[64], *p = line;
                    *p++ = 'X'; *p++ = '='; p = fmt_fixed(p, ax, 16384, 4);
                    *p++ = ' '; *p++ = 'Y'; *p++ = '='; p = fmt_fixed(p, ay, 16384, 4);
                    *p++ = ' '; *p++ = 'Z'; *p++ = '='; p = fmt_fixed(p, az, 16384, 4);
                    *p++ = ' '; *p++ = 'G'; *p++ = '='; p = fmt_fixed(p, gx, 131, 2);
                    *p++ = ' '; p = fmt_fixed(p, gy, 131, 2);
                    *p++ = ' '; p = fmt_fixed(p, gz, 131, 2);
                    *p++ = '\r'; *p++ = '\n';
                    HAL_UART_Transmit(&huart1, (uint8_t *)line, p - line, 100);
                }

                if (sci == 0)
                {
                    tout++;
                    int32_t mx = 0, my = 0, mz = 0;
                    for (int i = 0; i < SWIN; i++)
                    { mx += sx[i]; my += sy[i]; mz += sz[i]; }
                    mx /= SWIN; my /= SWIN; mz /= SWIN;

                    int32_t dx = 0, dy = 0, dz = 0;
                    for (int i = 0; i < SWIN; i++)
                    {
                        int32_t d;
                        d = sx[i] - mx; if (d < 0) d = -d; if (d > dx) dx = d;
                        d = sy[i] - my; if (d < 0) d = -d; if (d > dy) dy = d;
                        d = sz[i] - mz; if (d < 0) d = -d; if (d > dz) dz = d;
                    }

                    if ((dx < 500 && dy < 500 && dz < 500
                         && mz > 7000
                         && mx > -12000 && mx < 12000
                         && my > -12000 && my < 12000)
                        || tout > 500)
                    {
                        if (tout > 500)
                            HAL_UART_Transmit(&huart1, (uint8_t *)
                                "Timeout. Calibrating with current position...\r\n", 50, 100);
                        else
                            HAL_UART_Transmit(&huart1, (uint8_t *)
                                "Stable. Calibrating...\r\n", 24, 100);
                        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
                        break;
                    }
                }
                HAL_Delay(20);
            }
#undef SWIN

            prog_bar(0);
            if (MPU6050_Calibrate(&hi2c1, &mpu_calib, 100, prog_bar))
            {
                int good = 1;
                uint8_t tbuf[14];
                for (int i = 0; i < 5; i++)
                {
                    HAL_I2C_Mem_Read(&hi2c1, MPU6050_ADDR,
                                     MPU6050_ACCEL_XOUT_H, 1, tbuf, 14, 100);
                    MPU6050_Correct(tbuf, &mpu_calib);
                    int16_t *s = (int16_t *)tbuf;
                    if (s[2] < 12000 || s[2] > 20000
                        || (s[0] < 0 ? -s[0] : s[0]) > 5000
                        || (s[1] < 0 ? -s[1] : s[1]) > 5000)
                    {
                        good = 0;
                        break;
                    }
                    HAL_Delay(20);
                }
                if (good)
                {
                    HAL_UART_Transmit(&huart1, (uint8_t *)
                        "\r\nCalibration OK\r\n", 18, 100);
                    break;
                }
            }
            HAL_UART_Transmit(&huart1, (uint8_t *)
                "\r\nBad calibration, retrying...\r\n", 32, 100);
        }
    }

    xTaskCreateStatic(blink_task, "blink", configMINIMAL_STACK_SIZE,
                      NULL, 1, blink_stack, &blink_tcb);
    sensor_task_handle = xTaskCreateStatic(sensor_task, "sensor",
                      512,
                      NULL, 3, sensor_stack, &sensor_tcb);

    HAL_UART_Transmit(&huart1, (uint8_t *)"Starting scheduler...\r\n", 23, 100);
    scheduler_running = 1;
    vTaskStartScheduler();

    HAL_UART_Transmit(&huart1, (uint8_t *)"Scheduler FAILED to start!\r\n", 28, 100);
    for (;;) {}
}
