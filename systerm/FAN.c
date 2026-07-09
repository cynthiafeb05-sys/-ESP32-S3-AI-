#include "fan.h"
#include <rtthread.h>
#include <finsh.h>

/*
 * 风扇模块说明：
 * 1. 两路风扇分别接PA7和PA5；
 * 2. 风扇输出为低电平有效，所以GPIO_ResetBits表示打开，GPIO_SetBits表示关闭；
 * 3. 本模块使用RT-Thread线程模拟软件PWM，通过FAN_SetSpeed设置0~100%占空比。
 */

#define FAN_PWM_PERIOD_MS 20U          /* 软件PWM周期，20ms约等于50Hz。 */

static uint8_t fan_speed_percent = 0;  /* 当前风扇PWM占空比，范围0~100。 */
static uint8_t fan_manual_mode = 0;    /* 手动测试模式标志：1时暂停自动PWM控制。 */
static rt_thread_t fan_pwm_tid = RT_NULL; /* 风扇PWM线程句柄，防止重复创建线程。 */

static void FAN_OutputOn(void)
{
    GPIO_ResetBits(FAN1_PORT, FAN1_PIN); /* 风扇1低电平有效，拉低PA7打开风扇1。 */
    GPIO_ResetBits(FAN2_PORT, FAN2_PIN); /* 风扇2低电平有效，拉低PA5打开风扇2。 */
}

static void FAN_OutputOff(void)
{
    GPIO_SetBits(FAN1_PORT, FAN1_PIN);   /* 拉高PA7关闭风扇1。 */
    GPIO_SetBits(FAN2_PORT, FAN2_PIN);   /* 拉高PA5关闭风扇2。 */
}

static void FAN_OutputSingle(uint8_t fan_no, uint8_t state)
{
    uint16_t pin;
    GPIO_TypeDef *port;

    if (fan_no == 1U)                     /* fan_no为1时选择风扇1。 */
    {
        port = FAN1_PORT;
        pin = FAN1_PIN;
    }
    else
    {
        port = FAN2_PORT;
        pin = FAN2_PIN;
    }

    if (state)                            /* state为1表示打开指定风扇。 */
    {
        GPIO_ResetBits(port, pin);        /* 低电平有效，拉低引脚打开风扇。 */
    }
    else
    {
        GPIO_SetBits(port, pin);          /* 拉高引脚关闭风扇。 */
    }
}

static void FAN_PWM_Thread(void *parameter)
{
    (void)parameter;

    while (1)
    {
        uint8_t duty = fan_speed_percent; /* 读取当前占空比，作为本周期PWM输出依据。 */

        if (fan_manual_mode)
        {
            rt_thread_mdelay(FAN_PWM_PERIOD_MS); /* 手动模式下不改GPIO，只保持测试输出。 */
            continue;                            /* 跳过本轮自动PWM控制。 */
        }

        if (duty == 0U)
        {
            FAN_OutputOff();                     /* 占空比0%，整个周期关闭风扇。 */
            rt_thread_mdelay(FAN_PWM_PERIOD_MS); /* 延时一个PWM周期。 */
        }
        else if (duty >= 100U)
        {
            FAN_OutputOn();                      /* 占空比100%，整个周期打开风扇。 */
            rt_thread_mdelay(FAN_PWM_PERIOD_MS); /* 延时一个PWM周期。 */
        }
        else
        {
            uint16_t on_ms = (uint16_t)((FAN_PWM_PERIOD_MS * duty) / 100U); /* 根据占空比计算打开时间。 */
            uint16_t off_ms = (uint16_t)(FAN_PWM_PERIOD_MS - on_ms);        /* 剩余时间作为关闭时间。 */

            if (on_ms == 0U)
            {
                on_ms = 1U;                      /* 保证非零占空比至少打开1ms。 */
            }
            if (off_ms == 0U)
            {
                off_ms = 1U;                     /* 保证非100%占空比至少关闭1ms。 */
            }

            FAN_OutputOn();                      /* PWM高占空阶段：打开风扇。 */
            rt_thread_mdelay(on_ms);             /* 保持打开时间。 */
            FAN_OutputOff();                     /* PWM低占空阶段：关闭风扇。 */
            rt_thread_mdelay(off_ms);            /* 保持关闭时间。 */
        }
    }
}

void FAN_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;         /* GPIO初始化结构体。 */

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); /* 打开风扇所在GPIOA时钟。 */

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_InitStructure.GPIO_Pin = FAN1_PIN;
    GPIO_Init(FAN1_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = FAN2_PIN;
    GPIO_Init(FAN2_PORT, &GPIO_InitStructure);

    FAN_OutputOff();                             /* 初始化完成后默认关闭风扇，保证上电安全。 */

    if (fan_pwm_tid == RT_NULL)
    {
        fan_pwm_tid = rt_thread_create("fanpwm", FAN_PWM_Thread, RT_NULL, 256, 8, 5); /* 创建风扇软件PWM线程。 */
        if (fan_pwm_tid != RT_NULL)
        {
            rt_thread_startup(fan_pwm_tid);      /* 启动PWM线程，让风扇可按占空比运行。 */
        }
    }
}

void FAN_Control(uint8_t state)
{
    FAN_SetSpeed(state ? 100U : 0U);             /* 兼容开关控制接口：开=100%，关=0%。 */
}

void FAN_SetSpeed(uint8_t percent)
{
    if (fan_manual_mode)
    {
        return;                                  /* 手动测试模式下不允许自动逻辑改占空比。 */
    }

    if (percent > 100U)
    {
        percent = 100U;                          /* 占空比上限保护，避免超过100%。 */
    }

    fan_speed_percent = percent;                 /* 保存新的PWM占空比，PWM线程下一周期生效。 */
}

void FAN_TestOutput(uint8_t fan_no, uint8_t state)
{
    fan_manual_mode = 1;                         /* 进入手动测试模式，暂停自动PWM线程改GPIO。 */

    if (fan_no == 0U)
    {
        if (state)
        {
            FAN_OutputOn();
        }
        else
        {
            FAN_OutputOff();
        }
    }
    else if (fan_no == 1U)
    {
        FAN_OutputSingle(1U, state);
    }
    else if (fan_no == 2U)
    {
        FAN_OutputSingle(2U, state);
    }
}

void FAN_SetAutoMode(void)
{
    fan_manual_mode = 0;                         /* 退出手动测试模式，恢复自动PWM控制。 */
}

static void fan_test(int argc, char **argv)
{
    uint8_t fan_no;
    uint8_t state;

    if (argc < 3)
    {
        rt_kprintf("Usage: fan_test <0|1|2> <0|1>\r\n");
        rt_kprintf("0=both, 1=PA7 fan, 2=PA5 fan; 0=off, 1=on\r\n");
        return;
    }

    fan_no = (uint8_t)(argv[1][0] - '0');
    state = (uint8_t)(argv[2][0] - '0');

    FAN_TestOutput(fan_no, state);
    rt_kprintf("fan_test fan:%d state:%d\r\n", fan_no, state ? 1 : 0);
}
MSH_CMD_EXPORT(fan_test, test fan output: fan_test fan_no state);

static void fan_auto(void)
{
    FAN_SetAutoMode();
    rt_kprintf("fan auto mode\r\n");
}
MSH_CMD_EXPORT(fan_auto, resume fan pid auto mode);
