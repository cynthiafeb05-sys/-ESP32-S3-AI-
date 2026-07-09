#include "mq135.h"
#include "adc.h"
#include "oled.h"
#include "delay.h"

/*
 * MQ135空气质量模块说明：
 * 1. MQ135的AO引脚接入STM32 ADC1通道4；
 * 2. 本模块负责读取ADC原始值，并可换算为电压；
 * 3. 主业务线程会把ADC值作为空气质量判断依据，超过阈值后串口报警。
 */

static float mq135_voltage = 0.0f;     /* 保存最近一次换算得到的MQ135输出电压，单位V。 */
static uint16_t mq135_adc_value = 0;   /* 保存最近一次读取到的MQ135 ADC原始值，范围0~4095。 */

void MQ135_Init(void)
{
    ADC1_Init();                       /* 初始化ADC1，保证MQ135所在ADC通道可以正常采样。 */
}

uint16_t MQ135_GetADCValue(void)
{
    mq135_adc_value = ADC_GetValue(ADC1, ADC_Channel_4); /* 读取MQ135连接的ADC1通道4原始值。 */
    return mq135_adc_value;                              /* 返回本次采集到的空气质量原始ADC值。 */
}

float MQ135_GetVoltage(void)
{
    mq135_adc_value = MQ135_GetADCValue();               /* 先读取最新ADC值，保证电压换算使用新数据。 */
    mq135_voltage = (float)mq135_adc_value * 3.3f / 4095.0f; /* 将12位ADC值按3.3V参考电压换算为实际电压。 */
    return mq135_voltage;                                /* 返回MQ135模拟输出电压。 */
}

uint16_t MQ135_GetAirQuality(void)
{
    return MQ135_GetADCValue();                          /* 当前直接使用ADC原始值作为空气质量指标。 */
}

void MQ135_DisplayInfo(uint8_t line)
{
    uint16_t air_quality = MQ135_GetAirQuality();        /* 读取空气质量值，用于OLED本地显示。 */

    OLED_ShowString(line, 1, "Air ADC:");                /* OLED显示空气质量ADC标题。 */
    OLED_ShowNum(line, 10, air_quality, 4);              /* OLED显示4位ADC数值。 */

    OLED_ShowString(line + 1, 1, "Level:");              /* OLED下一行显示空气质量等级标题。 */
    if (air_quality > 2000U)
    {
        OLED_ShowString(line + 1, 8, "Danger");          /* ADC大于2000，认为空气质量危险。 */
    }
    else if (air_quality > 1200U)
    {
        OLED_ShowString(line + 1, 8, "Heavy");           /* ADC大于1200，显示污染较重。 */
    }
    else if (air_quality > 800U)
    {
        OLED_ShowString(line + 1, 8, "Light");           /* ADC大于800，显示轻度污染。 */
    }
    else if (air_quality > 400U)
    {
        OLED_ShowString(line + 1, 8, "Good");            /* ADC大于400，显示空气较好。 */
    }
    else
    {
        OLED_ShowString(line + 1, 8, "Best");            /* ADC较低，显示空气最佳。 */
    }
}
