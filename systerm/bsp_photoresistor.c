/*
 * 光敏电阻模块说明
 * 1. 本文件是独立光敏电阻ADC驱动示例
 * 2. 当前主业务使用User/RT_Thread_demo.c里的ADC读取逻辑控制WS2813灯带
 * 3. 本文件保留用于单独测试光敏电阻模拟量和数字量输入
  ******************************************************************************
  * @file    bsp_photoresistor.c
  * @author  fire
  * @version V1.0
  * @date    2024-xx-xx
  * @brief   光敏模块
  ******************************************************************************
  * @attention
  *
  * 实验平台:野火 F103 STM32 开发板 
  * 论坛    :http://www.firebbs.cn
  * 淘宝    :https://fire-stm32.taobao.com
  *
  ******************************************************************************
  */ 
#include "bsp_photoresistor.h"

__IO uint16_t ADC_ConvertedValue;


/**
  * @brief  光敏电阻的GPIO配置
  * @param  无
  * @retval 无
  */
static void PhotoResistor_GPIO_Config(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	// 打开 ADC IO端口时钟
	ADC_GPIO_APBxClock_FUN ( ADC_GPIO_CLK, ENABLE );
    //打开 数字量 IO端口时钟
    PhotoResistor_GPIO_APBxClock_FUN ( PhotoResistor_GPIO_CLK, ENABLE );
	
	// 配置 AO 模拟量 IO 引脚模式
	// 必须为模拟输入
	GPIO_InitStructure.GPIO_Pin = ADC_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
	// 初始化 ADC IO
	GPIO_Init(ADC_PORT, &GPIO_InitStructure);	
    
    // 配置 DO 数字量 IO 引脚模式
	// 浮空输入
	GPIO_InitStructure.GPIO_Pin = PhotoResistor_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	// 初始化 PhotoResistor IO
	GPIO_Init(PhotoResistor_PORT, &GPIO_InitStructure);	
    
}

/**
  * @brief  光敏电阻相关的ADC配置
  * @param  无
  * @retval 无
  */
static void PhotoResistor_ADC_Mode_Config(void)
{
	ADC_InitTypeDef ADC_InitStructure;	

	// 打开ADC时钟
	ADC_APBxClock_FUN ( ADC_CLK, ENABLE );
	
	// ADC 模式配置
	// 只使用一个ADC，属于独立模式
	ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
	
	// 禁止扫描模式，多通道才要，单通道不需要
	ADC_InitStructure.ADC_ScanConvMode = DISABLE ; 

	// 连续转换模式
	ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;

	// 不用外部触发转换，软件开启即可
	ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;

	// 转换结果右对齐
	ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
	
	// 转换通道1个
	ADC_InitStructure.ADC_NbrOfChannel = 1;	
		
	// 初始化ADC
	ADC_Init(ADCx, &ADC_InitStructure);
	
	// 配置ADC时钟为PCLK2的8分频，即9MHz
	RCC_ADCCLKConfig(RCC_PCLK2_Div8); 
	
	// 配置 ADC 通道转换顺序和采样时间
	ADC_RegularChannelConfig(ADCx, ADC_CHANNEL, 1, 
	                         ADC_SampleTime_55Cycles5);
	
	// ADC 转换结束产生中断，在中断服务程序中读取转换值
	ADC_ITConfig(ADCx, ADC_IT_EOC, ENABLE);
	
	// 开启ADC ，并开始转换
	ADC_Cmd(ADCx, ENABLE);
	
	// 初始化ADC 校准寄存器  
	ADC_ResetCalibration(ADCx);
	// 等待校准寄存器初始化完成
	while(ADC_GetResetCalibrationStatus(ADCx));
	
	// ADC开始校准
	ADC_StartCalibration(ADCx);
	// 等待校准完成
	while(ADC_GetCalibrationStatus(ADCx));
	
	// 由于没有采用外部触发，所以使用软件触发ADC转换 
	ADC_SoftwareStartConvCmd(ADCx, ENABLE);
}

/**
  * @brief  ADC中断配置
  * @param  无
  * @retval 无
  */
static void PhotoResistor_ADC_NVIC_Config(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;
    // 优先级分组
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    
    // 配置中断优先级
    NVIC_InitStructure.NVIC_IRQChannel = ADC_IRQ;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}


/**
  * @brief  光敏电阻初始化
  * @param  无
  * @retval 无
  */
void PhotoResistor_Init(void)
{
    PhotoResistor_GPIO_Config();
    PhotoResistor_ADC_Mode_Config();
    PhotoResistor_ADC_NVIC_Config();
}

/*********************************************END OF FILE**********************/
