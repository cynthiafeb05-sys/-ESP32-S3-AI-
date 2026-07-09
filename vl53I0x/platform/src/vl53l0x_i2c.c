#include "vl53l0x_i2c.h"
#include "bsp_hard_i2c.h"

void VL53L0X_i2c_init(void)
{
    I2C1_Init();
}

/******** Ğ´ ********/
uint8_t VL53L0X_write_byte(uint8_t addr,uint8_t reg,uint8_t data)
{
    return I2C_WriteByte(addr, reg, data);
}

uint8_t VL53L0X_write_word(uint8_t addr,uint8_t reg,uint16_t data)
{
    uint8_t buf[2];
    buf[0]=data>>8;
    buf[1]=data;
    return I2C_BufferWrite(addr,reg,2,buf);
}

uint8_t VL53L0X_write_dword(uint8_t addr,uint8_t reg,uint32_t data)
{
    uint8_t buf[4];
    buf[0]=data>>24;
    buf[1]=data>>16;
    buf[2]=data>>8;
    buf[3]=data;
    return I2C_BufferWrite(addr,reg,4,buf);
}

/******** ¶Á ********/
uint8_t VL53L0X_read_byte(uint8_t addr,uint8_t reg,uint8_t *data)
{
    return I2C_BufferRead(addr,reg,1,data);
}

uint8_t VL53L0X_read_word(uint8_t addr,uint8_t reg,uint16_t *data)
{
    uint8_t buf[2];
    I2C_BufferRead(addr,reg,2,buf);
    *data=(buf[0]<<8)|buf[1];
    return 0;
}

uint8_t VL53L0X_read_dword(uint8_t addr,uint8_t reg,uint32_t *data)
{
    uint8_t buf[4];
    I2C_BufferRead(addr,reg,4,buf);
    *data=(buf[0]<<24)|(buf[1]<<16)|(buf[2]<<8)|buf[3];
    return 0;
}
uint8_t VL53L0X_write_multi(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    return I2C_BufferWrite(addr, reg, len, buf);
}

uint8_t VL53L0X_read_multi(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    return I2C_BufferRead(addr, reg, len, buf);
}
