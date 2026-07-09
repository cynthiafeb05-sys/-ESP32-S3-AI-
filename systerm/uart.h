#ifndef __UART_H__
#define __UART_H__

#include "stm32f10x.h"

// 调试串口配置
#define DEBUG_USARTx                   USART1
#define DEBUG_USART_CLK                RCC_APB2Periph_USART1
#define DEBUG_USART_APBxClkCmd         RCC_APB2PeriphClockCmd
#define DEBUG_USART_BAUDRATE           115200

// USART GPIO 引脚定义
#define DEBUG_USART_TX_GPIO_PORT       GPIOA
#define DEBUG_USART_TX_GPIO_PIN        GPIO_Pin_9
#define DEBUG_USART_RX_GPIO_PORT       GPIOA
#define DEBUG_USART_RX_GPIO_PIN        GPIO_Pin_10

// 合并GPIO时钟定义，同时包含TX和RX的GPIO时钟
#define DEBUG_USART_GPIO_CLK           (RCC_APB2Periph_GPIOA)
#define DEBUG_USART_GPIO_APBxClkCmd    RCC_APB2PeriphClockCmd

// 中断定义
#define DEBUG_USART_IRQ                USART1_IRQn
#define DEBUG_USART_IRQHandler         USART1_IRQHandler

// 函数声明
// uart.h
void uart1_init(uint32_t bound); // 统一使用这个
void UsartSendByte(USART_TypeDef* pUSARTx, uint8_t data);
void UsartSendString(USART_TypeDef* USARTx, char *str);
void USART_SendString(USART_TypeDef* USARTx, unsigned char *str);

/*
 * 串口命令接收接口：
 * Uart1_ReadLine 供上层线程轮询，有完整命令行时返回1并把内容写入buf，否则返回0。
 */
uint8_t Uart1_ReadLine(char *buf, uint16_t buf_size);

#endif /* __UART_H__ */

