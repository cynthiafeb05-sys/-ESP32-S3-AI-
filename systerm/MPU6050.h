#ifndef __MPU6050_H
#define __MPU6050_H

#include "stm32f10x.h"
#include <stdint.h>

typedef uint8_t u8;

/*
 * MPU6050 uses a 7-bit I2C address of 0x68 when AD0 is connected to GND.
 * The software I2C driver sends the address in STM32 8-bit form, so it is
 * shifted left by one bit here.
 */
#define MPU6050_ADDR  (0x68 << 1)

/* MPU6050 register addresses used by this project. */
#define MPU6050_RA_PWR_MGMT_1   0x6B
#define MPU6050_RA_SMPLRT_DIV   0x19
#define MPU6050_RA_CONFIG       0x1A
#define MPU6050_RA_GYRO_CONFIG  0x1B
#define MPU6050_RA_ACCEL_CONFIG 0x1C
#define MPU6050_RA_WHO_AM_I     0x75
#define MPU6050_RA_ACCEL_XOUT_H 0x3B
#define MPU6050_RA_GYRO_XOUT_H  0x43

/* Public MPU6050 APIs. */
void MPU6050_Init(void);
uint8_t MPU6050_ReadID(void);

void MPU6050_ReadAcc(short *acc);
void MPU6050_ReadGyro(short *gyro);
void MPU6050_ReadTemp(short *temp);

/* Low-level register access APIs used by the driver. */
void MPU6050_WriteReg(uint8_t reg, uint8_t data);
void MPU6050_ReadData(uint8_t reg, uint8_t *buf, uint8_t len);

#endif
