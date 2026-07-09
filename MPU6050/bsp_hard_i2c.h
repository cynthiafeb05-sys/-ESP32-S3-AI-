#ifndef __BSP_HARD_I2C_H
#define __BSP_HARD_I2C_H

#include "stm32f10x.h"   // ? 必须加这个
#include <stdint.h>      // ? 再加这个

void I2C1_Init(void);

uint8_t I2C_WriteByte(uint8_t addr,uint8_t reg,uint8_t data);
uint8_t I2C_BufferWrite(uint8_t addr,uint8_t reg,uint8_t len,uint8_t *buf);
uint8_t I2C_BufferRead(uint8_t addr,uint8_t reg,uint8_t len,uint8_t *buf);

#endif
