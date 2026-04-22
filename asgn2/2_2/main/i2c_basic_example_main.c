#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "example";

#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          10000
#define I2C_MASTER_TIMEOUT_MS       1000
#define SENSOR_ADDR                 0x70

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
        ESP_LOGE(TAG, "Humidity CRC fail: got 0x%02X expected 0x%02X",
                 shtc3_crc8(raw_buf, 2), raw_buf[2]);
        return ESP_ERR_INVALID_CRC;
    }
    uint16_t raw = (raw_buf[0] << 8) | raw_buf[1];
    *rh_out = (int)(100.0f * raw / 65535.0f);
    return ESP_OK;
}

static esp_err_t read_temperature(int *rt_out, uint8_t *raw_buf) {
    if (shtc3_crc8(raw_buf, 2) != raw_buf[2]) {
        ESP_LOGE(TAG, "Temperature CRC fail: got 0x%02X expected 0x%02X",
                 shtc3_crc8(raw_buf, 2), raw_buf[2]);
        return ESP_ERR_INVALID_CRC;
    }
    uint16_t raw = (raw_buf[0] << 8) | raw_buf[1];
    *rt_out = (int)(-45.0f + 175.0f * raw / 65535.0f);
    return ESP_OK;
}

static void i2c_master_init(i2c_master_bus_handle_t *bus_handle, i2c_master_dev_handle_t *dev_handle) {
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = 7,
        .scl_io_num = 8,
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

void app_main(void) {
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    i2c_master_init(&bus_handle, &dev_handle);
    ESP_LOGI(TAG, "I2C initialized successfully");
    vTaskDelay(pdMS_TO_TICKS(50));

    while (1) {
        int rt_out = 0, rh_out = 0;
        uint8_t buf[6];

        esp_err_t ret = sensor_wakeup(dev_handle);
        ESP_LOGI(TAG, "Wakeup: %s", esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(2));

        ret = start_measurement(dev_handle);
        ESP_LOGI(TAG, "Measurement: %s", esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(15));

        ret = i2c_master_receive(dev_handle, buf, 6, I2C_MASTER_TIMEOUT_MS);
        ESP_LOGI(TAG, "Read: %s | bytes: %02X %02X %02X %02X %02X %02X",
                 esp_err_to_name(ret), buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

        if (ret == ESP_OK) {
            read_humidity(&rh_out, &buf[0]);
            read_temperature(&rt_out, &buf[3]);
            int temp_f = (rt_out * 9 / 5) + 32;
            printf("Temperature is %dC (or %dF) and Humidity is %d%%\n", rt_out, temp_f, rh_out);
        }

        sensor_sleep(dev_handle);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
