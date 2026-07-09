#include "stm32f10x.h"
#include "OLED_Font.h"

/*
 * OLED显示模块说明：
 * 1. 使用PB8/PB9模拟I2C驱动OLED；
 * 2. OLED_ShowString/OLED_ShowNum等函数负责在指定行列显示字符和数字；
 * 3. 主业务线程每秒刷新温湿度、空气质量、距离、心率、风扇/灯带状态。
 */

#define OLED_ADDR 0x78                         /* OLED I2C写地址。 */

#define OLED_I2C_PORT      GPIOB               /* OLED模拟I2C使用GPIOB。 */
#define OLED_I2C_CLK       RCC_APB2Periph_GPIOB /* OLED模拟I2C GPIO时钟。 */
#define OLED_I2C_SCL_PIN   GPIO_Pin_8          /* OLED SCL引脚。 */
#define OLED_I2C_SDA_PIN   GPIO_Pin_9          /* OLED SDA引脚。 */

#define OLED_SCL_H()       GPIO_SetBits(OLED_I2C_PORT, OLED_I2C_SCL_PIN)
#define OLED_SCL_L()       GPIO_ResetBits(OLED_I2C_PORT, OLED_I2C_SCL_PIN)
#define OLED_SDA_H()       GPIO_SetBits(OLED_I2C_PORT, OLED_I2C_SDA_PIN)
#define OLED_SDA_L()       GPIO_ResetBits(OLED_I2C_PORT, OLED_I2C_SDA_PIN)

static void OLED_I2C_Delay(void)
{
    uint8_t i;
    for (i = 0; i < 10; i++)
    {
    }
}

static void OLED_I2C_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(OLED_I2C_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin = OLED_I2C_SCL_PIN | OLED_I2C_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(OLED_I2C_PORT, &GPIO_InitStructure);

    OLED_SCL_H();
    OLED_SDA_H();
}

static void OLED_I2C_Start(void)
{
    OLED_SDA_H();
    OLED_SCL_H();
    OLED_I2C_Delay();
    OLED_SDA_L();
    OLED_I2C_Delay();
    OLED_SCL_L();
}

static void OLED_I2C_Stop(void)
{
    OLED_SDA_L();
    OLED_SCL_H();
    OLED_I2C_Delay();
    OLED_SDA_H();
    OLED_I2C_Delay();
}

static void OLED_I2C_SendByte(uint8_t data)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        if (data & 0x80)
        {
            OLED_SDA_H();
        }
        else
        {
            OLED_SDA_L();
        }

        data <<= 1;
        OLED_I2C_Delay();
        OLED_SCL_H();
        OLED_I2C_Delay();
        OLED_SCL_L();
        OLED_I2C_Delay();
    }

    OLED_SDA_H();
    OLED_I2C_Delay();
    OLED_SCL_H();
    OLED_I2C_Delay();
    OLED_SCL_L();
}

static void OLED_I2C_WriteByte(uint8_t control, uint8_t data)
{
    OLED_I2C_Start();
    OLED_I2C_SendByte(OLED_ADDR);
    OLED_I2C_SendByte(control);
    OLED_I2C_SendByte(data);
    OLED_I2C_Stop();
}

void OLED_WriteCommand(uint8_t cmd)
{
    OLED_I2C_WriteByte(0x00, cmd);
}

void OLED_WriteData(uint8_t data)
{
    OLED_I2C_WriteByte(0x40, data);
}

void OLED_SetCursor(uint8_t Y, uint8_t X)
{
    OLED_WriteCommand(0xB0 | Y);
    OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));
    OLED_WriteCommand(0x00 | (X & 0x0F));
}

void OLED_Clear(void)
{
    uint8_t i, j;

    for (j = 0; j < 8; j++)
    {
        OLED_SetCursor(j, 0);
        for (i = 0; i < 128; i++)
        {
            OLED_WriteData(0x00);
        }
    }
}

void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
    uint8_t i;

    if ((Char < ' ') || (Char > '~'))
    {
        Char = ' ';
    }

    OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);
    for (i = 0; i < 8; i++)
    {
        OLED_WriteData(OLED_F8x16[Char - ' '][i]);
    }

    OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);
    for (i = 0; i < 8; i++)
    {
        OLED_WriteData(OLED_F8x16[Char - ' '][i + 8]);
    }
}

void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
    uint8_t i;

    for (i = 0; (String[i] != '\0') && (Column + i <= 16); i++)
    {
        OLED_ShowChar(Line, Column + i, String[i]);
    }
}

uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
    uint32_t Result = 1;

    while (Y--)
    {
        Result *= X;
    }

    return Result;
}

void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i;

    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, Column + i,
                      Number / OLED_Pow(10, Length - i - 1) % 10 + '0');
    }
}

void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
    uint8_t i;
    uint32_t Number1;

    if (Number >= 0)
    {
        OLED_ShowChar(Line, Column, '+');
        Number1 = Number;
    }
    else
    {
        OLED_ShowChar(Line, Column, '-');
        Number1 = -Number;
    }

    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, Column + i + 1,
                      Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0');
    }
}

void OLED_Init(void)
{
    uint32_t i, j;

    OLED_I2C_GPIO_Init();

    for (i = 0; i < 1000; i++)
    {
        for (j = 0; j < 1000; j++)
        {
        }
    }

    OLED_WriteCommand(0xAE);
    OLED_WriteCommand(0xD5);
    OLED_WriteCommand(0x80);
    OLED_WriteCommand(0xA8);
    OLED_WriteCommand(0x3F);
    OLED_WriteCommand(0xD3);
    OLED_WriteCommand(0x00);
    OLED_WriteCommand(0x40);
    OLED_WriteCommand(0xA1);
    OLED_WriteCommand(0xC8);
    OLED_WriteCommand(0xDA);
    OLED_WriteCommand(0x12);
    OLED_WriteCommand(0x81);
    OLED_WriteCommand(0xCF);
    OLED_WriteCommand(0xD9);
    OLED_WriteCommand(0xF1);
    OLED_WriteCommand(0xDB);
    OLED_WriteCommand(0x30);
    OLED_WriteCommand(0xA4);
    OLED_WriteCommand(0xA6);
    OLED_WriteCommand(0x8D);
    OLED_WriteCommand(0x14);
    OLED_WriteCommand(0xAF);

    OLED_Clear();
}
