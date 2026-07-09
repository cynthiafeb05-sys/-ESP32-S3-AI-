#ifndef __FAN_H
#define __FAN_H

#include "stm32f10x.h"  // 包含STM32核心头文件

// 风扇引脚/端口宏定义
#define FAN1_PIN    GPIO_Pin_7
#define FAN1_PORT    GPIOA
#define FAN2_PIN    GPIO_Pin_5
#define FAN2_PORT    GPIOA

#define FAN_ON   1
#define FAN_OFF  0

// 函数声明
void FAN_GPIO_Init(void);               // 风扇GPIO初始化
void FAN_Control(uint8_t state);        // 风扇启停控制
void FAN_SetSpeed(uint8_t percent);     // 风扇软件PWM占空比，0~100
void FAN_TestOutput(uint8_t fan_no, uint8_t state); // 单独测试风扇输出
void FAN_SetAutoMode(void);             // 退出手动测试，恢复PID/PWM自动控制

#endif


