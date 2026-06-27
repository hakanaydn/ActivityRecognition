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
    if (whoami != 0x68)
        return 0;

    uint8_t val = 0x00;
    HAL_I2C_Mem_Write(i2c, MPU6050_ADDR, MPU6050_PWR_MGMT_1, 1, &val, 1, 100);

    val = 0x00;
    HAL_I2C_Mem_Write(i2c, MPU6050_ADDR, MPU6050_GYRO_CONFIG, 1, &val, 1, 100);

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

uint8_t MPU6050_Calibrate(I2C_HandleTypeDef *i2c, MPU6050_Calib_t *calib, uint16_t samples)
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
    int16_t *axis[6] = {
        (int16_t *)(buffer + 0),
        (int16_t *)(buffer + 2),
        (int16_t *)(buffer + 4),
        (int16_t *)(buffer + 8),
        (int16_t *)(buffer + 10),
        (int16_t *)(buffer + 12),
    };
    int16_t offsets[6] = {
        calib->ax_off, calib->ay_off, calib->az_off,
        calib->gx_off, calib->gy_off, calib->gz_off,
    };
    for (int i = 0; i < 6; i++)
        *axis[i] = *axis[i] - offsets[i];
}

uint8_t MPU6050_Read_DMA(I2C_HandleTypeDef *i2c, uint8_t *buffer)
{
    return HAL_I2C_Mem_Read_DMA(i2c, MPU6050_ADDR, MPU6050_ACCEL_XOUT_H, 1, buffer, 14);
}
