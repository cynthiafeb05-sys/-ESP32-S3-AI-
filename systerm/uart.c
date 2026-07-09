#include "uart.h"
#include <stdio.h>
#include <string.h>
#include <rtthread.h>

/*
 * USART1串口模块说明：
 * 1. USART1使用PA9作为TX、PA10作为RX；
 * 2. uart1_init负责配置GPIO、USART参数以及RX接收中断；
 * 3. fputc把printf/rt_kprintf最终输出到USART1，方便调试和中文告警发送；
 * 4. USART1_IRQHandler把收到的字节存入环形缓冲区，遇到换行符标记一行就绪；
 * 5. Uart1_ReadLine供上层线程轮询取出一行完整命令字符串。
 */

/* 接收环形缓冲区，大小需能容纳最长命令（"打开风扇"GBK编码8字节+换行）。 */
#define UART1_RX_BUF_SIZE  64U

static volatile uint8_t  uart1_rx_buf[UART1_RX_BUF_SIZE]; /* 接收原始字节缓冲区。 */
static volatile uint16_t uart1_rx_len  = 0;               /* 当前缓冲区已存字节数。 */
static volatile uint8_t  uart1_rx_ready = 0;              /* 1表示已收到一行完整命令，等待上层读取。 */

void uart1_init(uint32_t bound)
{
    GPIO_InitTypeDef  GPIO_InitStructure;      /* GPIO初始化结构体，用来配置PA9/PA10。 */
    USART_InitTypeDef USART_InitStructure;     /* USART初始化结构体，用来配置波特率、校验位等。 */
    NVIC_InitTypeDef  NVIC_InitStructure;      /* NVIC初始化结构体，用来配置USART1接收中断优先级。 */

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE); /* 打开USART1外设时钟。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,  ENABLE); /* 打开GPIOA时钟，PA9/PA10属于GPIOA。 */

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;            /* PA9配置为USART1_TX。 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;      /* TX引脚输出速度设置为50MHz。 */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;       /* TX使用复用推挽输出模式。 */
    GPIO_Init(GPIOA, &GPIO_InitStructure);                 /* 写入PA9 GPIO配置。 */

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_10;            /* PA10配置为USART1_RX。 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;  /* RX使用浮空输入模式接收串口数据。 */
    GPIO_Init(GPIOA, &GPIO_InitStructure);                 /* 写入PA10 GPIO配置。 */

    USART_InitStructure.USART_BaudRate            = bound;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    /* 使能USART1接收非空中断，每收到一个字节触发一次IRQ。 */
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    /* 配置USART1中断优先级，Group_1下抢占优先级最大为1，子优先级0~7。
     * 设为抢占1、子优先级1，低于ESP8266的USART3（抢占1、子优先级0）。 */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART1, ENABLE);                             /* 使能USART1，开始工作。 */
}

/*
 * USART1接收中断服务函数。
 * 每收到一个字节就存入缓冲区；遇到 '\n' 或 '\r' 时标记一行就绪。
 * 上层线程通过 Uart1_ReadLine 轮询取走命令后清空缓冲区。
 */
void USART1_IRQHandler(void)
{
    uint8_t ch;

    if (USART_GetITStatus(USART1, USART_IT_RXNE) == RESET)
    {
        return;
    }

    ch = (uint8_t)USART_ReceiveData(USART1);
    USART_ClearITPendingBit(USART1, USART_IT_RXNE);

    /* 调试回显：收到字节立刻原样发回，确认中断是否触发。确认后可删除。 */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {}
    USART_SendData(USART1, ch);

    if (uart1_rx_ready)
    {
        return;
    }

    if ((ch == '\n') || (ch == '\r'))
    {
        if (uart1_rx_len > 0U)
        {
            uart1_rx_buf[uart1_rx_len] = '\0';
            uart1_rx_ready = 1U;
        }
    }
    else
    {
        if (uart1_rx_len < (UART1_RX_BUF_SIZE - 1U))
        {
            uart1_rx_buf[uart1_rx_len++] = ch;
        }
    }
}

/*
 * 上层线程调用此函数轮询是否有新命令行。
 * 有则把内容复制到 buf（最多 buf_size-1 字节），清空缓冲区，返回1；
 * 无则返回0。
 */
uint8_t Uart1_ReadLine(char *buf, uint16_t buf_size)
{
    uint16_t copy_len;
    rt_base_t level;

    if (!uart1_rx_ready)
    {
        return 0U;
    }

    copy_len = uart1_rx_len;
    if (copy_len >= buf_size)
    {
        copy_len = buf_size - 1U;
    }
    memcpy(buf, (const void *)uart1_rx_buf, copy_len);
    buf[copy_len] = '\0';

    /* 关中断后再清空缓冲区，防止清空过程中中断写入新数据造成竞争。 */
    level = rt_hw_interrupt_disable();
    uart1_rx_len   = 0U;
    uart1_rx_ready = 0U;
    rt_hw_interrupt_enable(level);

    return 1U;
}

int fputc(int ch, FILE *f)
{
    (void)f;                                               /* 未使用FILE参数，避免编译器警告。 */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET); /* 等待发送数据寄存器为空。 */
    USART_SendData(USART1, (uint8_t)ch);                   /* 发送一个字节到USART1。 */
    return ch;                                             /* 返回已发送字符，满足fputc接口要求。 */
}

void UsartSendByte(USART_TypeDef* pUSARTx, uint8_t data)
{
    while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == RESET); /* 等待指定串口可以发送。 */
    USART_SendData(pUSARTx, data);                                 /* 通过指定串口发送一个字节。 */
}

void UsartSendString(USART_TypeDef* USARTx, char *str)
{
    if (str == 0)
    {
        return;                                            /* 空指针保护，避免访问无效字符串。 */
    }

    while (*str != '\0')
    {
        UsartSendByte(USARTx, (uint8_t)(*str));            /* 逐字节发送字符串当前字符。 */
        str++;                                             /* 指针后移，准备发送下一个字符。 */
    }
}

void USART_SendString(USART_TypeDef* USARTx, unsigned char *str)
{
    UsartSendString(USARTx, (char *)str);                  /* 兼容unsigned char字符串接口，内部复用发送函数。 */
}
