#include "max30102.h"
#include <rtthread.h>

/*
 * MAX30102心率模块说明：
 * 1. 本文件使用GPIO模拟I2C读取MAX30102；
 * 2. MAX30102_ReadFIFO读取红光/红外原始值；
 * 3. MAX30102_UpdateHeartRate对IR信号做简单峰值检测，估算BPM。
 */

#define HEART_SCL_H()       GPIO_SetBits(MAX30102_SCL_PORT, MAX30102_SCL_PIN)      /* 模拟I2C：SCL拉高。 */
#define HEART_SCL_L()       GPIO_ResetBits(MAX30102_SCL_PORT, MAX30102_SCL_PIN)    /* 模拟I2C：SCL拉低。 */
#define HEART_SDA_H()       GPIO_SetBits(MAX30102_SDA_PORT, MAX30102_SDA_PIN)      /* 模拟I2C：SDA释放/拉高。 */
#define HEART_SDA_L()       GPIO_ResetBits(MAX30102_SDA_PORT, MAX30102_SDA_PIN)    /* 模拟I2C：SDA拉低。 */
#define HEART_SDA_READ()    GPIO_ReadInputDataBit(MAX30102_SDA_PORT, MAX30102_SDA_PIN) /* 读取SDA当前电平。 */

static void HEART_I2C_Delay(void)
{
    uint8_t i;                                  /* 简单空循环计数变量。 */
    for (i = 0; i < 10; i++)
    {
        /* 空循环用于产生模拟I2C所需的短延时。 */
    }
}

static void HEART_I2C_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;        /* GPIO初始化结构体。 */
    static uint8_t is_init = 0;                 /* 防止MAX30102 GPIO重复初始化。 */

    if (is_init)
    {
        return;                                 /* 已初始化则直接返回。 */
    }

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin = MAX30102_SCL_PIN | MAX30102_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = MAX30102_INT_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    HEART_SCL_H();                              /* I2C空闲状态SCL保持高电平。 */
    HEART_SDA_H();                              /* I2C空闲状态SDA保持高电平。 */

    is_init = 1;                                /* 标记GPIO已经初始化。 */
}

static void HEART_I2C_Start(void)
{
    HEART_SDA_H();                              /* 起始信号前保证SDA为高。 */
    HEART_SCL_H();                              /* 起始信号前保证SCL为高。 */
    HEART_I2C_Delay();                          /* 稳定总线电平。 */
    HEART_SDA_L();                              /* SCL高电平期间SDA由高变低，产生Start。 */
    HEART_I2C_Delay();                          /* 保持Start信号。 */
    HEART_SCL_L();                              /* 拉低SCL，准备发送数据。 */
}

static void HEART_I2C_Stop(void)
{
    HEART_SDA_L();                              /* 停止信号前先拉低SDA。 */
    HEART_SCL_H();                              /* 拉高SCL。 */
    HEART_I2C_Delay();                          /* 稳定SCL高电平。 */
    HEART_SDA_H();                              /* SCL高电平期间SDA由低变高，产生Stop。 */
    HEART_I2C_Delay();                          /* 保持Stop信号。 */
}

static void HEART_I2C_SendByte(uint8_t data)
{
    uint8_t i;                                  /* 发送8位数据的循环变量。 */

    for (i = 0; i < 8; i++)
    {
        if (data & 0x80)
        {
            HEART_SDA_H();
        }
        else
        {
            HEART_SDA_L();
        }

        data <<= 1;                             /* 左移一位，准备发送下一位。 */
        HEART_I2C_Delay();
        HEART_SCL_H();
        HEART_I2C_Delay();
        HEART_SCL_L();
        HEART_I2C_Delay();
    }

    HEART_SDA_H();                              /* 发送完后释放SDA，等待从机ACK。 */
}

static uint8_t HEART_I2C_WaitAck(void)
{
    uint8_t ack;                                /* ACK读取结果，0表示收到应答。 */

    HEART_SDA_H();
    HEART_I2C_Delay();
    HEART_SCL_H();
    HEART_I2C_Delay();
    ack = HEART_SDA_READ() ? 1 : 0;
    HEART_SCL_L();
    HEART_I2C_Delay();

    return ack;                                 /* 返回ACK状态，供读写函数判断通信是否正常。 */
}

static void HEART_I2C_Ack(void)
{
    HEART_SDA_L();
    HEART_I2C_Delay();
    HEART_SCL_H();
    HEART_I2C_Delay();
    HEART_SCL_L();
    HEART_SDA_H();
}

static void HEART_I2C_NAck(void)
{
    HEART_SDA_H();
    HEART_I2C_Delay();
    HEART_SCL_H();
    HEART_I2C_Delay();
    HEART_SCL_L();
}

static uint8_t HEART_I2C_ReadByte(uint8_t ack)
{
    uint8_t i;
    uint8_t data = 0;

    HEART_SDA_H();
    for (i = 0; i < 8; i++)
    {
        data <<= 1;
        HEART_SCL_H();
        HEART_I2C_Delay();
        if (HEART_SDA_READ())
        {
            data++;
        }
        HEART_SCL_L();
        HEART_I2C_Delay();
    }

    if (ack)
    {
        HEART_I2C_Ack();
    }
    else
    {
        HEART_I2C_NAck();
    }

    return data;
}

static uint8_t HEART_I2C_WriteByte(uint8_t addr, uint8_t reg, uint8_t data)
{
    uint8_t res = 0;

    HEART_I2C_Start();
    HEART_I2C_SendByte(addr);
    res |= HEART_I2C_WaitAck();
    HEART_I2C_SendByte(reg);
    res |= HEART_I2C_WaitAck();
    HEART_I2C_SendByte(data);
    res |= HEART_I2C_WaitAck();
    HEART_I2C_Stop();

    return res;
}

static uint8_t HEART_I2C_ReadBytes(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf)
{
    uint8_t i;
    uint8_t res = 0;

    HEART_I2C_Start();
    HEART_I2C_SendByte(addr);
    res |= HEART_I2C_WaitAck();
    HEART_I2C_SendByte(reg);
    res |= HEART_I2C_WaitAck();

    HEART_I2C_Start();
    HEART_I2C_SendByte(addr | 0x01);
    res |= HEART_I2C_WaitAck();

    for (i = 0; i < len; i++)
    {
        buf[i] = HEART_I2C_ReadByte((i + 1U) < len);
    }

    HEART_I2C_Stop();

    return res;
}

static uint8_t MAX30102_WriteReg(uint8_t reg, uint8_t data)
{
    return HEART_I2C_WriteByte(MAX30102_ADDR, reg, data); /* 向MAX30102指定寄存器写入1字节。 */
}

static uint8_t MAX30102_ReadReg(uint8_t reg, uint8_t *data)
{
    return HEART_I2C_ReadBytes(MAX30102_ADDR, reg, 1, data); /* 从MAX30102指定寄存器读取1字节。 */
}

uint8_t MAX30102_ReadID(void)
{
    uint8_t id = 0;                             /* 保存读取到的芯片ID。 */
    (void)MAX30102_ReadReg(MAX30102_REG_PART_ID, &id); /* 读取MAX30102 PART_ID寄存器。 */
    return id;                                  /* 返回芯片ID，用于判断模块是否在线。 */
}

uint8_t MAX30102_Init(void)
{
    uint8_t id;                                 /* 保存MAX30102芯片ID。 */

    HEART_I2C_GPIO_Init();                      /* 初始化模拟I2C引脚。 */

    MAX30102_WriteReg(MAX30102_REG_MODE_CONFIG, 0x40); /* 软件复位MAX30102。 */
    rt_thread_mdelay(100);                      /* 等待复位完成。 */

    id = MAX30102_ReadID();
    rt_kprintf("MAX30102 ID: 0x%02X\r\n", id);

    MAX30102_WriteReg(MAX30102_REG_INTR_STATUS_1, 0x00);
    MAX30102_WriteReg(MAX30102_REG_FIFO_WR_PTR, 0x00);
    MAX30102_WriteReg(MAX30102_REG_OVF_COUNTER, 0x00);
    MAX30102_WriteReg(MAX30102_REG_FIFO_RD_PTR, 0x00);
    MAX30102_WriteReg(MAX30102_REG_FIFO_CONFIG, 0x5F);
    MAX30102_WriteReg(MAX30102_REG_MODE_CONFIG, 0x03);
    MAX30102_WriteReg(MAX30102_REG_SPO2_CONFIG, 0x67);
    MAX30102_WriteReg(MAX30102_REG_LED1_PA, 0x7F);
    MAX30102_WriteReg(MAX30102_REG_LED2_PA, 0x7F);
    MAX30102_WriteReg(MAX30102_REG_PILOT_PA, 0x7F);

    return (id == 0x15) ? 0 : 1;                /* ID为0x15表示初始化成功，否则返回错误。 */
}

uint8_t MAX30102_ReadFIFO(uint32_t *red, uint32_t *ir)
{
    uint8_t buf[6];                             /* FIFO一次读取6字节：红光3字节、红外3字节。 */
    uint8_t res;                                /* 保存I2C读取结果。 */

    res = HEART_I2C_ReadBytes(MAX30102_ADDR, MAX30102_REG_FIFO_DATA, 6, buf);
    if (res != 0)
    {
        return res;                             /* I2C读取失败，直接返回错误码。 */
    }

    *red = (((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2]) & 0x03FFFF; /* 拼接18位红光数据。 */
    *ir = (((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 8) | buf[5]) & 0x03FFFF;  /* 拼接18位红外数据。 */

    return 0;
}

uint8_t MAX30102_UpdateHeartRate(uint32_t ir, uint16_t *heart_rate)
{
    static uint32_t dc = 0;                     /* IR信号直流分量，用于去除基础光强。 */
    static int32_t ac_abs_avg = 0;              /* AC分量平均幅度，用于动态阈值。 */
    static uint32_t last_beat_tick = 0;         /* 上一次检测到心跳峰值的系统tick。 */
    static uint8_t was_above = 0;               /* 上一次是否已经处于阈值上方，避免同一峰重复计数。 */
    static uint16_t bpm_filtered = 0;           /* 滤波后的BPM结果。 */
    int32_t ac;                                 /* 去直流后的交流分量。 */
    int32_t abs_ac;                             /* AC分量绝对值。 */
    int32_t threshold;                          /* 根据波动幅度计算出的动态检测阈值。 */
    uint32_t tick;                              /* 当前系统tick。 */
    uint32_t interval;                          /* 两次心跳峰之间的tick间隔。 */
    uint16_t bpm;                               /* 本次根据间隔计算出的瞬时BPM。 */

    if ((ir < 2000U) || (ir > 250000U))
    {
        dc = 0;
        ac_abs_avg = 0;
        last_beat_tick = 0;
        was_above = 0;
        bpm_filtered = 0;
        *heart_rate = 0;                        /* 信号无效时心率清零。 */
        return 0;                               /* 返回0表示本次没有得到新的有效心率。 */
    }

    if (dc == 0)
    {
        dc = ir;                                /* 第一次采样先建立直流基线。 */
        return 0;                               /* 基线建立阶段不输出心率。 */
    }

    dc = (dc * 31U + ir) / 32U;                 /* 低通滤波更新直流分量。 */
    ac = (int32_t)ir - (int32_t)dc;             /* 当前IR减去直流分量，得到脉搏波AC分量。 */
    abs_ac = (ac >= 0) ? ac : -ac;              /* 计算AC绝对值。 */
    ac_abs_avg = (ac_abs_avg * 15 + abs_ac) / 16; /* 平滑AC幅度，用作动态阈值参考。 */

    threshold = ac_abs_avg / 2;
    if (threshold < 80)
    {
        threshold = 80;                         /* 阈值下限，避免微小噪声误判。 */
    }
    else if (threshold > 3000)
    {
        threshold = 3000;                       /* 阈值上限，避免幅度异常导致无法触发。 */
    }

    if ((was_above == 0) && (ac > threshold))
    {
        was_above = 1;                          /* 标记已经越过阈值，等待回落后才能再次触发。 */
        tick = rt_tick_get();                   /* 记录当前峰值时间。 */

        if (last_beat_tick != 0)
        {
            interval = tick - last_beat_tick;
            if ((interval > RT_TICK_PER_SECOND / 3U) && (interval < RT_TICK_PER_SECOND * 2U))
            {
                bpm = (uint16_t)((60U * RT_TICK_PER_SECOND) / interval); /* 根据心跳间隔换算BPM。 */
                if (bpm_filtered == 0)
                {
                    bpm_filtered = bpm;         /* 第一次有效BPM直接作为滤波初值。 */
                }
                else
                {
                    bpm_filtered = (uint16_t)((bpm_filtered * 3U + bpm) / 4U); /* 后续BPM做简单滑动滤波。 */
                }

                *heart_rate = bpm_filtered;     /* 输出滤波后的心率。 */
                last_beat_tick = tick;          /* 更新最近一次心跳时间。 */
                return 1;                       /* 返回1表示本次得到新的有效心率。 */
            }
        }

        last_beat_tick = tick;                  /* 第一次峰值只记录时间，不计算BPM。 */
    }
    else if (ac < (threshold / 2))
    {
        was_above = 0;                          /* 信号回落到阈值一半以下，允许下一次峰值触发。 */
    }

    if ((last_beat_tick != 0) && ((rt_tick_get() - last_beat_tick) > RT_TICK_PER_SECOND * 3U))
    {
        bpm_filtered = 0;                       /* 超过3秒无心跳峰，清除滤波心率。 */
        *heart_rate = 0;                        /* 输出心率清零，表示暂时无有效心率。 */
    }

    return 0;
}
