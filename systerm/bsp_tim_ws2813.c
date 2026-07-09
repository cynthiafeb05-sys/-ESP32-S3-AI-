#include "bsp_tim_ws2813.h"
#include "delay.h"
#include "stdlib.h"
#include "string.h"

/*
 * WS2813灯带模块说明：
 * 1. 使用定时器PWM产生WS2813需要的高低电平时序；
 * 2. 使用DMA把DMABuffer里的PWM占空比连续搬运到定时器CCR寄存器；
 * 3. rgb_buf保存每颗灯的RGB值，WS2813_Display负责转换成GRB时序并发送。
 */

uint16_t DMABuffer[RESET_WORD + RGB_BIT * LED_NUM + DUMMY_WORD]; /* DMA发送缓冲区：包含复位码、RGB数据位和尾部空码。 */
uint8_t rgb_buf[LED_NUM][3];                                     /* 灯带颜色缓冲区，每颗灯保存R/G/B三个通道。 */

void WS2813_TIM_Mode_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;                 /* WS2813输出引脚GPIO配置结构体。 */
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;       /* 定时器基础计数配置结构体。 */
    TIM_OCInitTypeDef TIM_OCInitStructure;               /* 定时器PWM输出比较配置结构体。 */

    WS2813_RCC_CLK_ENABLE(WS2813_RCC_CLK_PORT, ENABLE);

    GPIO_InitStructure.GPIO_Pin = WS2813_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(WS2813_GPIO_PORT, &GPIO_InitStructure);
    GPIO_ResetBits(WS2813_GPIO_PORT, WS2813_GPIO_PIN);

    WS2813_TIM_RCC_CLK_ENABLE(WS2813_TIM_RCC_CLK_PORT, ENABLE);

    TIM_TimeBaseStructure.TIM_Period = 90 - 1;           /* 定时器周期设置为90个计数，用于匹配WS2813单bit时间。 */
    TIM_TimeBaseStructure.TIM_Prescaler = 0;             /* 不分频，使用定时器原始时钟。 */
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(WS2813_TIMX, &TIM_TimeBaseStructure);

    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;    /* 使用PWM1模式输出不同占空比。 */
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 0;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_OCIdleState = TIM_OCIdleState_Reset;
    TIM_OCXInit(WS2813_TIMX, &TIM_OCInitStructure);

    TIM_OCXPreloadConfig(WS2813_TIMX, TIM_OCPreload_Enable);

    TIM_Cmd(WS2813_TIMX, ENABLE);
}

void WS2813_TIM_DMA_Config(void)
{
    DMA_InitTypeDef DMA_InitStructure;                   /* DMA初始化结构体。 */

    WS2813_DMA_RCC_CLK_ENABLE(WS2813_DMA_RCC_CLK_PORT, ENABLE);

    memset((uint8_t *)&DMABuffer, 0, sizeof(DMABuffer)); /* 清空DMA缓冲区，避免上电时输出随机颜色。 */

    DMA_DeInit(WS2813_DMA_CHANNELX);

    DMA_InitStructure.DMA_PeripheralBaseAddr = TIMX_CCR1_Address; /* DMA目标地址为定时器CCR寄存器。 */
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)&DMABuffer;  /* DMA源地址为灯带时序缓冲区。 */
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = sizeof(DMABuffer) / 2;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;

    DMA_Init(WS2813_DMA_CHANNELX, &DMA_InitStructure);

    TIM_DMACmd(WS2813_TIMX, TIM_DMA_CCX, ENABLE);
}

void WS2813_DMA_Send(void)
{
    DMA_SetCurrDataCounter(WS2813_DMA_CHANNELX, sizeof(DMABuffer) / 2); /* 设置本次DMA需要发送的半字数量。 */
    DMA_Cmd(WS2813_DMA_CHANNELX, ENABLE);                              /* 使能DMA通道，准备开始搬运数据。 */
    TIM_Cmd(WS2813_TIMX, ENABLE);                                      /* 使能定时器，开始产生PWM时序。 */

    while (!DMA_GetFlagStatus(DMAX_FLAG_TCX))
    {
    }

    DMA_Cmd(WS2813_DMA_CHANNELX, DISABLE);             /* DMA发送完成后关闭DMA通道。 */
    DMA_ClearFlag(DMAX_FLAG_TCX);                       /* 清除DMA传输完成标志。 */
    TIM_Cmd(WS2813_TIMX, DISABLE);                      /* 关闭定时器，等待下一次发送。 */
}

void WS2813_Display(uint8_t (*led_buf)[3], uint8_t led_num)
{
    uint8_t i, j;                                       /* i遍历灯珠，j遍历每个颜色通道的8位。 */

    for (i = 0; i < led_num; i++)
    {
        for (j = 0; j < 8; j++)
        {
            DMABuffer[RESET_WORD + RGB_BIT * i + j] = ((led_buf[i][1] << j) & 0x80) ? TIMING_1 : TIMING_0;      /* WS2813先发G通道。 */
            DMABuffer[RESET_WORD + RGB_BIT * i + j + 8] = ((led_buf[i][0] << j) & 0x80) ? TIMING_1 : TIMING_0;  /* 再发R通道。 */
            DMABuffer[RESET_WORD + RGB_BIT * i + j + 16] = ((led_buf[i][2] << j) & 0x80) ? TIMING_1 : TIMING_0; /* 最后发B通道。 */
        }
    }

    WS2813_DMA_Send();                                  /* 将转换好的PWM时序通过DMA一次性发给灯带。 */
}

void WS2813_Init(void)
{
    WS2813_TIM_Mode_Config();                           /* 初始化灯带定时器PWM输出。 */
    WS2813_TIM_DMA_Config();                            /* 初始化灯带DMA发送通道。 */
}

void WS2813_RandomBright(void)
{
    uint8_t i, j;
    for (i = 0; i < 10; i++)
    {
        for (j = 0; j < LED_NUM; j++)
        {
            rgb_buf[j][0] = rand() % 256;
            rgb_buf[j][1] = rand() % 256;
            rgb_buf[j][2] = rand() % 256;
        }
        WS2813_Display(rgb_buf, LED_NUM);
        Delay_ms(500);
    }
}

void WS2813_RedBreathe(void)
{
    uint16_t i, j, k;
    for (i = 0; i < 256; i++)
    {
        k = i;
        for (j = 0; j < LED_NUM; j++)
        {
            rgb_buf[j][0] = (uint8_t)k;
            rgb_buf[j][1] = 0;
            rgb_buf[j][2] = 0;
        }
        WS2813_Display(rgb_buf, LED_NUM);
        Delay_ms(5);
    }
    for (i = 0; i < 256; i++)
    {
        k = (uint16_t)(255 - i);
        for (j = 0; j < LED_NUM; j++)
        {
            rgb_buf[j][0] = (uint8_t)k;
            rgb_buf[j][1] = 0;
            rgb_buf[j][2] = 0;
        }
        WS2813_Display(rgb_buf, LED_NUM);
        Delay_ms(5);
    }
}

void WS2813_GreenBreathe(void)
{
    uint16_t i, j, k;
    for (i = 0; i < 256; i++)
    {
        k = i;
        for (j = 0; j < LED_NUM; j++)
        {
            rgb_buf[j][0] = 0;
            rgb_buf[j][1] = (uint8_t)k;
            rgb_buf[j][2] = 0;
        }
        WS2813_Display(rgb_buf, LED_NUM);
        Delay_ms(5);
    }
    for (i = 0; i < 256; i++)
    {
        k = (uint16_t)(255 - i);
        for (j = 0; j < LED_NUM; j++)
        {
            rgb_buf[j][0] = 0;
            rgb_buf[j][1] = (uint8_t)k;
            rgb_buf[j][2] = 0;
        }
        WS2813_Display(rgb_buf, LED_NUM);
        Delay_ms(5);
    }
}

void WS2813_BlueBreathe(void)
{
    uint16_t i, j, k;
    for (i = 0; i < 256; i++)
    {
        k = i;
        for (j = 0; j < LED_NUM; j++)
        {
            rgb_buf[j][0] = 0;
            rgb_buf[j][1] = 0;
            rgb_buf[j][2] = (uint8_t)k;
        }
        WS2813_Display(rgb_buf, LED_NUM);
        Delay_ms(5);
    }
    for (i = 0; i < 256; i++)
    {
        k = (uint16_t)(255 - i);
        for (j = 0; j < LED_NUM; j++)
        {
            rgb_buf[j][0] = 0;
            rgb_buf[j][1] = 0;
            rgb_buf[j][2] = (uint8_t)k;
        }
        WS2813_Display(rgb_buf, LED_NUM);
        Delay_ms(5);
    }
}

static uint8_t WS2813_Scale8(uint8_t value, uint8_t scale)
{
    return (uint8_t)(((uint16_t)value * scale) / 255U);  /* 按0~255亮度比例缩放单个颜色通道。 */
}

static void WS2813_ColorWheel(uint8_t pos, uint8_t brightness, uint8_t *red, uint8_t *green, uint8_t *blue)
{
    /* 根据0~255的位置生成彩虹色，再按照brightness整体缩放亮度。 */
    if (pos < 85U)
    {
        *red = (uint8_t)(255U - pos * 3U);
        *green = (uint8_t)(pos * 3U);
        *blue = 0;
    }
    else if (pos < 170U)
    {
        pos = (uint8_t)(pos - 85U);
        *red = 0;
        *green = (uint8_t)(255U - pos * 3U);
        *blue = (uint8_t)(pos * 3U);
    }
    else
    {
        pos = (uint8_t)(pos - 170U);
        *red = (uint8_t)(pos * 3U);
        *green = 0;
        *blue = (uint8_t)(255U - pos * 3U);
    }

    *red = WS2813_Scale8(*red, brightness);
    *green = WS2813_Scale8(*green, brightness);
    *blue = WS2813_Scale8(*blue, brightness);
}

void WS2813_RainbowBreatheStep(void)
{
    static uint16_t phase = 0;                          /* 彩虹呼吸相位，0~511循环。 */
    uint8_t i;                                          /* 遍历灯珠。 */
    uint8_t brightness;                                 /* 当前呼吸亮度。 */
    uint8_t base_color;                                 /* 当前基础彩虹颜色位置。 */

    brightness = (phase < 256U) ? (uint8_t)phase : (uint8_t)(511U - phase);
    base_color = (uint8_t)(phase >> 1);

    for (i = 0; i < LED_NUM; i++)
    {
        uint8_t red;
        uint8_t green;
        uint8_t blue;
        uint8_t pos;

        pos = (uint8_t)(base_color + ((uint16_t)i * 256U) / LED_NUM);
        WS2813_ColorWheel(pos, brightness, &red, &green, &blue);
        rgb_buf[i][0] = red;
        rgb_buf[i][1] = green;
        rgb_buf[i][2] = blue;
    }

    WS2813_Display(rgb_buf, LED_NUM);                   /* 发送本步彩虹呼吸颜色。 */

    phase = (uint16_t)((phase + 8U) & 0x01FFU);         /* 相位递增并限制在0~511。 */
}

void WS2813_RainbowBrightness(uint8_t brightness)
{
    static uint8_t phase = 0;                           /* 彩虹滚动相位。 */
    uint8_t i;                                          /* 遍历灯珠。 */

    if (brightness == 0U)
    {
        WS2813_AllOff();                                /* 亮度为0时直接关闭全部灯珠。 */
        return;                                         /* 不再计算彩虹颜色。 */
    }

    for (i = 0; i < LED_NUM; i++)
    {
        uint8_t red;
        uint8_t green;
        uint8_t blue;
        uint8_t pos;

        pos = (uint8_t)(phase + ((uint16_t)i * 256U) / LED_NUM);
        WS2813_ColorWheel(pos, brightness, &red, &green, &blue);
        rgb_buf[i][0] = red;
        rgb_buf[i][1] = green;
        rgb_buf[i][2] = blue;
    }

    WS2813_Display(rgb_buf, LED_NUM);                   /* 按指定亮度显示彩虹颜色。 */
    phase++;                                            /* 相位递增，让彩虹颜色随时间流动。 */
}

void WS2813_RainbowBreathe(uint8_t led_num, uint16_t delay_ms)
{
    uint16_t step;

    (void)led_num;                                      /* 当前使用固定LED_NUM，保留参数兼容旧接口。 */

    for (step = 0; step < 64U; step++)
    {
        WS2813_RainbowBreatheStep();                    /* 执行一步彩虹呼吸动画。 */
        Delay_ms(delay_ms);                             /* 控制动画速度。 */
    }
}

void WS2813_AllOn(void)
{
    uint8_t i;                                          /* 遍历所有灯珠。 */
    for (i = 0; i < LED_NUM; i++)
    {
        rgb_buf[i][0] = 255;
        rgb_buf[i][1] = 255;
        rgb_buf[i][2] = 255;
    }
    WS2813_Display(rgb_buf, LED_NUM);                   /* 发送全白颜色，实现全亮。 */
}

void WS2813_AllOff(void)
{
    uint8_t i;                                          /* 遍历所有灯珠。 */
    for (i = 0; i < LED_NUM; i++)
    {
        rgb_buf[i][0] = 0;
        rgb_buf[i][1] = 0;
        rgb_buf[i][2] = 0;
    }
    WS2813_Display(rgb_buf, LED_NUM);                   /* 发送全0颜色，实现熄灭。 */
}
