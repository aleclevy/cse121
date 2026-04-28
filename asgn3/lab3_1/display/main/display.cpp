/*!
 * @file Display.ino
 * @brief display.
 * @copyright	Copyright (c) 2010 DFRobot Co.Ltd (http://www.dfrobot.com)
 * @licence     The MIT License (MIT)
 * @maintainer [yangfeng](feng.yang@dfrobot.com)
 * @version  V1.0
 * @date  2021-09-24
 * @url https://github.com/DFRobot/DFRobot_RGBLCD1602
 */
#include "DFRobot_RGBLCD1602.h"

/*
Change the RGBaddr value based on the hardware version
-----------------------------------------
       Moudule        | Version| RGBAddr|
-----------------------------------------
  LCD1602 Module      |  V1.0  | 0x60   |
-----------------------------------------
  LCD1602 Module      |  V1.1  | 0x6B   |
-----------------------------------------
  LCD1602 RGB Module  |  V1.0  | 0x60   |
-----------------------------------------
  LCD1602 RG Module  |  V2.0  | 0x2D   |
-----------------------------------------
*/

DFRobot_RGBLCD1602 lcd(/*RGBAddr*/0x2D ,/*lcdCols*/16,/*lcdRows*/2, , );  //16 characters and 2 lines of show

static void i2c_master_init(i2c_master_bus_handle_t *bus_handle, i2c_master_dev_handle_t *dev_handle){
	i2c_master_bus_config_t bus_config = {
		.i2c_port = I2C_NUM_0,
		.sda_io_num = 7,
		.scl_io_num = 8,
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = 7,
		.flags.enable_internal_pullup = true,
	};

	i2c_device_config_t dev_config = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
	.device_address = LED_ADDRESS, 
	.scl_speed_hz = I2C_MASTER_FREQ_HZ,	
	};

void DFROBOT_RGBLCD1602::send(uint8_t *data, uint8_t len){
	i2c_master_write_to_device(I2C_MASTER_NUM, LCD_ADDRESS, data, len, pdMS_TO_TICKS(100));
}

void DFRobot_RGBLCD1602::setReg(uint8_t reg, uint8_t data) {
    uint8_t buf[2] = {reg, data};
    i2c_master_write_to_device(
        I2C_MASTER_NUM,
        _RGBAddr,          // 0x2D
        buf,
        2,
        pdMS_TO_TICKS(100)
    );
}

void app_main(){
	while(true){
		lcd.init();
		lcd.setRGB("Hello CSE121!");
		lcd.setCursor(0,1);
		lcd.printstr("Levy");
	}
}
