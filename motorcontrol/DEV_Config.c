/*****************************************************************************
* | File        :   DEV_Config.c
* | Author      :   Waveshare team
* | Function    :   Hardware underlying interface
* | Info        :
*                Used to shield the underlying layers of each master
*                and enhance portability
*----------------
* |	This version:   V1.0
* | Date        :   2018-09-03
* | Info        :   Basic version
*
******************************************************************************/
#include "DEV_Config.h"
#include "Debug.h"  //DEBUG()

#include <bcm2835.h>


/**
 * Module Initialize, use BCM2835 library.
 *
 * Example:
 * if(DEV_ModuleInit())
 *   exit(0);
 */
UBYTE DEV_ModuleInit(void)
{
    if(!bcm2835_init()) {
        DEBUG("bcm2835 init failed   !!! \r\n");
        return 1;
    } else {
        DEBUG("bcm2835 init success  !!! \r\n");
    }

    return 0;
}

/**
 * Module Exit, close BCM2835 library.
 *
 * Example:
 * DEV_ModuleExit();
 */
void DEV_ModuleExit(void)
{
    bcm2835_i2c_end();
    bcm2835_close();
}

/**
 * Module Initialize, use BCM2835 library.
 *
 * @param addr: I2C Device address.
 *
 * Example:
 * DEV_I2C_Init(0X57);
 */
void DEV_I2C_Init(char addr)
{
    bcm2835_i2c_begin();
    bcm2835_i2c_setSlaveAddress(addr);  //i2c address

    /*
    BCM2835_I2C_CLOCK_DIVIDER_2500 ：2500 = 10us = 100 kHz
    BCM2835_I2C_CLOCK_DIVIDER_626  ：622 = 2.504us = 399.3610 kHz
    150 = 60ns = 1.666 MHz (default at reset)
    148 = 59ns = 1.689 MHz
    */
    bcm2835_i2c_setClockDivider(BCM2835_I2C_CLOCK_DIVIDER_626);
    // bcm2835_i2c_set_baudrate(100000);    //100k baudrate
}

/**
 * I2C write byte.
 *
 * @param reg: register.
 * @param value: value.
 *
 * Example:
 * DEV_I2C_WriteByte(0x00, 0xff);
 */
void DEV_I2C_WriteByte(UBYTE reg, UBYTE value)
{
    char ref = 0;
    char buf[2] = {reg, value};
    ref = bcm2835_i2c_write(buf, 2);
    while(ref != 0) {
        ref = bcm2835_i2c_write(buf, 2);
        if(ref == 0)
            break;
    }
}

/**
 * I2C read byte.
 *
 * @param reg: register.
 *
 * Example:
 * UBYTE buf = PCA9685_Read(0x00);
 */
UBYTE DEV_I2C_ReadByte(UBYTE reg)
{
    char ref = 0;
    char buf[1] = {reg};
    ref = bcm2835_i2c_write(buf, 1);
    while(ref != 0) {
        ref = bcm2835_i2c_write(buf, 1);
        if(ref == 0)
            break;
    }
    bcm2835_i2c_read(buf, 1);

    return buf[0];
}

/**
 * Millisecond delay.
 *
 * @param xms: time.
 *
 * Example:
 * DEV_Delay_ms(500);//delay 500ms
 */
void DEV_Delay_ms(uint32_t xms)
{
    bcm2835_delay(xms);
}

/**
 * Microsecond delay.
 *
 * @param xus: time.
 *
 * Example:
 * DEV_Delay_us(500);//delay 500us
 */
void DEV_Delay_us(uint32_t xus)
{
    int j;
    for(j=xus; j > 0; j--);
}
