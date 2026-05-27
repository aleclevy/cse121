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
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "sdkconfig.h"

/* Constants that aren't configurable in menuconfig */
#define WEB_SERVER "192.168.0.109"
#define WEB_PORT "1234"
#define WEB_PATH "/"
#define WTTR_SERVER "wttr.in"
#define WTTR_PORT "80"

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
/* ── Network helper: open a TCP socket to host:port ─────────────────────── */
 
static int open_socket(const char *host, const char *port) {
    const struct addrinfo hints = {
        .ai_family   = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;
 
    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed for %s err=%d", host, err);
        return -1;
    }
 
    int s = socket(res->ai_family, res->ai_socktype, 0);
    if (s < 0) {
        ESP_LOGE(TAG, "Failed to allocate socket for %s", host);
        freeaddrinfo(res);
        return -1;
    }
 
    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "Socket connect failed for %s errno=%d", host, errno);
        close(s);
        freeaddrinfo(res);
        return -1;
    }
 
    freeaddrinfo(res);
 
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
 
    return s;
}
 
/* ── Network helper: send request, return body in out_buf ───────────────── */
 
static int http_transaction(const char *host, const char *port,
                             const char *request,
                             char *out_buf, size_t out_buf_len) {
    int s = open_socket(host, port);
    if (s < 0) return -1;
 
    if (write(s, request, strlen(request)) < 0) {
        ESP_LOGE(TAG, "socket send failed errno=%d", errno);
        close(s);
        return -1;
    }
 
    /* Read full response into a temp buffer */
    char tmp[512] = {0};
    int total = 0, r = 0;
    do {
        r = read(s, tmp + total, sizeof(tmp) - 1 - total);
        if (r > 0) total += r;
    } while (r > 0 && total < (int)sizeof(tmp) - 1);
    close(s);
 
    tmp[total] = '\0';
 
    /* Strip HTTP headers — body starts after \r\n\r\n */
    char *body = strstr(tmp, "\r\n\r\n");
    if (!body) {
        ESP_LOGE(TAG, "No header separator found in response");
        return -1;
    }
    body += 4;
 
    strncpy(out_buf, body, out_buf_len - 1);
    out_buf[out_buf_len - 1] = '\0';
 
    /* Trim trailing whitespace/newlines */
    int len = strlen(out_buf);
    while (len > 0 && (out_buf[len-1] == '\r' || out_buf[len-1] == '\n'
                        || out_buf[len-1] == ' ')) {
        out_buf[--len] = '\0';
    }
 
    return 0;
}
 
 
static void http_get_task(void *pvParameters) {
    i2c_master_dev_handle_t dev_handle = (i2c_master_dev_handle_t)pvParameters;
 
    char get_location_req[256];
    char post_temp_req[256];
    char wttr_req[256];
 
    char location[64]    = {0};
    char outdoor_tmp[64] = {0};
 
    snprintf(get_location_req, sizeof(get_location_req),
        "GET /location HTTP/1.0\r\n"
        "Host: " WEB_SERVER ":" WEB_PORT "\r\n"
        "User-Agent: esp-idf/1.0 esp32\r\n"
        "Connection: close\r\n"
        "\r\n");
 
    while (1) {
        int rt_out = 0;
        uint8_t buf[6];
 
        sensor_wakeup(dev_handle);
        vTaskDelay(pdMS_TO_TICKS(2));
        start_measurement(dev_handle);
        vTaskDelay(pdMS_TO_TICKS(15));
 
        int temp_f = 0;
        if (i2c_master_receive(dev_handle, buf, 6, I2C_MASTER_TIMEOUT_MS) == ESP_OK) {
            if (read_temperature(&rt_out, &buf[3]) == ESP_OK) {
                temp_f = (rt_out * 9 / 5) + 32;
                ESP_LOGI(TAG, "Indoor temp: %d °F", temp_f);
            }
        }
        sensor_sleep(dev_handle);
 
        if (http_transaction(WEB_SERVER, WEB_PORT,
                             get_location_req,
                             location, sizeof(location)) == 0) {
            ESP_LOGI(TAG, "Location: %s", location);
        } else {
            snprintf(location, sizeof(location), "Santa Cruz");  // fallback
        }
// after http_transaction fills location[]
// replace space with + for the URL
for (int i = 0; location[i]; i++) {
    if (location[i] == ' ') location[i] = '+';
} 
        // wttr.in?format=j1 returns JSON; using format=1 gives a plain text
        // one-line summary. We use %t to get just the temperature.
        snprintf(wttr_req, sizeof(wttr_req),
            "GET /%s?format=3 HTTP/1.0\r\n"
	    "Host: "WTTR_SERVER"\r\n"
            "User-Agent: esp-idf/1.0 esp32\r\n"
            "Connection: close\r\n"
            "\r\n",
            location);
 
        if (http_transaction(WTTR_SERVER, WTTR_PORT,
                             wttr_req,
                             outdoor_tmp, sizeof(outdoor_tmp)) == 0) {
            ESP_LOGI(TAG, "Outdoor temp at %s: %s", location, outdoor_tmp);
        }
 
        int body_len = snprintf(NULL, 0, "%d%s", temp_f,outdoor_tmp);
        snprintf(post_temp_req, sizeof(post_temp_req),
            "POST " WEB_PATH " HTTP/1.0\r\n"
            "Host: " WEB_SERVER ":" WEB_PORT "\r\n"
            "User-Agent: esp-idf/1.0 esp32\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%d%s",
            body_len, temp_f, outdoor_tmp);
 
        char discard[64];
        http_transaction(WEB_SERVER, WEB_PORT, post_temp_req,
                         discard, sizeof(discard));
        ESP_LOGI(TAG, "Posted indoor temp %d °F to server", temp_f);
 
        for (int i = 10; i >= 0; i--) {
            ESP_LOGI(TAG, "%d...", i);
            vTaskDelay(pdMS_TO_TICKS(1000));
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
