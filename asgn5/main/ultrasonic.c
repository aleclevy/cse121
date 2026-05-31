/*
 * Copyright (c) 2016 Ruslan V. Uss <unclerus@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of itscontributors
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file ultrasonic.c
 *
 * ESP-IDF driver for ultrasonic range meters, e.g. HC-SR04, HY-SRF05 and the like
 *
 * Ported from esp-open-rtos
 *
 * Copyright (c) 2016 Ruslan V. Uss <unclerus@gmail.com>
 *
 * BSD Licensed as described in the file LICENSE
 */
#define HELPER_TARGET_IS_ESP32 1
#include "ultrasonic.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <rom/ets_sys.h>

#define TRIGGER_LOW_DELAY 4
#define TRIGGER_HIGH_DELAY 10
#define PING_TIMEOUT 6000
#define ROUNDTRIP_M 5800.0f
#define ROUNDTRIP_CM 58
#define HALF_SPEED_OF_SOUND_AT_0C_M_S 165.7 // Half speed of sound in m/s at 0 degrees Celsius

#if HELPER_TARGET_IS_ESP32
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
#define PORT_ENTER_CRITICAL portENTER_CRITICAL(&mux)
#define PORT_EXIT_CRITICAL portEXIT_CRITICAL(&mux)

#elif HELPER_TARGET_IS_ESP8266
#define PORT_ENTER_CRITICAL portENTER_CRITICAL()
#define PORT_EXIT_CRITICAL portEXIT_CRITICAL()

#else
#error cannot identify the target
#endif

#define timeout_expired(start, len) ((esp_timer_get_time() - (start)) >= (len))

#define CHECK_ARG(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)
#define CHECK(x) do { esp_err_t __; if ((__ = x) != ESP_OK) return __; } while (0)
#define RETURN_CRITICAL(RES) do { PORT_EXIT_CRITICAL; return RES; } while(0)

#include <stdio.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "example";

#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          10000
#define I2C_MASTER_TIMEOUT_MS       1000
#define SENSOR_ADDR                 0x70

// FIXED - use an initializer
ultrasonic_sensor_t ultrasonic_dev = {
    .trigger_pin = 4,
    .echo_pin = 5  // set your echo pin too
};

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
    uint32_t distance;
    while (1) {
        int rt_out = 0, rh_out = 0;
        uint8_t buf[6];

        esp_err_t ret = sensor_wakeup(dev_handle);
        vTaskDelay(pdMS_TO_TICKS(2));
        ret = start_measurement(dev_handle);
        vTaskDelay(pdMS_TO_TICKS(15));

        ret = i2c_master_receive(dev_handle, buf, 6, I2C_MASTER_TIMEOUT_MS);

        if (ret == ESP_OK) {
            read_humidity(&rh_out, &buf[0]);
            read_temperature(&rt_out, &buf[3]);
            int temp_f = (rt_out * 9 / 5) + 32;
            printf("Temperature is %dC (or %dF) and Humidity is %d%%\n", rt_out, temp_f, rh_out);
        }
	
	ultrasonic_init(&ultrasonic_dev);
	ultrasonic_measure_cm_temp_compensated(&ultrasonic_dev, 30, &distance, (float)rt_out);
	printf("Distance %lucm at %dC\n", distance, rt_out);
        sensor_sleep(dev_handle);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

esp_err_t ultrasonic_init(const ultrasonic_sensor_t *dev)
{
    CHECK_ARG(dev);

    CHECK(gpio_set_direction(dev->trigger_pin, GPIO_MODE_OUTPUT));
    CHECK(gpio_set_direction(dev->echo_pin, GPIO_MODE_INPUT));

    return gpio_set_level(dev->trigger_pin, 0);
}


esp_err_t ultrasonic_measure_raw(const ultrasonic_sensor_t *dev, uint32_t max_time_us, uint32_t *time_us)
{
    CHECK_ARG(dev && time_us);

    PORT_ENTER_CRITICAL;

    // Ping: Low for 2..4 us, then high 10 us
    CHECK(gpio_set_level(dev->trigger_pin, 0));
    ets_delay_us(TRIGGER_LOW_DELAY);
    CHECK(gpio_set_level(dev->trigger_pin, 1));
    ets_delay_us(TRIGGER_HIGH_DELAY);
    CHECK(gpio_set_level(dev->trigger_pin, 0));

    // Previous ping isn't ended
    if (gpio_get_level(dev->echo_pin))
        RETURN_CRITICAL(ESP_ERR_ULTRASONIC_PING);

    // Wait for echo
    int64_t start = esp_timer_get_time();
    while (!gpio_get_level(dev->echo_pin))
    {
        if (timeout_expired(start, PING_TIMEOUT))
            RETURN_CRITICAL(ESP_ERR_ULTRASONIC_PING_TIMEOUT);
    }

    // got echo, measuring
    int64_t echo_start = esp_timer_get_time();
    int64_t time = echo_start;
    while (gpio_get_level(dev->echo_pin))
    {
        time = esp_timer_get_time();
        if (timeout_expired(echo_start, max_time_us))
            RETURN_CRITICAL(ESP_ERR_ULTRASONIC_ECHO_TIMEOUT);
    }
    PORT_EXIT_CRITICAL;

    *time_us = time - echo_start;

    return ESP_OK;
}

esp_err_t ultrasonic_measure(const ultrasonic_sensor_t *dev, float max_distance, float *distance)
{
    CHECK_ARG(dev && distance);

    uint32_t time_us;
    CHECK(ultrasonic_measure_raw(dev, max_distance * ROUNDTRIP_M, &time_us));
    *distance = time_us / ROUNDTRIP_M;

    return ESP_OK;
}

esp_err_t ultrasonic_measure_cm(const ultrasonic_sensor_t *dev, uint32_t max_distance, uint32_t *distance)
{
    CHECK_ARG(dev && distance);

    uint32_t time_us;
    CHECK(ultrasonic_measure_raw(dev, max_distance * ROUNDTRIP_CM, &time_us));
    *distance = time_us / ROUNDTRIP_CM;

    return ESP_OK;
}

esp_err_t ultrasonic_measure_temp_compensated(const ultrasonic_sensor_t *dev, float max_distance, float *distance, float temperature_c)
{
    CHECK_ARG(dev && distance);

    // Calculate half (because of roundtrip) speed of sound in m/us based on temperature
    float speed_of_sound = (HALF_SPEED_OF_SOUND_AT_0C_M_S + 0.6 * temperature_c) / 1000000; // Convert m/s to m/us

    uint32_t time_us;
    // Adjust max_time_us based on the recalculated speed of sound
    CHECK(ultrasonic_measure_raw(dev, max_distance / speed_of_sound, &time_us));
    // Calculate distance using the temperature-compensated speed of sound
    *distance = time_us * speed_of_sound;

    return ESP_OK;
}

esp_err_t ultrasonic_measure_cm_temp_compensated(const ultrasonic_sensor_t *dev, uint32_t max_distance, uint32_t *distance, float temperature_c)
{
    CHECK_ARG(dev && distance);

    // Calculate half (because of roundtrip) speed of sound in cm/us based on temperature
    float speed_of_sound_cm_us = ((HALF_SPEED_OF_SOUND_AT_0C_M_S + 0.6 * temperature_c) * 100) / 1000000; // Convert m/s to cm/us

    uint32_t time_us;
    // Adjust max_time_us based on the recalculated speed of sound in cm
    CHECK(ultrasonic_measure_raw(dev, max_distance * 100 / speed_of_sound_cm_us, &time_us));
    // Calculate distance using the temperature-compensated speed of sound, converting result to cm
    *distance = time_us * speed_of_sound_cm_us;

    return ESP_OK;
}
