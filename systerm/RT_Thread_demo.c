#include "stm32f10x.h" 
#include "RT_Thread_demo.h"
#include <rtthread.h>
#include "delay.h"
#include "uart.h"

#define TASK1_PRIO      1                        /* 任务优先级 */
#define TASK1_STK_SIZE  128                       /* 任务堆栈大小 */
static rt_thread_t      Task1_Handler = RT_NULL;  /* 任务句柄 */
void Task1(void *pvParameters);             /* 任务函数 */

#define TASK2_PRIO      2                        /* 任务优先级 */
#define TASK2_STK_SIZE  128                       /* 任务堆栈大小 */
static rt_thread_t      Task2_Handler = RT_NULL;  /* 任务句柄 */
void Task2(void *pvParameters);             /* 任务函数 */

#define TASK3_PRIO      3                        /* 任务优先级 */
#define TASK3_STK_SIZE  128                       /* 任务堆栈大小 */
static rt_thread_t      Task3_Handler = RT_NULL;  /* 任务句柄 */
void Task3(void *pvParameters);             /* 任务函数 */

void ADC_Task(void *parameter)
{
    uint16_t value;

    ADC1_Init();  // 初始化一次

    while (1)
    {
        value = ADC_GetValue(ADC1, 4);

        rt_kprintf("ADC: %d\r\n", value);

        rt_thread_mdelay(500);
    }
}

void Task2(void *parameter)
{
	while(1)
	{
		LED2_Toggle;
		//printf("Task2\r\n");
		USART_SendString(USART1,(unsigned char*)"Task2\r\n");
		rt_thread_mdelay(600);		
	}
	rt_thread_delay(10);
}

void Task3(void *parameter)
{
	while(1)
	{
		LED3_Toggle;
		//printf("Task3\r\n");
		USART_SendString(USART1,(unsigned char*)"Task3\r\n");
		rt_thread_mdelay(900);		
	}
	rt_thread_delay(10);
}
