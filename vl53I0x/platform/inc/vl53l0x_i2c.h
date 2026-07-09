#ifndef __VL53L0_I2C_H
#define __VL53L0_I2C_H

#include "stm32f10x.h"

void VL53L0X_i2c_init(void);

uint8_t VL53L0X_write_byte(uint8_t address,uint8_t index,uint8_t data);
uint8_t VL53L0X_write_multi(uint8_t address,uint8_t index,uint8_t *pdata,uint16_t count);

uint8_t VL53L0X_read_byte(uint8_t address,uint8_t index,uint8_t *pdata);
uint8_t VL53L0X_read_multi(uint8_t address,uint8_t index,uint8_t *pdata,uint16_t count);

#endif
