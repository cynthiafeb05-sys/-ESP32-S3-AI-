#ifndef __MAX30102_H
#define __MAX30102_H

#include "stm32f10x.h"
#include <stdint.h>

#define MAX30102_ADDR          (0x57 << 1)

#define MAX30102_REG_INTR_STATUS_1  0x00
#define MAX30102_REG_FIFO_WR_PTR    0x04
#define MAX30102_REG_OVF_COUNTER    0x05
#define MAX30102_REG_FIFO_RD_PTR    0x06
#define MAX30102_REG_FIFO_DATA      0x07
#define MAX30102_REG_FIFO_CONFIG    0x08
#define MAX30102_REG_MODE_CONFIG    0x09
#define MAX30102_REG_SPO2_CONFIG    0x0A
#define MAX30102_REG_LED1_PA        0x0C
#define MAX30102_REG_LED2_PA        0x0D
#define MAX30102_REG_PILOT_PA       0x10
#define MAX30102_REG_PART_ID        0xFF

#define MAX30102_INT_PORT           GPIOB
#define MAX30102_INT_PIN            GPIO_Pin_5
#define MAX30102_SCL_PORT           GPIOB
#define MAX30102_SCL_PIN            GPIO_Pin_14
#define MAX30102_SDA_PORT           GPIOB
#define MAX30102_SDA_PIN            GPIO_Pin_15

uint8_t MAX30102_Init(void);
uint8_t MAX30102_ReadID(void);
uint8_t MAX30102_ReadFIFO(uint32_t *red, uint32_t *ir);
uint8_t MAX30102_UpdateHeartRate(uint32_t ir, uint16_t *heart_rate);

#endif
