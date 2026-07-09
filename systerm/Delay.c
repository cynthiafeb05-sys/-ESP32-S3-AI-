#include "delay.h"
#include <rtthread.h>

/*
 * 延时模块说明：
 * 1. Delay_ms使用RT-Thread线程延时，会让出CPU，适合长延时；
 * 2. Delay_us使用空循环忙等，不会线程切换，适合I2C、DHT11等严格时序；
 * 3. 微秒延时系数按72MHz主频估算，若主频变化需要重新校准。
 */

void Delay_Init(void)
{
    /* RT-Thread Nano已经初始化SysTick，本函数保留为空，兼容旧接口。 */
}

void Delay_ms(uint32_t ms)
{
    rt_thread_mdelay(ms);              /* 毫秒级延时，让当前线程休眠并允许其他线程运行。 */
}

void Delay_us(uint32_t us)
{
    uint32_t cnt = us * 12;            /* 根据72MHz主频粗略换算空循环次数。 */
    while(cnt--);                      /* 忙等待，不发生线程切换，用于微秒级总线时序。 */
}
