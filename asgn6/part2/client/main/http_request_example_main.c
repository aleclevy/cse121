/* HTTP GET Example using plain POSIX sockets

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "stdio.h"
#include "sdkconfig.h"
#include "driver/i2c_master.h"

#define I2C_MASTER_NUM		I2C_NUM_0
#define I2C_MASTER_FREQ_HZ	10000
#define I2C_MASTER_TIMEOUT_MS       1000
#define SENSOR_ADDR                 0x70

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "sdkconfig.h"

/* Constants that aren't configurable in menuconfig */
#define WEB_SERVER "192.168.0.109"
#define WEB_PORT "1234"
#define WEB_PATH "/"

static const char *TAG = "example";

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

static void http_get_task(void *pvParameters)
{
    i2c_master_dev_handle_t dev_handle = (i2c_master_dev_handle_t)pvParameters;

    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[64];
char request[256];
int temp_f = 0;  // default in case sensor read fails
    while(1) {
 	int rt_out = 0, rh_out = 0;
        uint8_t buf[6];

        esp_err_t ret = sensor_wakeup(dev_handle);
        vTaskDelay(pdMS_TO_TICKS(2));

        ret = start_measurement(dev_handle);
        vTaskDelay(pdMS_TO_TICKS(15));

        ret = i2c_master_receive(dev_handle, buf, 6, I2C_MASTER_TIMEOUT_MS);

        if (ret == ESP_OK) {
            read_temperature(&rt_out, &buf[3]);
            temp_f = (rt_out * 9 / 5) + 32;
        }

snprintf(request, sizeof(request),
    "POST " WEB_PATH " HTTP/1.0\r\n"
    "Host: " WEB_SERVER ":" WEB_PORT "\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: %d\r\n"
    "\r\n"
    "%d",
    snprintf(NULL, 0, "%d", temp_f),  // calculate body length
    temp_f);
        int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);

        if(err != 0 || res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        /* Code to print the resolved IP.

           Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

        s = socket(res->ai_family, res->ai_socktype, 0);
        if(s < 0) {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... allocated socket");

        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "... connected");
        freeaddrinfo(res);

        if (write(s, request, strlen(request)) < 0) {
            ESP_LOGE(TAG, "... socket send failed");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... socket send success");

        struct timeval receiving_timeout;
        receiving_timeout.tv_sec = 5;
        receiving_timeout.tv_usec = 0;
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                sizeof(receiving_timeout)) < 0) {
            ESP_LOGE(TAG, "... failed to set socket receiving timeout");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... set socket receiving timeout success");

        /* Read HTTP response */
        do {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf)-1);
            for(int i = 0; i < r; i++) {
                putchar(recv_buf[i]);
            }
        } while(r > 0);

        ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d.", r, errno);
        close(s);
        for(int countdown = 10; countdown >= 0; countdown--) {
            ESP_LOGI(TAG, "%d... ", countdown);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        ESP_LOGI(TAG, "Starting again!");
    }
}

void app_main(void)
{
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    i2c_master_init(&bus_handle, &dev_handle);
    ESP_LOGI(TAG, "I2C initialized successfully");
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_ERROR_CHECK( nvs_flash_init() );
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
    xTaskCreate(&http_get_task, "http_get_task", 4096, (void*)dev_handle, 5, NULL);
}
