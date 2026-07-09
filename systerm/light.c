#include "light.h"
#include "stm32f10x_adc.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_gpio.h"
#include "Delay.h"

/*
 * 普通光敏LED模块说明：
 * 1. PA7作为光敏电阻模拟输入，接入ADC1通道7；
 * 2. PA8作为普通LED输出，可根据光强自动开关；
 * 3. 当前主业务使用的是WS2813灯带，本文件保留普通LED光控方案作为备用模块。
 */

static float light_intensity = 0.0f;                         /* 保存最近一次计算得到的相对光强值。 */

void LightSensor_ADC_Init(void)
{
    ADC_InitTypeDef ADC_InitStructure;                       /* ADC初始化结构体。 */
    GPIO_InitTypeDef GPIO_InitStructure;                     /* GPIO初始化结构体。 */

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_GPIOA, ENABLE); /* 打开ADC1和GPIOA时钟。 */

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;                /* 选择PA7作为光敏电阻模拟输入。 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;            /* 模拟输入模式，关闭数字输入输出干扰。 */
    GPIO_Init(GPIOA, &GPIO_InitStructure);                   /* 写入PA7配置。 */

    RCC_ADCCLKConfig(RCC_PCLK2_Div6);                        /* ADC时钟设置为PCLK2/6，满足ADC时钟上限。 */

    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;       /* ADC1独立工作。 */
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;            /* 单通道转换，不开启扫描模式。 */
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;       /* 连续转换，方便随时读取最新光强。 */
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None; /* 软件触发转换。 */
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;   /* 12位ADC结果右对齐。 */
    ADC_InitStructure.ADC_NbrOfChannel = 1;                  /* 只转换1个规则通道。 */
    ADC_Init(ADC1, &ADC_InitStructure);                      /* 初始化ADC1。 */

    ADC_RegularChannelConfig(ADC1, ADC_Channel_7, 1, ADC_SampleTime_239Cycles5); /* 配置PA7对应的ADC通道7。 */

    ADC_Cmd(ADC1, ENABLE);                                   /* 使能ADC1。 */

    ADC_ResetCalibration(ADC1);                              /* 复位ADC校准寄存器。 */
    while (ADC_GetResetCalibrationStatus(ADC1));             /* 等待复位校准完成。 */
    ADC_StartCalibration(ADC1);                              /* 开始ADC校准。 */
    while (ADC_GetCalibrationStatus(ADC1));                  /* 等待ADC校准完成。 */

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);                  /* 启动第一次软件转换，之后连续转换。 */
}

void LightLED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;                     /* GPIO初始化结构体。 */

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);    /* 打开GPIOA时钟。 */

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;                /* 选择PA8作为普通LED控制引脚。 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;         /* 推挽输出，可直接输出高低电平。 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;        /* GPIO输出速度设置为50MHz。 */
    GPIO_Init(GPIOA, &GPIO_InitStructure);                   /* 写入PA8配置。 */

    GPIO_ResetBits(GPIOA, GPIO_Pin_8);                       /* 初始化默认关闭LED。 */
}

void Light_Init(void)
{
    LightSensor_ADC_Init();                                  /* 初始化光敏电阻ADC采集。 */
    LightLED_Init();                                         /* 初始化普通LED输出引脚。 */
}

float LightSensor_GetIntensity(void)
{
    uint16_t adc_val;                                        /* 保存ADC原始值，范围0~4095。 */
    float voltage;                                           /* 保存PA7采样电压，单位V。 */
    float r_ldr;                                             /* 保存估算出的光敏电阻阻值。 */

    while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));          /* 等待ADC转换完成。 */
    ADC_ClearFlag(ADC1, ADC_FLAG_EOC);                       /* 清除转换完成标志，准备下次读取。 */

    adc_val = ADC_GetConversionValue(ADC1);                  /* 读取ADC1当前转换结果。 */
    voltage = adc_val * 3.3f / 4095.0f;                      /* 把12位ADC值换算成0~3.3V电压。 */

    if (voltage <= 0.01f)
    {
        voltage = 0.01f;                                     /* 防止电压过低导致除0。 */
    }

    r_ldr = (3.3f - voltage) * 10000.0f / voltage;           /* 按10k分压电阻估算光敏电阻阻值。 */
    light_intensity = 259600.0f / r_ldr;                     /* 光强与光敏电阻阻值近似成反比。 */

    return light_intensity;                                  /* 返回相对光强，数值越大表示越亮。 */
}

void LightSensor_ControlLED(void)
{
    light_intensity = LightSensor_GetIntensity();            /* 更新当前环境光强。 */

    if (light_intensity > 43.0f)
    {
        GPIO_ResetBits(GPIOA, GPIO_Pin_8);                   /* 光强高于阈值时关闭普通LED。 */
    }
    else
    {
        GPIO_SetBits(GPIOA, GPIO_Pin_8);                     /* 光强低于阈值时点亮普通LED。 */
    }
}
