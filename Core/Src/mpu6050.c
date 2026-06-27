#include "mpu6050.h"

#define CALIB_DELAY_MS 20

static I2C_HandleTypeDef *mpu_i2c;
static uint8_t accel_fs = MPU6050_ACCEL_FS_2;
static uint8_t gyro_fs  = MPU6050_GYRO_FS_250;

uint8_t MPU6050_Init(I2C_HandleTypeDef *i2c)
{
    mpu_i2c = i2c;

    HAL_Delay(100);

    uint8_t whoami = 0;
    if (HAL_I2C_Mem_Read(i2c, MPU6050_ADDR, MPU6050_WHO_AM_I, 1, &whoami, 1, 100) != HAL_OK)
        return 0;
    if (whoami != 0x68 && whoami != 0x70 && whoami != 0x71 && whoami != 0x72)
        return 0;

    uint8_t val;

    val = 0x07;  // reset gyro + accel + temp signal paths
    HAL_I2C_Mem_Write(i2c, MPU6050_ADDR, 0x68, 1, &val, 1, 100);
    HAL_Delay(50);

    val = 0x01;  // CLKSEL=1: PLL with X gyro ref (wake up)
    HAL_I2C_Mem_Write(i2c, MPU6050_ADDR, MPU6050_PWR_MGMT_1, 1, &val, 1, 100);
    HAL_Delay(50);

    for (int i = 0; i < 3; i++)
    {
        val = 0x03;  // FS_SEL=0 (±250), FCHOICE_B=0b11 (gyro DLPF)
        HAL_I2C_Mem_Write(i2c, MPU6050_ADDR, MPU6050_GYRO_CONFIG, 1, &val, 1, 100);
        HAL_Delay(1);
    }

    val = 0x00;
    HAL_I2C_Mem_Write(i2c, MPU6050_ADDR, MPU6050_ACCEL_CONFIG, 1, &val, 1, 100);

    return 1;
}

void MPU6050_SetDLPF(I2C_HandleTypeDef *i2c, uint8_t dlpf)
{
    uint8_t val = dlpf & 0x07;
    HAL_I2C_Mem_Write(i2c, MPU6050_ADDR, MPU6050_CONFIG, 1, &val, 1, 100);
}

void MPU6050_SetSampleRate(I2C_HandleTypeDef *i2c, uint16_t rate_hz)
{
    if (rate_hz == 0) rate_hz = 1;
    uint8_t div = (uint8_t)(1000 / rate_hz - 1);
    HAL_I2C_Mem_Write(i2c, MPU6050_ADDR, MPU6050_SMPLRT_DIV, 1, &div, 1, 100);
}

uint8_t MPU6050_Calibrate(I2C_HandleTypeDef *i2c, MPU6050_Calib_t *calib,
                          uint16_t samples, MPU6050_ProgressCB progress)
{
    if (samples == 0) return 0;

    int32_t sum_ax = 0, sum_ay = 0, sum_az = 0;
    int32_t sum_gx = 0, sum_gy = 0, sum_gz = 0;
    uint8_t buf[14];

    for (uint16_t i = 0; i < samples; i++)
    {
        if (HAL_I2C_Mem_Read(i2c, MPU6050_ADDR, MPU6050_ACCEL_XOUT_H, 1, buf, 14, 100) != HAL_OK)
            return 0;

        sum_ax += (int16_t)((buf[0]  << 8) | buf[1]);
        sum_ay += (int16_t)((buf[2]  << 8) | buf[3]);
        sum_az += (int16_t)((buf[4]  << 8) | buf[5]);
        sum_gx += (int16_t)((buf[8]  << 8) | buf[9]);
        sum_gy += (int16_t)((buf[10] << 8) | buf[11]);
        sum_gz += (int16_t)((buf[12] << 8) | buf[13]);

        if (progress)
        {
            int pct = (i + 1) * 100 / samples;
            if (pct % 10 == 0 || i == samples - 1)
                progress(pct);
        }

        HAL_Delay(CALIB_DELAY_MS);
    }

    calib->ax_off = (int16_t)(sum_ax / (int32_t)samples);
    calib->ay_off = (int16_t)(sum_ay / (int32_t)samples);
    calib->az_off = (int16_t)(sum_az / (int32_t)samples) - 16384;
    calib->gx_off = (int16_t)(sum_gx / (int32_t)samples);
    calib->gy_off = (int16_t)(sum_gy / (int32_t)samples);
    calib->gz_off = (int16_t)(sum_gz / (int32_t)samples);

    return 1;
}

void MPU6050_Correct(uint8_t *buffer, const MPU6050_Calib_t *calib)
{
    int16_t raw[6];
    raw[0] = (int16_t)((buffer[0]  << 8) | buffer[1]);
    raw[1] = (int16_t)((buffer[2]  << 8) | buffer[3]);
    raw[2] = (int16_t)((buffer[4]  << 8) | buffer[5]);
    raw[3] = (int16_t)((buffer[8]  << 8) | buffer[9]);
    raw[4] = (int16_t)((buffer[10] << 8) | buffer[11]);
    raw[5] = (int16_t)((buffer[12] << 8) | buffer[13]);

    raw[0] -= calib->ax_off;
    raw[1] -= calib->ay_off;
    raw[2] -= calib->az_off;
    raw[3] -= calib->gx_off;
    raw[4] -= calib->gy_off;
    raw[5] -= calib->gz_off;

    buffer[0]  = raw[0] & 0xFF;         buffer[1]  = (raw[0] >> 8) & 0xFF;
    buffer[2]  = raw[1] & 0xFF;         buffer[3]  = (raw[1] >> 8) & 0xFF;
    buffer[4]  = raw[2] & 0xFF;         buffer[5]  = (raw[2] >> 8) & 0xFF;
    buffer[8]  = raw[3] & 0xFF;         buffer[9]  = (raw[3] >> 8) & 0xFF;
    buffer[10] = raw[4] & 0xFF;         buffer[11] = (raw[4] >> 8) & 0xFF;
    buffer[12] = raw[5] & 0xFF;         buffer[13] = (raw[5] >> 8) & 0xFF;
}

uint8_t MPU6050_Read_DMA(I2C_HandleTypeDef *i2c, uint8_t *buffer)
{
    return HAL_I2C_Mem_Read_DMA(i2c, MPU6050_ADDR, MPU6050_ACCEL_XOUT_H, 1, buffer, 14);
}
