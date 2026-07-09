#ifndef __BSP_TIM_WS2813_H
#define __BSP_TIM_WS2813_H

#include "stm32f10x.h"

#define LED_NUM     60    // LED数量
#define RGB_BIT     24    // 每个LED 24bit，顺序 G R B

#define RESET_WORD  10
#define DUMMY_WORD  10

#define TIMING_0    22
#define TIMING_1    68

/* GPIO */
#define WS2813_RCC_CLK_ENABLE                RCC_APB2PeriphClockCmd
#define WS2813_RCC_CLK_PORT                  RCC_APB2Periph_GPIOB
#define WS2813_GPIO_PIN                      GPIO_Pin_1
#define WS2813_GPIO_PORT                     GPIOB

/* TIM */
#define WS2813_TIM_RCC_CLK_ENABLE            RCC_APB1PeriphClockCmd
#define WS2813_TIM_RCC_CLK_PORT              RCC_APB1Periph_TIM3
#define WS2813_TIMX                          TIM3
#define TIM_OCXInit                          TIM_OC4Init
#define TIM_OCXPreloadConfig                 TIM_OC4PreloadConfig

/* DMA */
#define WS2813_DMA_RCC_CLK_ENABLE            RCC_AHBPeriphClockCmd
#define WS2813_DMA_RCC_CLK_PORT              RCC_AHBPeriph_DMA1
#define WS2813_DMA_CHANNELX                  DMA1_Channel3
#define TIMX_CCR1_Address                    (uint32_t)&TIM3->CCR4
#define DMAX_FLAG_TCX                        DMA1_FLAG_TC3
#define TIM_DMA_CCX                          TIM_DMA_CC4

extern uint8_t rgb_buf[LED_NUM][3];

void WS2813_Init(void);
void WS2813_Display(uint8_t (*led_buf)[3], uint8_t led_num);

void WS2813_AllOn(void);
void WS2813_AllOff(void);

void WS2813_RainbowBreatheStep(void);
void WS2813_RainbowBrightness(uint8_t brightness);
void WS2813_RainbowBreathe(uint8_t led_num, uint16_t delay_ms);

#endif
