#ifndef LIGHT_H
#define LIGHT_H

#include "stm32f10x.h"

// º¯ʽɹ÷
void LightSensor_ADC_Init(void);  // ³õʼ»¯PA2ΪADCʤȫ
void LightLED_Init(void);         // ³õʼ»¯PA3ΪLEDʤ³ö
float LightSensor_GetIntensity(void);  // »񈡹⇿ֵ
void LightSensor_ControlLED(void);     // ¸ù¾ݹ⇿¿ؖƌED

#endif

