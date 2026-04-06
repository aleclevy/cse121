 
/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "sdkconfig.h"


/* Use project configuration menu (idf.py menuconfig) to choose the GPIO to blink,
   or you can edit the following line and set a number here.
*/
#define BLINK_GPIO GPIO_NUM_2

void blink_task(void *pvParameter){
	gpio_reset_pin(BLINK_GPIO);
	gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
	printf("entering blink_test");
	while(1){
		printf("loop");
		gpio_set_level(BLINK_GPIO, 1);
		vTaskDelay(500 / portTICK_PERIOD_MS);
		gpio_set_level(BLINK_GPIO, 0);
		vTaskDelay(500 / portTICK_PERIOD_MS);
	}

}
void app_main(void)
{

    /* Configure the peripheral according to the LED type */
	xTaskCreate(blink_task, "blink_task", 2048, NULL, 5, NULL);
}
 
