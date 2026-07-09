#include "stm32f10x.h"
#include <rtthread.h>

/* I2C 总线互斥量 */
static struct rt_mutex i2c_bus_lock;

/**
  * @brief  I2C1 初始化
  */
void I2C1_Init(void)
{
    static uint8_t is_init = 0; 
    if(is_init) return; // 保证互斥锁和硬件只初始化一次

    GPIO_InitTypeDef GPIO_InitStructure;
    I2C_InitTypeDef  I2C_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    // 硬件复位 I2C，清除之前可能存在的错误状态
    I2C_DeInit(I2C1); 
    I2C_SoftwareResetCmd(I2C1, ENABLE);
    I2C_SoftwareResetCmd(I2C1, DISABLE);

    // PB6 SCL, PB7 SDA
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD; // 必须开漏
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStructure.I2C_ClockSpeed = 400000;
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStructure.I2C_OwnAddress1 = 0x00;
    I2C_Init(I2C1, &I2C_InitStructure);

    I2C_Cmd(I2C1, ENABLE);
    
    // 只要初始化一次锁即可
    rt_mutex_init(&i2c_bus_lock, "i2c_bus_lock", RT_IPC_FLAG_PRIO);
    is_init = 1;
}
/**
  * @brief  内部超时等待宏
  * @note   防止总线挂死导致线程死循环
  */
#define I2C_TIMEOUT_MAX 1000
static uint8_t Wait_Event(I2C_TypeDef* I2Cx, uint32_t I2C_EVENT)
{
    uint32_t timeout = I2C_TIMEOUT_MAX;
    while(!I2C_CheckEvent(I2Cx, I2C_EVENT))
    {
        if((timeout--) == 0) return 1; // 超时失败
    }
    return 0; // 成功
}

/**
  * @brief  I2C 起始信号
  */
static uint8_t I2C_Start(void)
{
    uint32_t timeout = I2C_TIMEOUT_MAX;
    while(I2C_GetFlagStatus(I2C1, I2C_FLAG_BUSY))
    {
        if((timeout--) == 0) return 1;
    }
    I2C_GenerateSTART(I2C1, ENABLE);
    return Wait_Event(I2C1, I2C_EVENT_MASTER_MODE_SELECT);
}

/**
  * @brief  I2C 写字节（带互斥锁）
  */
uint8_t I2C_WriteByte(uint8_t addr, uint8_t reg, uint8_t data)
{
    uint8_t res = 0;

    /* 1. 获取总线锁 */
    rt_mutex_take(&i2c_bus_lock, RT_WAITING_FOREVER);

    /* 2. 执行 I2C 序列 */
    if(I2C_Start()) { res = 1; goto exit; }

    I2C_Send7bitAddress(I2C1, addr, I2C_Direction_Transmitter);
    if(Wait_Event(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { res = 2; goto exit; }

    I2C_SendData(I2C1, reg);
    if(Wait_Event(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { res = 3; goto exit; }

    I2C_SendData(I2C1, data);
    if(Wait_Event(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { res = 4; goto exit; }

exit:
    I2C_GenerateSTOP(I2C1, ENABLE);
    /* 3. 释放总线锁 */
    rt_mutex_release(&i2c_bus_lock);
    return res;
}

/**
  * @brief  I2C 多字节写（带互斥锁）
  */
uint8_t I2C_BufferWrite(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf)
{
    uint8_t res = 0;
    rt_mutex_take(&i2c_bus_lock, RT_WAITING_FOREVER);

    if(I2C_Start()) { res = 1; goto exit; }

    I2C_Send7bitAddress(I2C1, addr, I2C_Direction_Transmitter);
    if(Wait_Event(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { res = 2; goto exit; }

    I2C_SendData(I2C1, reg);
    if(Wait_Event(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { res = 3; goto exit; }

    for(uint8_t i = 0; i < len; i++)
    {
        I2C_SendData(I2C1, buf[i]);
        if(Wait_Event(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { res = 4; goto exit; }
    }

exit:
    I2C_GenerateSTOP(I2C1, ENABLE);
    rt_mutex_release(&i2c_bus_lock);
    return res;
}

/**
  * @brief  I2C 多字节读（带互斥锁）
  */
uint8_t I2C_BufferRead(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf)
{
    uint8_t res = 0;
    rt_mutex_take(&i2c_bus_lock, RT_WAITING_FOREVER);

    /* 写地址阶段 */
    if(I2C_Start()) { res = 1; goto exit; }

    I2C_Send7bitAddress(I2C1, addr, I2C_Direction_Transmitter);
    if(Wait_Event(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { res = 2; goto exit; }

    I2C_SendData(I2C1, reg);
    if(Wait_Event(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { res = 3; goto exit; }

    /* 读数据阶段 (Re-start) */
    I2C_GenerateSTART(I2C1, ENABLE);
    if(Wait_Event(I2C1, I2C_EVENT_MASTER_MODE_SELECT)) { res = 4; goto exit; }

    I2C_Send7bitAddress(I2C1, addr, I2C_Direction_Receiver);
    if(Wait_Event(I2C1, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) { res = 5; goto exit; }

    for(uint8_t i = 0; i < len; i++)
    {
        if(i == len - 1)
            I2C_AcknowledgeConfig(I2C1, DISABLE); // 最后一个字节 NACK

        if(Wait_Event(I2C1, I2C_EVENT_MASTER_BYTE_RECEIVED)) { res = 6; goto exit; }
        buf[i] = I2C_ReceiveData(I2C1);
    }

exit:
    I2C_GenerateSTOP(I2C1, ENABLE);
    I2C_AcknowledgeConfig(I2C1, ENABLE); // 恢复 ACK 配置
    rt_mutex_release(&i2c_bus_lock);
    return res;
}
