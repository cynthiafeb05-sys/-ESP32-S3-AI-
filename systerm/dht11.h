#ifndef __DHT11_H
#define __DHT11_H

#include "stm32f10x.h"

// 引脚定义（根据你的电路修改）
#define DHT11_GPIO_PORT    GPIOA
#define DHT11_GPIO_PIN     GPIO_Pin_6
#define DHT11_GPIO_CLK     RCC_APB2Periph_GPIOA

#define DHT11_HIGH        GPIO_SetBits(DHT11_GPIO_PORT, DHT11_GPIO_PIN)
#define DHT11_LOW         GPIO_ResetBits(DHT11_GPIO_PORT, DHT11_GPIO_PIN)
#define DHT11_READ_DATA   GPIO_ReadInputDataBit(DHT11_GPIO_PORT, DHT11_GPIO_PIN)

typedef struct
{
    uint8_t humi_int;
    uint8_t humi_deci;
    uint8_t temp_int;
    uint8_t temp_deci;
    uint8_t check_sum;
} DHT11_Data_TypeDef;

// 函数声明
void DHT11_GPIO_Config(void);
uint8_t DHT11_ReadData(DHT11_Data_TypeDef *data);

#endif
