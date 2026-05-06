#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_hidd.h"
#include "esp_hid_gap.h"
#include <icm42670.h>

static const char *TAG = "LAB4_3";

// ── BLE HID boilerplate (same as your 4.2) ──────────────────────────────────

typedef struct {
    TaskHandle_t task_hdl;
    esp_hidd_dev_t *hid_dev;
    uint8_t protocol_mode;
    uint8_t *buffer;
} local_param_t;

static local_param_t s_ble_hid_param = {0};
static bool s_connected = false;

const unsigned char mouseReportMap[] = {
    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01,
    0x09, 0x01, 0xa1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
    0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x03,
    0x05, 0x01,
    0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7f,
    0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xc0, 0xc0
};

static esp_hid_raw_report_map_t ble_report_maps[] = {
    { .data = mouseReportMap, .len = sizeof(mouseReportMap) }
};

static esp_hid_device_config_t ble_hid_config = {
    .vendor_id        = 0x16C0,
    .product_id       = 0x05DF,
    .version          = 0x0100,
    .device_name      = "ESP Mouse",
    .manufacturer_name = "Espressif",
    .serial_number    = "1234567890",
    .report_maps      = ble_report_maps,
    .report_maps_len  = 1
};

void send_mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel)
{
    static uint8_t buffer[4] = {0};
    buffer[0] = buttons;
    buffer[1] = (uint8_t)dx;
    buffer[2] = (uint8_t)dy;
    buffer[3] = (uint8_t)wheel;
    esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 0, buffer, 4);
}

// ── IMU + mouse control task ─────────────────────────────────────────────────

#define PORT 0
#define I2C_ADDR ICM42670_I2C_ADDR_GND  // adjust if needed

// Tilt thresholds (raw int16, range ±32768 at ±2g)
#define THRESH_BIT   3000
#define THRESH_LOT   10000

// Base deltas per level
#define DELTA_BIT    5
#define DELTA_LOT    15

// Time thresholds for acceleration multiplier (ms)
#define TIME_A2      10
#define TIME_A3      50

// Click: sharp downward tilt on Z
#define CLICK_THRESH  15000   // accel_z very negative = tilted face-down

#ifndef APP_CPU_NUM
#define APP_CPU_NUM PRO_CPU_NUM
#endif

void mouse_imu_task(void *pvParameters)
{
    // ── Init IMU ──────────────────────────────────────────────────────────────
    icm42670_t dev = {0};
    ESP_ERROR_CHECK(icm42670_init_desc(&dev, I2C_ADDR, PORT,
                                        CONFIG_EXAMPLE_I2C_MASTER_SDA,
                                        CONFIG_EXAMPLE_I2C_MASTER_SCL));
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

    // Per-axis tilt duration trackers (ms)
    int32_t x_tilt_ms = 0;
    int32_t y_tilt_ms = 0;
    int8_t  last_x_dir = 0;  // -1, 0, +1
    int8_t  last_y_dir = 0;

    const int LOOP_MS = 20;  // poll every 20ms

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));

        icm42670_read_raw_data(&dev, ICM42670_REG_ACCEL_DATA_X1, &accel_x);
        icm42670_read_raw_data(&dev, ICM42670_REG_ACCEL_DATA_Y1, &accel_y);
        icm42670_read_raw_data(&dev, ICM42670_REG_ACCEL_DATA_Z1, &accel_z);

        // ── Direction + level detection ───────────────────────────────────────
        int8_t x_dir = 0, y_dir = 0;
        int    x_base = 0, y_base = 0;

        if (accel_x > THRESH_LOT) {
            x_dir = 1;  x_base = DELTA_LOT;
            ESP_LOGI(TAG, "A_LOT_LEFT");
        } else if (accel_x > THRESH_BIT) {
            x_dir = 1;  x_base = DELTA_BIT;
            ESP_LOGI(TAG, "A_BIT_LEFT");
        } else if (accel_x < -THRESH_LOT) {
            x_dir = -1; x_base = DELTA_LOT;
            ESP_LOGI(TAG, "A_LOT_RIGHT");
        } else if (accel_x < -THRESH_BIT) {
            x_dir = -1; x_base = DELTA_BIT;
            ESP_LOGI(TAG, "A_BIT_RIGHT");
        }

        if (accel_y > THRESH_LOT) {
            y_dir = 1;  y_base = DELTA_LOT;
            ESP_LOGI(TAG, "A_LOT_UP");
        } else if (accel_y > THRESH_BIT) {
            y_dir = 1;  y_base = DELTA_BIT;
            ESP_LOGI(TAG, "A_BIT_UP");
        } else if (accel_y < -THRESH_LOT) {
            y_dir = -1; y_base = DELTA_LOT;
            ESP_LOGI(TAG, "A_LOT_DOWN");
        } else if (accel_y < -THRESH_BIT) {
            y_dir = -1; y_base = DELTA_BIT;
            ESP_LOGI(TAG, "A_BIT_DOWN");
        }

        // ── Time-based multiplier ─────────────────────────────────────────────
        // Reset timer if direction changed
        if (x_dir != last_x_dir) { x_tilt_ms = 0; last_x_dir = x_dir; }
        if (y_dir != last_y_dir) { y_tilt_ms = 0; last_y_dir = y_dir; }

        if (x_dir != 0) x_tilt_ms += LOOP_MS;
        if (y_dir != 0) y_tilt_ms += LOOP_MS;

        int ax = (x_tilt_ms >= TIME_A3) ? 3 : (x_tilt_ms >= TIME_A2) ? 2 : 1;
        int ay = (y_tilt_ms >= TIME_A3) ? 3 : (y_tilt_ms >= TIME_A2) ? 2 : 1;

        int8_t dx = (int8_t)(x_dir * x_base * ax);
        int8_t dy = (int8_t)(-y_dir * y_base * ay);  // Y axis inverted on screen

        // ── Click detection (sharp face-down tilt) ────────────────────────────
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

        // ── Send mouse movement ───────────────────────────────────────────────
        if (s_connected && (dx != 0 || dy != 0)) {
            send_mouse(0, dx, dy, 0);
        }
    }
}

// ── BLE event callback ───────────────────────────────────────────────────────

static void ble_hidd_event_callback(void *handler_args, esp_event_base_t base,
                                     int32_t id, void *event_data)
{
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "BLE HID started");
        esp_hid_ble_gap_adv_start();
        break;
    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(TAG, "Connected");
        s_connected = true;
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        ESP_LOGI(TAG, "Disconnected: %s",
                 esp_hid_disconnect_reason_str(
                     esp_hidd_dev_transport_get(param->disconnect.dev),
                     param->disconnect.reason));
        s_connected = false;
        esp_hid_ble_gap_adv_start();
        break;
    case ESP_HIDD_STOP_EVENT:
        ESP_LOGI(TAG, "BLE HID stopped");
        break;
    default:
        break;
    }
}

// ── app_main ─────────────────────────────────────────────────────────────────

void app_main(void)
{
    esp_err_t ret;

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Init I2C bus for IMU
    ESP_ERROR_CHECK(i2cdev_init());

    // Init BLE HID
    ESP_ERROR_CHECK(esp_hid_gap_init(HID_DEV_MODE));
    ESP_ERROR_CHECK(esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_MOUSE,
                                              ble_hid_config.device_name));
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler));
    ESP_ERROR_CHECK(esp_hidd_dev_init(&ble_hid_config, ESP_HID_TRANSPORT_BLE,
                                      ble_hidd_event_callback,
                                      &s_ble_hid_param.hid_dev));

    // Start IMU + mouse task
    xTaskCreatePinnedToCore(mouse_imu_task, "mouse_imu_task",
                            configMINIMAL_STACK_SIZE * 8,
                            NULL, 5, NULL, APP_CPU_NUM);
}
