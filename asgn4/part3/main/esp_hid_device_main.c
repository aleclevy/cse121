/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <i2cdev.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include <driver/gpio.h>
#include <icm42670.h>

#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "esp_hidd.h"
#include "esp_hid_gap.h"

static const char *TAG = "HID_DEV_DEMO";

typedef struct
{
    TaskHandle_t task_hdl;
    esp_hidd_dev_t *hid_dev;
    uint8_t protocol_mode;
    uint8_t *buffer;
} local_param_t;

static local_param_t s_ble_hid_param = {0};
static bool s_connected = false;

const unsigned char mouseReportMap[] = {
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x02,                    // USAGE (Mouse)
    0xa1, 0x01,                    // COLLECTION (Application)

    0x09, 0x01,                    //   USAGE (Pointer)
    0xa1, 0x00,                    //   COLLECTION (Physical)

    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x19, 0x01,                    //     USAGE_MINIMUM (Button 1)
    0x29, 0x03,                    //     USAGE_MAXIMUM (Button 3)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x95, 0x03,                    //     REPORT_COUNT (3)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x95, 0x01,                    //     REPORT_COUNT (1)
    0x75, 0x05,                    //     REPORT_SIZE (5)
    0x81, 0x03,                    //     INPUT (Cnst,Var,Abs)

    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                    //     USAGE (X)
    0x09, 0x31,                    //     USAGE (Y)
    0x09, 0x38,                    //     USAGE (Wheel)
    0x15, 0x81,                    //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f,                    //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x95, 0x03,                    //     REPORT_COUNT (3)
    0x81, 0x06,                    //     INPUT (Data,Var,Rel)

    0xc0,                          //   END_COLLECTION
    0xc0                           // END_COLLECTION
};

#define PORT 0
#if defined(CONFIG_EXAMPLE_I2C_ADDRESS_GND)
#define I2C_ADDR ICM42670_I2C_ADDR_GND
#elif defined(CONFIG_EXAMPLE_I2C_ADDRESS_VCC)
#define I2C_ADDR ICM42670_I2C_ADDR_VCC
#else
#define I2C_ADDR ICM42670_I2C_ADDR_GND  // default: AD0 pin low
#endif
#ifndef APP_CPU_NUM
#define APP_CPU_NUM PRO_CPU_NUM
#endif
#define BOOT_BUTTON_GPIO 9
#define LOOP_MS     100
#define THRESH_BIT  3000
#define THRESH_LOT  10000
#define DELTA_BIT   5
#define DELTA_LOT   15
#define TIME_A2     20
#define TIME_A3     50
#define CLICK_THRESH 15000

void send_mouse(uint8_t buttons, char dx, char dy, char wheel)
{
    static uint8_t buffer[4] = {0};
    buffer[0] = buttons;
    buffer[1] = dx;
    buffer[2] = dy;
    buffer[3] = wheel;
    esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 0, buffer, 4);
}

void ble_hid_demo_task_mouse(void *pvParameters)
{
gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};
gpio_config(&io_conf);
    const char *TAG = "icm42670";

    icm42670_t dev = { 0 };
    ESP_ERROR_CHECK(
        icm42670_init_desc(&dev, I2C_ADDR, PORT, 7, 8));
    ESP_ERROR_CHECK(icm42670_init(&dev));

    ESP_ERROR_CHECK(icm42670_set_gyro_pwr_mode(&dev, ICM42670_GYRO_ENABLE_LN_MODE));
    ESP_ERROR_CHECK(icm42670_set_accel_pwr_mode(&dev, ICM42670_ACCEL_ENABLE_LN_MODE));

    ESP_ERROR_CHECK(icm42670_set_accel_lpf(&dev, ICM42670_ACCEL_LFP_53HZ));
    ESP_ERROR_CHECK(icm42670_set_gyro_lpf(&dev, ICM42670_GYRO_LFP_53HZ));
    ESP_ERROR_CHECK(icm42670_set_accel_odr(&dev, ICM42670_ACCEL_ODR_200HZ));
    ESP_ERROR_CHECK(icm42670_set_gyro_odr(&dev, ICM42670_GYRO_ODR_200HZ));
    ESP_ERROR_CHECK(icm42670_set_accel_fsr(&dev, ICM42670_ACCEL_RANGE_2G));
    ESP_ERROR_CHECK(icm42670_set_gyro_fsr(&dev, ICM42670_GYRO_RANGE_2000DPS));

    int16_t accel_x, accel_y, accel_z;
    int32_t x_tilt_ms = 0;
    int32_t y_tilt_ms = 0;
    int8_t last_x_dir = 0;
    int8_t last_y_dir = 0;

    while (1)
    {
        icm42670_read_raw_data(&dev, ICM42670_REG_ACCEL_DATA_X1, &accel_x);
        icm42670_read_raw_data(&dev, ICM42670_REG_ACCEL_DATA_Y1, &accel_y);
        icm42670_read_raw_data(&dev, ICM42670_REG_ACCEL_DATA_Z1, &accel_z);

        int8_t x_dir = 0, y_dir = 0;
        int x_base = 0, y_base = 0;

        if (accel_x > THRESH_LOT) {
            ESP_LOGI(TAG, "A_LOT_LEFT");
            x_dir = 1; x_base = DELTA_LOT;
        } else if (accel_x > THRESH_BIT) {
            ESP_LOGI(TAG, "A_BIT_LEFT");
            x_dir = 1; x_base = DELTA_BIT;
        } else if (accel_x < -THRESH_LOT) {
            ESP_LOGI(TAG, "A_LOT_RIGHT");
            x_dir = -1; x_base = DELTA_LOT;
        } else if (accel_x < -THRESH_BIT) {
            ESP_LOGI(TAG, "A_BIT_RIGHT");
            x_dir = -1; x_base = DELTA_BIT;
        }

        if (accel_y > THRESH_LOT) {
            ESP_LOGI(TAG, "A_LOT_UP");
            y_dir = 1; y_base = DELTA_LOT;
        } else if (accel_y > THRESH_BIT) {
            ESP_LOGI(TAG, "A_BIT_UP");
            y_dir = 1; y_base = DELTA_BIT;
        } else if (accel_y < -THRESH_LOT) {
            ESP_LOGI(TAG, "A_LOT_DOWN");
            y_dir = -1; y_base = DELTA_LOT;
        } else if (accel_y < -THRESH_BIT) {
            ESP_LOGI(TAG, "A_BIT_DOWN");
            y_dir = -1; y_base = DELTA_BIT;
        }

        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));

        if (x_dir != last_x_dir) {
            x_tilt_ms = 0; last_x_dir = x_dir;
        }
        if (y_dir != last_y_dir) {
            y_tilt_ms = 0; last_y_dir = y_dir;
        }
        if (x_dir != 0) x_tilt_ms += LOOP_MS;
        if (y_dir != 0) y_tilt_ms += LOOP_MS;

        int ax = (x_tilt_ms >= TIME_A3) ? 3 : (x_tilt_ms >= TIME_A2) ? 2 : 1;
        int ay = (y_tilt_ms >= TIME_A3) ? 3 : (y_tilt_ms >= TIME_A2) ? 2 : 1;

        int8_t dx = (int8_t)(x_dir * x_base * ax);
        int8_t dy = (int8_t)(-y_dir * y_base * ay);  // Y axis inverted on screen

        // Click detection (sharp face-down tilt)
        // accel_z goes very negative when board tilts face-down sharply
        static bool click_sent = false;
        if (accel_z < -CLICK_THRESH && !click_sent) {
            ESP_LOGI(TAG, "CLICK");
            if (s_connected) {
                send_mouse(0x01, 0, 0, 0);  // button down
                vTaskDelay(pdMS_TO_TICKS(50));
                send_mouse(0x00, 0, 0, 0);  // button up
            }
            click_sent = true;
        } else if (accel_z >= -CLICK_THRESH) {
            click_sent = false;
        }

        if (s_connected && (dx != 0 || dy != 0)) {
            send_mouse(0, dx, dy, 0);
        }

	// Boot button click (GPIO9, active low)
static bool btn_click_sent = false;
if (gpio_get_level(BOOT_BUTTON_GPIO) == 0 && !btn_click_sent) {
    ESP_LOGI(TAG, "BOOT BUTTON CLICK");
    if (s_connected) {
        send_mouse(0x01, 0, 0, 0);  // button down
        vTaskDelay(pdMS_TO_TICKS(50));
        send_mouse(0x00, 0, 0, 0);  // button up
    }
    btn_click_sent = true;
} else if (gpio_get_level(BOOT_BUTTON_GPIO) == 1) {
    btn_click_sent = false;
}
    }
}

static esp_hid_raw_report_map_t ble_report_maps[] = {
    {
        .data = mouseReportMap,
        .len = sizeof(mouseReportMap)
    },
};

static esp_hid_device_config_t ble_hid_config = {
    .vendor_id          = 0x16C0,
    .product_id         = 0x05DF,
    .version            = 0x0100,
    .device_name        = "ESP Mouse",
    .manufacturer_name  = "Espressif",
    .serial_number      = "1234567890",
    .report_maps        = ble_report_maps,
    .report_maps_len    = 1
};

void ble_hid_task_start_up(void)
{
    if (s_ble_hid_param.task_hdl) {
        return;
    }
    xTaskCreate(ble_hid_demo_task_mouse, "ble_hid_demo_task_mouse", 3 * 1024, NULL, configMAX_PRIORITIES - 3,
                &s_ble_hid_param.task_hdl);
}

void ble_hid_task_shut_down(void)
{
    if (s_ble_hid_param.task_hdl) {
        vTaskDelete(s_ble_hid_param.task_hdl);
        s_ble_hid_param.task_hdl = NULL;
    }
}

static void ble_hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;
    static const char *TAG = "HID_DEV_BLE";

    switch (event) {
    case ESP_HIDD_START_EVENT: {
        ESP_LOGI(TAG, "START");
        esp_hid_ble_gap_adv_start();
        break;
    }
    case ESP_HIDD_CONNECT_EVENT: {
        ESP_LOGI(TAG, "CONNECT");
        s_connected = true;
        ble_hid_task_start_up();
        break;
    }
    case ESP_HIDD_PROTOCOL_MODE_EVENT: {
        ESP_LOGI(TAG, "PROTOCOL MODE[%u]: %s", param->protocol_mode.map_index,
                 param->protocol_mode.protocol_mode ? "REPORT" : "BOOT");
        break;
    }
    case ESP_HIDD_CONTROL_EVENT: {
        ESP_LOGI(TAG, "CONTROL[%u]: %sSUSPEND", param->control.map_index,
                 param->control.control ? "EXIT_" : "");
        if (param->control.control) {
            ble_hid_task_start_up();
        } else {
            ble_hid_task_shut_down();
        }
        break;
    }
    case ESP_HIDD_OUTPUT_EVENT: {
        ESP_LOGI(TAG, "OUTPUT[%u]: %8s ID: %2u, Len: %d, Data:",
                 param->output.map_index, esp_hid_usage_str(param->output.usage),
                 param->output.report_id, param->output.length);
        ESP_LOG_BUFFER_HEX(TAG, param->output.data, param->output.length);
        break;
    }
    case ESP_HIDD_FEATURE_EVENT: {
        ESP_LOGI(TAG, "FEATURE[%u]: %8s ID: %2u, Len: %d, Data:",
                 param->feature.map_index, esp_hid_usage_str(param->feature.usage),
                 param->feature.report_id, param->feature.length);
        ESP_LOG_BUFFER_HEX(TAG, param->feature.data, param->feature.length);
        break;
    }
    case ESP_HIDD_DISCONNECT_EVENT: {
        ESP_LOGI(TAG, "DISCONNECT: %s",
                 esp_hid_disconnect_reason_str(esp_hidd_dev_transport_get(param->disconnect.dev),
                                               param->disconnect.reason));
        s_connected = false;
        ble_hid_task_shut_down();
        esp_hid_ble_gap_adv_start();
        break;
    }
    case ESP_HIDD_STOP_EVENT: {
        ESP_LOGI(TAG, "STOP");
        break;
    }
    default:
        break;
    }
}

void ble_hid_device_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_store_config_init(void);

void app_main(void)
{
    esp_err_t ret;
    ESP_ERROR_CHECK(i2cdev_init());
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "setting hid gap, mode:%d", HID_DEV_MODE);
    ret = esp_hid_gap_init(HID_DEV_MODE);
    ESP_ERROR_CHECK(ret);

    ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_MOUSE, ble_hid_config.device_name);
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "setting ble device");
    ESP_ERROR_CHECK(
        esp_hidd_dev_init(&ble_hid_config, ESP_HID_TRANSPORT_BLE, ble_hidd_event_callback, &s_ble_hid_param.hid_dev));

    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ret = esp_nimble_enable(ble_hid_device_host_task);
    if (ret) {
        ESP_LOGE(TAG, "esp_nimble_enable failed: %d", ret);
    }
}
