/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* i2c - Simple Example

   Simple I2C example that shows how to initialize I2C
   as well as reading and writing from and to registers for a sensor connected over I2C.

   The sensor used in this example is a MPU9250 inertial measurement unit.
*/
#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
static const char *TAG = "example";

#define I2C_MASTER_SCL_IO           CONFIG_I2C_MASTER_SCL       /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO           CONFIG_I2C_MASTER_SDA       /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              I2C_NUM_0                   /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ          CONFIG_I2C_MASTER_FREQUENCY /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0                           /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0                           /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       1000

#define SENSOR_ADDR         0x70        /*!< Address of the MPU9250 sensor */

static esp_err_t sensor_wakeup(i2c_master_dev_handle_t dev_handle){
    uint8_t cmd[2] = {0x35, 0x17};
    return i2c_master_transmit(dev_handle, cmd, 2, I2C_MASTER_TIMEOUT_MS);
}

static esp_err_t sensor_sleep(i2c_master_dev_handle_t dev_handle){
    uint8_t cmd[2] = {0xB0, 0x98};
    return i2c_master_transmit(dev_handle, cmd, 2, I2C_MASTER_TIMEOUT_MS);
}

static esp_err_t start_measurement(i2c_master_dev_handle_t dev_handle){
	uint8_t cmd[2] = {0x58, 0xE0};
	return i2c_master_transmit(dev_handle, cmd, 2, I2C_MASTER_TIMEOUT_MS);
}

static uint8_t shtc3_crc8(uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static esp_err_t read_temperature(i2c_master_dev_handle_t dev_handle, int *rt_out){
	uint8_t buf[3];
	esp_err_t ret = i2c_master_receive(dev_handle, buf, 3, I2C_MASTER_TIMEOUT_MS);
	if(ret != ESP_OK) return ret;
	//add checksum here
	if(shtc3_crc8(buf, 2) != buf[2]){
		return ESP_ERR_INVALID_CRC;
	}
	uint16_t raw = (buf[0] << 8) | buf[1];
	*rt_out = (int)(-45.0f +175.0f*(raw/65535.0f));
	return ESP_OK;
}

static esp_err_t read_humidity(i2c_master_dev_handle_t dev_handle, int *rh_out){
	uint8_t buf[3];
	esp_err_t ret = i2c_master_receive(dev_handle, buf, 3, I2C_MASTER_TIMEOUT_MS);
	if(ret != ESP_OK) return ret;
	//add checksum here
	if(shtc3_crc8(buf, 2) != buf[2]){
		return ESP_ERR_INVALID_CRC;
	}
	uint16_t raw = (buf[0] << 8) | buf[1];
	*rh_out = (int)(100.0f * raw)/(65535.0f);
	return ESP_OK;
}

static void i2c_master_init(i2c_master_bus_handle_t *bus_handle, i2c_master_dev_handle_t *dev_handle)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, bus_handle));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SENSOR_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(*bus_handle, &dev_config, dev_handle));
}

void app_main(void)
{

    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    i2c_master_init(&bus_handle, &dev_handle);
    ESP_LOGI(TAG, "I2C initialized successfully");
    while(1){
    	int rt_out, rh_out;
    	sensor_wakeup(dev_handle);
	vTaskDelay(pdMS_TO_TICKS(1));
    	start_measurement(dev_handle);
	vTaskDelay(pdMS_TO_TICKS(13));
	read_humidity(dev_handle, &rh_out);
	read_temperature(dev_handle, &rt_out);
	sensor_sleep(dev_handle);

	int temp_f = (rt_out * 9/5) +32;

	printf("Temperature is %dC (or %dF) and Humidity is %d%% \n", rt_out, temp_f, rh_out);
	vTaskDelay(pdMS_TO_TICKS(2000));
    }

    ESP_ERROR_CHECK(i2c_master_bus_rm_device(dev_handle));
    ESP_ERROR_CHECK(i2c_del_master_bus(bus_handle));
    ESP_LOGI(TAG, "I2C de-initialized successfully");
}
