#ifndef __ADC_H
#define __ADC_H

#include "stm32f10x.h"

void ADC1_Init(void);
uint16_t ADC_GetValue(ADC_TypeDef* ADCx,uint8_t ch);

#endif
