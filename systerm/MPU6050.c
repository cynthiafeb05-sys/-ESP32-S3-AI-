#include "MPU6050.h"
#include <rtthread.h>
#include <stdio.h>

/*
 * MPU6050姿态模块说明：
 * 1. 本文件使用PB3/PB4模拟I2C访问MPU6050；
 * 2. 主业务线程读取三轴加速度和陀螺仪，再计算Pitch/Roll和摔倒风险；
 * 3. PB3默认是JTAG引脚，所以初始化时会关闭JTAG、保留SWD调试。
 *
 * This project uses software I2C for MPU6050 on PB3/PB4.
 * PB3 is also a JTAG pin on STM32F103, so JTAG is disabled and SWD remains enabled.
 */
#define MPU_I2C_PORT      GPIOB
#define MPU_I2C_CLK       RCC_APB2Periph_GPIOB
#define MPU_I2C_SCL_PIN   GPIO_Pin_3
#define MPU_I2C_SDA_PIN   GPIO_Pin_4

#define MPU_SCL_H()       GPIO_SetBits(MPU_I2C_PORT, MPU_I2C_SCL_PIN)
#define MPU_SCL_L()       GPIO_ResetBits(MPU_I2C_PORT, MPU_I2C_SCL_PIN)
#define MPU_SDA_H()       GPIO_SetBits(MPU_I2C_PORT, MPU_I2C_SDA_PIN)
#define MPU_SDA_L()       GPIO_ResetBits(MPU_I2C_PORT, MPU_I2C_SDA_PIN)
#define MPU_SDA_READ()    GPIO_ReadInputDataBit(MPU_I2C_PORT, MPU_I2C_SDA_PIN)

static void MPU_I2C_Delay(void)
{
    uint8_t i;

    /* Short delay used to create a stable software I2C clock. */
    for (i = 0; i < 10; i++)
    {
    }
}

static void MPU_I2C_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    static uint8_t is_init = 0;

    if (is_init)
    {
        return;
    }

    RCC_APB2PeriphClockCmd(MPU_I2C_CLK | RCC_APB2Periph_AFIO, ENABLE);

    /* Release PB3 from JTAG so it can be used as MPU6050 SCL. SWD debug is kept. */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    GPIO_InitStructure.GPIO_Pin = MPU_I2C_SCL_PIN | MPU_I2C_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MPU_I2C_PORT, &GPIO_InitStructure);

    MPU_SCL_H();
    MPU_SDA_H();

    is_init = 1;
}

static void MPU_I2C_Start(void)
{
    MPU_SDA_H();
    MPU_SCL_H();
    MPU_I2C_Delay();
    MPU_SDA_L();
    MPU_I2C_Delay();
    MPU_SCL_L();
}

static void MPU_I2C_Stop(void)
{
    MPU_SDA_L();
    MPU_SCL_H();
    MPU_I2C_Delay();
    MPU_SDA_H();
    MPU_I2C_Delay();
}

static void MPU_I2C_SendByte(uint8_t data)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        if (data & 0x80)
        {
            MPU_SDA_H();
        }
        else
        {
            MPU_SDA_L();
        }

        data <<= 1;
        MPU_I2C_Delay();
        MPU_SCL_H();
        MPU_I2C_Delay();
        MPU_SCL_L();
        MPU_I2C_Delay();
    }

    /* Release SDA so the slave can drive the ACK bit. */
    MPU_SDA_H();
}

static uint8_t MPU_I2C_WaitAck(void)
{
    uint8_t ack;

    MPU_SDA_H();
    MPU_I2C_Delay();
    MPU_SCL_H();
    MPU_I2C_Delay();
    ack = MPU_SDA_READ() ? 1 : 0;
    MPU_SCL_L();
    MPU_I2C_Delay();

    return ack;
}

static void MPU_I2C_Ack(void)
{
    MPU_SDA_L();
    MPU_I2C_Delay();
    MPU_SCL_H();
    MPU_I2C_Delay();
    MPU_SCL_L();
    MPU_SDA_H();
}

static void MPU_I2C_NAck(void)
{
    MPU_SDA_H();
    MPU_I2C_Delay();
    MPU_SCL_H();
    MPU_I2C_Delay();
    MPU_SCL_L();
}

static uint8_t MPU_I2C_ReadByte(uint8_t ack)
{
    uint8_t i;
    uint8_t data = 0;

    MPU_SDA_H();
    for (i = 0; i < 8; i++)
    {
        data <<= 1;
        MPU_SCL_H();
        MPU_I2C_Delay();
        if (MPU_SDA_READ())
        {
            data++;
        }
        MPU_SCL_L();
        MPU_I2C_Delay();
    }

    if (ack)
    {
        MPU_I2C_Ack();
    }
    else
    {
        MPU_I2C_NAck();
    }

    return data;
}

void MPU6050_WriteReg(uint8_t reg, uint8_t data)
{
    MPU_I2C_Start();
    MPU_I2C_SendByte(MPU6050_ADDR);
    (void)MPU_I2C_WaitAck();
    MPU_I2C_SendByte(reg);
    (void)MPU_I2C_WaitAck();
    MPU_I2C_SendByte(data);
    (void)MPU_I2C_WaitAck();
    MPU_I2C_Stop();
}

void MPU6050_ReadData(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    MPU_I2C_Start();
    MPU_I2C_SendByte(MPU6050_ADDR);
    (void)MPU_I2C_WaitAck();
    MPU_I2C_SendByte(reg);
    (void)MPU_I2C_WaitAck();

    /* Repeated start switches the bus from register-address write to data read. */
    MPU_I2C_Start();
    MPU_I2C_SendByte(MPU6050_ADDR | 0x01);
    (void)MPU_I2C_WaitAck();

    for (i = 0; i < len; i++)
    {
        buf[i] = MPU_I2C_ReadByte((i + 1U) < len);
    }

    MPU_I2C_Stop();
}

void MPU6050_Init(void)
{
    uint8_t id;

    MPU_I2C_GPIO_Init();

    /* Reset the chip and wait until the internal registers are ready. */
    MPU6050_WriteReg(MPU6050_RA_PWR_MGMT_1, 0x80);
    rt_thread_mdelay(100);

    /* Wake up the chip and select X-axis gyro as the clock source. */
    MPU6050_WriteReg(MPU6050_RA_PWR_MGMT_1, 0x01);
    rt_thread_mdelay(50);

    /* Configure sample rate, low-pass filter, gyro range, and accelerometer range. */
    MPU6050_WriteReg(MPU6050_RA_SMPLRT_DIV, 0x07);   /* 1 kHz / (1 + 7) = 125 Hz. */
    MPU6050_WriteReg(MPU6050_RA_CONFIG, 0x06);       /* Low-pass filter for stable posture data. */
    MPU6050_WriteReg(MPU6050_RA_GYRO_CONFIG, 0x18);  /* +-2000 deg/s gyro range. */
    MPU6050_WriteReg(MPU6050_RA_ACCEL_CONFIG, 0x00); /* +-2g accelerometer range, 16384 LSB/g. */

    id = MPU6050_ReadID();
    if (id != 0x68)
    {
        rt_kprintf("MPU6050 ERROR! ID:0x%02X\r\n", id);
    }
}

uint8_t MPU6050_ReadID(void)
{
    uint8_t id;

    MPU6050_ReadData(MPU6050_RA_WHO_AM_I, &id, 1);
    rt_kprintf("MPU6050 ID: 0x%02X\r\n", id);

    return id;
}

void MPU6050_ReadAcc(short *acc)
{
    uint8_t buf[6];

    MPU6050_ReadData(MPU6050_RA_ACCEL_XOUT_H, buf, 6);

    acc[0] = (short)((buf[0] << 8) | buf[1]);
    acc[1] = (short)((buf[2] << 8) | buf[3]);
    acc[2] = (short)((buf[4] << 8) | buf[5]);
}

void MPU6050_ReadGyro(short *gyro)
{
    uint8_t buf[6];

    MPU6050_ReadData(MPU6050_RA_GYRO_XOUT_H, buf, 6);

    gyro[0] = (short)((buf[0] << 8) | buf[1]);
    gyro[1] = (short)((buf[2] << 8) | buf[3]);
    gyro[2] = (short)((buf[4] << 8) | buf[5]);
}

void MPU6050_ReadTemp(short *temp)
{
    uint8_t buf[2];

    MPU6050_ReadData(0x41, buf, 2);
    *temp = (short)((buf[0] << 8) | buf[1]);
}
