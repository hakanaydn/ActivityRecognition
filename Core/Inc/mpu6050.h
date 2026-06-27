#ifndef __MPU6050_H
#define __MPU6050_H

#include "stm32f1xx_hal.h"

#define MPU6050_ADDR            0xD0
#define MPU6050_WHO_AM_I        0x75
#define MPU6050_PWR_MGMT_1      0x6B
#define MPU6050_ACCEL_XOUT_H    0x3B
#define MPU6050_GYRO_CONFIG     0x1B
#define MPU6050_ACCEL_CONFIG    0x1C
#define MPU6050_CONFIG          0x1A
#define MPU6050_SMPLRT_DIV      0x19

#define MPU6050_DLPF_260HZ      0x00
#define MPU6050_DLPF_184HZ      0x01
#define MPU6050_DLPF_94HZ       0x02
#define MPU6050_DLPF_44HZ       0x03
#define MPU6050_DLPF_21HZ       0x04
#define MPU6050_DLPF_10HZ       0x05
#define MPU6050_DLPF_5HZ        0x06

#define MPU6050_GYRO_FS_250     0x00
#define MPU6050_GYRO_FS_500     0x08
#define MPU6050_GYRO_FS_1000    0x10
#define MPU6050_GYRO_FS_2000    0x18

#define MPU6050_ACCEL_FS_2      0x00
#define MPU6050_ACCEL_FS_4      0x08
#define MPU6050_ACCEL_FS_8      0x10
#define MPU6050_ACCEL_FS_16     0x18

typedef struct {
    int16_t ax, ay, az;
    int16_t temp;
    int16_t gx, gy, gz;
} MPU6050_Data_t;

typedef struct {
    int16_t gx_off, gy_off, gz_off;
    int16_t ax_off, ay_off, az_off;
} MPU6050_Calib_t;

uint8_t MPU6050_Init(I2C_HandleTypeDef *i2c);
void    MPU6050_SetDLPF(I2C_HandleTypeDef *i2c, uint8_t dlpf);
void    MPU6050_SetSampleRate(I2C_HandleTypeDef *i2c, uint16_t rate_hz);
uint8_t MPU6050_Calibrate(I2C_HandleTypeDef *i2c, MPU6050_Calib_t *calib, uint16_t samples);
void    MPU6050_Correct(uint8_t *buffer, const MPU6050_Calib_t *calib);
uint8_t MPU6050_Read_DMA(I2C_HandleTypeDef *i2c, uint8_t *buffer);

#endif
