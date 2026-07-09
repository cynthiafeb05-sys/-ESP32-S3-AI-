#include "dht11.h"
#include "Delay.h"
#include <rtthread.h>

/*
 * DHT11温湿度模块说明：
 * 1. DHT11采用单总线通信，对时序要求比较严格；
 * 2. 读取数据时会进入临界区，避免OLED等线程打断80us级握手时序；
 * 3. 成功读取后返回温度整数/小数、湿度整数/小数，主线程再用于风扇控制。
 */

static void DHT11_Mode_IPU(void);                         /* 将DHT11数据引脚切换为上拉输入。 */
static void DHT11_Mode_OUT(void);                         /* 将DHT11数据引脚切换为推挽输出。 */
static uint8_t DHT11_WaitLevel(uint8_t level, uint32_t timeout_us); /* 等待总线变为指定电平，带超时保护。 */

void DHT11_GPIO_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;                  /* GPIO配置结构体。 */

    RCC_APB2PeriphClockCmd(DHT11_GPIO_CLK, ENABLE);       /* 打开DHT11数据引脚所在GPIO端口时钟。 */

    GPIO_InitStructure.GPIO_Pin = DHT11_GPIO_PIN;         /* 选择DHT11数据引脚。 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;      /* 初始配置为推挽输出，方便拉高总线。 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;     /* 设置GPIO输出速度。 */
    GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStructure);      /* 写入GPIO配置。 */

    DHT11_HIGH;                                           /* 空闲状态下DHT11总线保持高电平。 */
}

static void DHT11_Mode_IPU(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;                  /* GPIO配置结构体。 */

    GPIO_InitStructure.GPIO_Pin = DHT11_GPIO_PIN;         /* 选择DHT11数据引脚。 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;         /* 配置为上拉输入，用来读取DHT11返回的数据。 */
    GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStructure);      /* 写入输入模式配置。 */
}

static void DHT11_Mode_OUT(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;                  /* GPIO配置结构体。 */

    GPIO_InitStructure.GPIO_Pin = DHT11_GPIO_PIN;         /* 选择DHT11数据引脚。 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;      /* 配置为推挽输出，用来产生起始信号。 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;     /* 设置GPIO输出速度。 */
    GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStructure);      /* 写入输出模式配置。 */
}

static uint8_t DHT11_WaitLevel(uint8_t level, uint32_t timeout_us)
{
    while (timeout_us--)
    {
        if (DHT11_READ_DATA == level)
        {
            return 0;                                     /* 等到目标电平，返回0表示成功。 */
        }
        Delay_us(1);                                      /* 每次等待1us，形成微秒级超时计数。 */
    }
    return 1;                                             /* 超时仍未等到目标电平，返回1表示失败。 */
}

uint8_t DHT11_ReadData(DHT11_Data_TypeDef *data)
{
    uint8_t buf[5] = {0};                                 /* DHT11一次返回5字节：湿度高/低、温度高/低、校验。 */
    uint8_t i, j;                                         /* i遍历5个字节，j遍历每个字节的8位。 */

    DHT11_Mode_OUT();                                     /* 主机先切换为输出模式，准备发送起始信号。 */
    DHT11_LOW;                                            /* 主机拉低总线，通知DHT11开始一次采样传输。 */
    rt_thread_mdelay(20);                                 /* 起始低电平保持至少18ms，这里取20ms。 */
    DHT11_HIGH;                                           /* 主机释放总线前先拉高。 */
    Delay_us(30);                                         /* 等待20~40us，给DHT11准备响应的时间。 */

    DHT11_Mode_IPU();                                     /* 切换为输入模式，开始读取DHT11响应和数据。 */

    rt_enter_critical();                                  /* 进入临界区，避免读取时序被线程调度打断。 */

    if (DHT11_READ_DATA == 0)
    {
        if (DHT11_WaitLevel(1, 120) != 0)
        {
            rt_exit_critical();                           /* 响应低电平结束超时，退出临界区。 */
            return 3;                                     /* 返回3表示时序等待失败。 */
        }
        if (DHT11_WaitLevel(0, 120) != 0)
        {
            rt_exit_critical();                           /* 响应高电平结束超时，退出临界区。 */
            return 3;                                     /* 返回3表示时序等待失败。 */
        }

        for (i = 0; i < 5; i++)
        {
            for (j = 0; j < 8; j++)
            {
                if (DHT11_WaitLevel(1, 100) != 0)
                {
                    rt_exit_critical();                   /* 等待每一位开始的低电平结束失败。 */
                    return 3;                             /* 返回3表示时序等待失败。 */
                }

                Delay_us(40);                             /* 延时40us后读电平：高电平仍存在则该位为1。 */
                buf[i] <<= 1;                             /* 给当前字节左移一位，准备写入新bit。 */
                if (DHT11_READ_DATA == 1)
                {
                    buf[i] |= 1;                          /* 40us后仍为高电平，判断当前bit为1。 */
                    if (DHT11_WaitLevel(0, 100) != 0)
                    {
                        rt_exit_critical();               /* 等待该位高电平结束失败。 */
                        return 3;                         /* 返回3表示时序等待失败。 */
                    }
                }
            }
        }
        rt_exit_critical();                               /* 5字节读取完成，退出临界区恢复调度。 */

        if (buf[4] == (buf[0] + buf[1] + buf[2] + buf[3]))
        {
            data->humi_int  = buf[0];                     /* 湿度整数部分。 */
            data->humi_deci = buf[1];                     /* 湿度小数部分。 */
            data->temp_int  = buf[2];                     /* 温度整数部分。 */
            data->temp_deci = buf[3];                     /* 温度小数部分。 */
            return 0;                                     /* 校验成功，返回0表示读取成功。 */
        }
        return 2;                                         /* 校验失败，返回2。 */
    }

    rt_exit_critical();                                   /* DHT11未响应，也要退出临界区。 */
    return 1;                                             /* 返回1表示传感器未响应。 */
}
