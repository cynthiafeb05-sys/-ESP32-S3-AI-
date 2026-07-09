#ifndef __MQ135_H
#define __MQ135_H

#include "stm32f10x.h"

// 函数声明
void MQ135_Init(void);
uint16_t MQ135_GetADCValue(void);
float MQ135_GetVoltage(void);
uint16_t MQ135_GetAirQuality(void);
void MQ135_DisplayInfo(uint8_t line);

#endif
