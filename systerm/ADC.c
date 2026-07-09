#include "adc.h"

/*
 * ADC模块说明：
 * 1. ADC1_Init初始化ADC1和PA2/PA4模拟输入；
 * 2. ADC_GetValue支持按通道读取ADC值，当前用于MQ135和光敏电阻；
 * 3. 为降低通道切换残留影响，每次读取会先丢弃一次采样，再取4次平均。
 */

static uint16_t ADC_ReadOnce(ADC_TypeDef* ADCx, uint8_t ch)
{
    ADC_RegularChannelConfig(ADCx, (uint8_t)ch, 1, ADC_SampleTime_239Cycles5); /* 配置本次要读取的ADC通道。 */
    ADC_SoftwareStartConvCmd(ADCx, ENABLE);                                    /* 软件触发一次ADC转换。 */
    while (!ADC_GetFlagStatus(ADCx, ADC_FLAG_EOC));                            /* 等待转换完成。 */
    return ADC_GetConversionValue(ADCx);                                        /* 返回本次转换结果。 */
}

void ADC1_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;    /* GPIO初始化结构体。 */
    ADC_InitTypeDef ADC_InitStructure;      /* ADC初始化结构体。 */

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE); /* 打开GPIOA和ADC1时钟。 */

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_4; /* PA2用于光敏，PA4用于MQ135。 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;          /* 模拟输入模式。 */
    GPIO_Init(GPIOA,&GPIO_InitStructure);                  /* 写入GPIO配置。 */

    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;     /* ADC1独立工作。 */
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;          /* 单通道读取，不使用扫描模式。 */
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;    /* 单次转换模式，由读取函数主动触发。 */
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None; /* 不使用外部触发。 */
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right; /* ADC结果右对齐。 */
    ADC_InitStructure.ADC_NbrOfChannel = 1;                /* 规则转换序列只包含1个通道。 */

    ADC_Init(ADC1,&ADC_InitStructure);                     /* 初始化ADC1。 */

    ADC_RegularChannelConfig(ADC1, ADC_Channel_4, 1, ADC_SampleTime_239Cycles5); /* 默认配置MQ135所在通道4。 */

    ADC_Cmd(ADC1,ENABLE);                                  /* 使能ADC1。 */

    ADC_ResetCalibration(ADC1);                             /* 复位ADC校准。 */
    while(ADC_GetResetCalibrationStatus(ADC1));             /* 等待复位校准完成。 */

    ADC_StartCalibration(ADC1);                             /* 开始ADC校准。 */
    while(ADC_GetCalibrationStatus(ADC1));                  /* 等待ADC校准完成。 */

    ADC_SoftwareStartConvCmd(ADC1,ENABLE);                  /* 启动一次转换，激活ADC工作状态。 */
}

uint16_t ADC_GetValue(ADC_TypeDef* ADCx,uint8_t ch)
{
    uint32_t sum = 0;                                       /* 保存4次采样累加值。 */
    uint8_t i;                                              /* 采样循环计数。 */

    (void)ADC_ReadOnce(ADCx, ch);                           /* 通道切换后先丢弃一次采样，减少残留误差。 */

    for (i = 0; i < 4; i++)
    {
        sum += ADC_ReadOnce(ADCx, ch);                      /* 累加有效采样值。 */
    }

    return (uint16_t)(sum >> 2);                             /* 除以4得到平均值。 */
}
