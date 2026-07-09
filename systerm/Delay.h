#ifndef __DELAY_H
#define __DELAY_H

#include "stm32f10x.h"

void Delay_Init(void);
void Delay_ms(uint32_t ms);  // 这里必须是 uint32_t
void Delay_us(uint32_t us);

#endif
