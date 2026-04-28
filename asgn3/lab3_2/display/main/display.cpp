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
#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

DFRobot_RGBLCD1602 lcd(/*RGBAddr*/0x2D ,/*lcdCols*/16,/*lcdRows*/2 );  //16 characters and 2 lines of show

static i2c_master_dev_handle_t lcd_dev_handle;
static i2c_master_dev_handle_t rgb_dev_handle;

#define SENSOR_ADDR                 0x70
#define I2C_MASTER_TIMEOUT_MS       1000

const uint8_t color_define[4][3] = {
    {255, 255, 255},  // WHITE
    {255, 0,   0  },  // RED
    {0,   255, 0  },  // GREEN
    {0,   0,   255},  // BLUE
};

void DFRobot_RGBLCD1602::init(i2c_master_bus_handle_t bus)
{
  i2c_master_init(bus);
  if(_RGBAddr == (0x60)){
    REG_RED   =      0x04;
    REG_GREEN =      0x03;
    REG_BLUE  =      0x02;
    REG_ONLY  =      0x02 ;
  } else if(_RGBAddr == (0x60>>1)){
    REG_RED      =   0x06 ;       // pwm2
    REG_GREEN    =   0x07 ;       // pwm1
    REG_BLUE     =   0x08 ;       // pwm0
    REG_ONLY     =   0x08 ;
  } else if(_RGBAddr == (0x6B)){
    REG_RED      =   0x06 ;       // pwm2
    REG_GREEN    =   0x05 ;       // pwm1
    REG_BLUE     =   0x04 ;       // pwm0
    REG_ONLY     =   0x04 ; 
  } else if(_RGBAddr == (0x2D)){
    REG_RED      =   0x01 ;       // pwm2
    REG_GREEN    =   0x02 ;       // pwm1
    REG_BLUE     =   0x03 ;       // pwm0
    REG_ONLY     =   0x01 ; 
  }
  _showFunction = LCD_4BITMODE | LCD_1LINE | LCD_5x8DOTS;
  begin(_rows);
}

void DFRobot_RGBLCD1602::clear()
{
    command(LCD_CLEARDISPLAY);        // clear display, set cursor position to zero
    vTaskDelay(pdMS_TO_TICKS(2));
}

void DFRobot_RGBLCD1602::home()
{
    command(LCD_RETURNHOME);        // set cursor position to z
    vTaskDelay(pdMS_TO_TICKS(2));
}

void DFRobot_RGBLCD1602::noDisplay()
{
    _showControl &= ~LCD_DISPLAYON;
    command(LCD_DISPLAYCONTROL | _showControl);
}

void DFRobot_RGBLCD1602::display() {
    _showControl |= LCD_DISPLAYON;
    command(LCD_DISPLAYCONTROL | _showControl);
}

void DFRobot_RGBLCD1602::stopBlink()
{
    _showControl &= ~LCD_BLINKON;
    command(LCD_DISPLAYCONTROL | _showControl);
}
void DFRobot_RGBLCD1602::blink()
{
    _showControl |= LCD_BLINKON;
    command(LCD_DISPLAYCONTROL | _showControl);
}

void DFRobot_RGBLCD1602::noCursor()
{
    _showControl &= ~LCD_CURSORON;
    command(LCD_DISPLAYCONTROL | _showControl);
}

void DFRobot_RGBLCD1602::cursor() {
    _showControl |= LCD_CURSORON;
    command(LCD_DISPLAYCONTROL | _showControl);
}

void DFRobot_RGBLCD1602::scrollDisplayLeft(void)
{
    command(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVELEFT);
}

void DFRobot_RGBLCD1602::scrollDisplayRight(void)
{
    command(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVERIGHT);
}

void DFRobot_RGBLCD1602::leftToRight(void)
{
    _showMode |= LCD_ENTRYLEFT;
    command(LCD_ENTRYMODESET | _showMode);
}

void DFRobot_RGBLCD1602::rightToLeft(void)
{
    _showMode &= ~LCD_ENTRYLEFT;
    command(LCD_ENTRYMODESET | _showMode);
}

void DFRobot_RGBLCD1602::noAutoscroll(void)
{
    _showMode &= ~LCD_ENTRYSHIFTINCREMENT;
    command(LCD_ENTRYMODESET | _showMode);
}

void DFRobot_RGBLCD1602::autoscroll(void)
{
    _showMode |= LCD_ENTRYSHIFTINCREMENT;
    command(LCD_ENTRYMODESET | _showMode);
}

void DFRobot_RGBLCD1602::customSymbol(uint8_t location, uint8_t charmap[])
{

    location &= 0x7; // we only have 8 locations 0-7
    command(LCD_SETCGRAMADDR | (location << 3));
    
    
    uint8_t data[9];
    data[0] = 0x40;
    for(int i=0; i<8; i++)
    {
        data[i+1] = charmap[i];
    }
    send(data, 9);
}

void DFRobot_RGBLCD1602::setCursor(uint8_t col, uint8_t row)
{

    col = (row == 0 ? col|0x80 : col|0xc0);
    uint8_t data[3] = {0x80, col};

    send(data, 2);

}

void DFRobot_RGBLCD1602::setRGB(uint8_t r, uint8_t g, uint8_t b)
{
  uint16_t temp_r,temp_g,temp_b;
  if(_RGBAddr == 0x60>>1){
    temp_r = (uint16_t)r*192/255;
    temp_g = (uint16_t)g*192/255;
    temp_b = (uint16_t)b*192/255;
    setReg(REG_RED, temp_r);
    setReg(REG_GREEN, temp_g);
    setReg(REG_BLUE, temp_b);
  } else{
    setReg(REG_RED, r);
    setReg(REG_GREEN, g);
    setReg(REG_BLUE, b);
    if(_RGBAddr == 0x6B){
      setReg(0x07, 0xFF);
    }
  }

}

void DFRobot_RGBLCD1602::setColor(uint8_t color)
{
    if(color > 3)return ;
    setRGB(color_define[color][0], color_define[color][1], color_define[color][2]);
}


void DFRobot_RGBLCD1602::write(uint8_t value)
{

    uint8_t data[3] = {0x40, value};
    send(data, 2);
}

inline void DFRobot_RGBLCD1602::command(uint8_t value)
{
    uint8_t data[3] = {0x80, value};
    send(data, 2);
}



void DFRobot_RGBLCD1602::setBacklight(bool mode){
	if(mode){
		setColorWhite();		// turn backlight on
	}else{
		closeBacklight();		// turn backlight off
	}
}

/*******************************private*******************************/
void DFRobot_RGBLCD1602::begin( uint8_t rows, uint8_t charSize) 
{
    if (rows > 1) {
        _showFunction |= LCD_2LINE;
    }
    _numLines = rows;
    _currLine = 0;
    ///< for some 1 line displays you can select a 10 pixel high font
    if ((charSize != 0) && (rows == 1)) {
        _showFunction |= LCD_5x10DOTS;
    }

    ///< SEE PAGE 45/46 FOR INITIALIZATION SPECIFICATION!
    ///< according to datasheet, we need at least 40ms after power rises above 2.7V
    ///< before sending commands. Arduino can turn on way befer 4.5V so we'll wait 50
    vTaskDelay(pdMS_TO_TICKS(.05));

    ///< this is according to the hitachi HD44780 datasheet
    ///< page 45 figure 23

    ///< Send function set command sequence
    command(LCD_FUNCTIONSET | _showFunction);
    vTaskDelay(pdMS_TO_TICKS(.005));
	
	///< second try
    command(LCD_FUNCTIONSET | _showFunction);
    vTaskDelay(pdMS_TO_TICKS(.005));

    ///< third go
    command(LCD_FUNCTIONSET | _showFunction);

    ///< turn the display on with no cursor or blinking default
    _showControl = LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF;
    display();

    ///< clear it off
    clear();

    ///< Initialize to default text direction (for romance languages)
    _showMode = LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT;
    ///< set the entry mode
    command(LCD_ENTRYMODESET | _showMode);
    
    if(_RGBAddr == (0xc0>>1)){
      ///< backlight init
      setReg(REG_MODE1, 0);
      ///< set LEDs controllable by both PWM and GRPPWM registers
      setReg(REG_OUTPUT, 0xFF);
      ///< set MODE2 values
      ///< 0010 0000 -> 0x20  (DMBLNK to 1, ie blinky mode)
      setReg(REG_MODE2, 0x20);
    }else if(_RGBAddr == (0x60>>1)){
       setReg(0x01, 0x00);
       setReg(0x02, 0xfF);
       setReg(0x04, 0x15);
    }else if(_RGBAddr==0x6B){
        setReg(0x2F, 0x00);
        setReg(0x00, 0x20);
        setReg(0x01, 0x00);
        setReg(0x02, 0x01);
        setReg(0x03, 4);
    }
    setColorWhite();
}


// Constructor
DFRobot_RGBLCD1602::DFRobot_RGBLCD1602(uint8_t RGBAddr, uint8_t lcdCols,
                                         uint8_t lcdRows, uint8_t lcdAddr) {
    _RGBAddr = RGBAddr;
    _lcdAddr = lcdAddr;
    _cols    = lcdCols;
    _rows    = lcdRows;
}

// printstr
void DFRobot_RGBLCD1602::printstr(const char* str) {
    while (*str) {
        write((uint8_t)*str++);
    }
}

//---------------------------------------------------------------------------------------------------
void DFRobot_RGBLCD1602::i2c_master_init(i2c_master_bus_handle_t bus_handle) {

    i2c_device_config_t lcd_dev_config = {};
    lcd_dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    lcd_dev_config.device_address = _lcdAddr;
    lcd_dev_config.scl_speed_hz = I2C_MASTER_FREQ_HZ;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &lcd_dev_config, &lcd_dev_handle));

    i2c_device_config_t rgb_dev_config = {};
    rgb_dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    rgb_dev_config.device_address = _RGBAddr;
    rgb_dev_config.scl_speed_hz = I2C_MASTER_FREQ_HZ;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &rgb_dev_config, &rgb_dev_handle));
}

void DFRobot_RGBLCD1602::send(uint8_t *data, uint8_t len){
	i2c_master_transmit(lcd_dev_handle, data, len, pdMS_TO_TICKS(100));
}

void DFRobot_RGBLCD1602::setReg(uint8_t reg, uint8_t data) {
    uint8_t buf[2] = {reg, data};
    i2c_master_transmit(
	rgb_dev_handle,
        buf,
        2,
        pdMS_TO_TICKS(100)
    );
}


static esp_err_t sensor_wakeup(i2c_master_dev_handle_t dev_handle) {
    uint8_t cmd[2] = {0x35, 0x17};
    i2c_master_transmit(dev_handle, cmd, 2, I2C_MASTER_TIMEOUT_MS);
    vTaskDelay(pdMS_TO_TICKS(2));
    return ESP_OK;
}

static esp_err_t sensor_sleep(i2c_master_dev_handle_t dev_handle) {
    uint8_t cmd[2] = {0xB0, 0x98};
    return i2c_master_transmit(dev_handle, cmd, 2, I2C_MASTER_TIMEOUT_MS);
}

static esp_err_t start_measurement(i2c_master_dev_handle_t dev_handle) {
    uint8_t cmd[2] = {0x5C, 0x24};  // RH first, clock stretching enabled
    return i2c_master_transmit(dev_handle, cmd, 2, I2C_MASTER_TIMEOUT_MS);
}

static uint8_t shtc3_crc8(uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x31;
            else crc <<= 1;
        }
    }
    return crc;
}

static esp_err_t read_humidity(int *rh_out, uint8_t *raw_buf) {
    if (shtc3_crc8(raw_buf, 2) != raw_buf[2]) {
        return ESP_ERR_INVALID_CRC;
    }
    uint16_t raw = (raw_buf[0] << 8) | raw_buf[1];
    *rh_out = (int)(100.0f * raw / 65535.0f);
    return ESP_OK;
}

static esp_err_t read_temperature(int *rt_out, uint8_t *raw_buf) {
    if (shtc3_crc8(raw_buf, 2) != raw_buf[2]) {
        return ESP_ERR_INVALID_CRC;
    }
    uint16_t raw = (raw_buf[0] << 8) | raw_buf[1];
    *rt_out = (int)(-45.0f + 175.0f * raw / 65535.0f);
    return ESP_OK;
}

extern "C" void app_main(void) {
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = (gpio_num_t)I2C_MASTER_SDA_IO;
    bus_config.scl_io_num = (gpio_num_t)I2C_MASTER_SCL_IO;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
        i2c_master_bus_handle_t bus;

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));


    // ── add sensor device to the shared bus ──────────────────
    i2c_device_config_t sensor_cfg = {};
    sensor_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    sensor_cfg.device_address  = SENSOR_ADDR;
    sensor_cfg.scl_speed_hz    = I2C_MASTER_FREQ_HZ;

    i2c_master_dev_handle_t sensor_handle;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &sensor_cfg, &sensor_handle));

    lcd.init(bus);
    lcd.setRGB(255,0,90);
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1) {
        int rt_out = 0, rh_out = 0;
        uint8_t buf[6];
	char line[17];
	sensor_wakeup(sensor_handle);
        vTaskDelay(pdMS_TO_TICKS(10));
	start_measurement(sensor_handle);
        vTaskDelay(pdMS_TO_TICKS(55));

	esp_err_t ret = i2c_master_receive(sensor_handle, buf, 6, I2C_MASTER_TIMEOUT_MS);
        if (ret == ESP_OK) {
            read_humidity(&rh_out, &buf[0]);
            read_temperature(&rt_out, &buf[3]);
	    lcd.setCursor(0,0);
	    snprintf(line, sizeof(line), "Temp: %3dC", rt_out);

	    lcd.printstr(line);

	    lcd.setCursor(0,1);
	    snprintf(line, sizeof(line), "Hum: %3d%%", rh_out);
	    lcd.printstr(line);
        }

        sensor_sleep(sensor_handle);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

