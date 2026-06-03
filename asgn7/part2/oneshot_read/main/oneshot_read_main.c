/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const static char *TAG = "EXAMPLE";

string const morseCode[] = {".-", "-...", "-.-.", "-..", ".", "..-.",
    "--.", "....", "..", ".---", "-.-", ".-..", "--", "-.", "---", ".--.", "--.-",
    ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--.."};//morse code from A to Z

/*---------------------------------------------------------------
  ADC General Macros
  ---------------------------------------------------------------*/
// ADC1 Channels
#if CONFIG_IDF_TARGET_ESP32
#define EXAMPLE_ADC1_CHAN0 ADC_CHANNEL_0
#define EXAMPLE_ADC1_CHAN1 ADC_CHANNEL_1
#else
#define EXAMPLE_ADC1_CHAN0 ADC_CHANNEL_2
#define EXAMPLE_ADC1_CHAN1 ADC_CHANNEL_3
#endif

#if (SOC_ADC_PERIPH_NUM >= 2) && !CONFIG_IDF_TARGET_ESP32C3
/**
 * On ESP32C3, ADC2 is no longer supported, due to its HW limitation.
 * Search for errata on espressif website for more details.
 */
#define EXAMPLE_USE_ADC2 1
#endif

#if EXAMPLE_USE_ADC2
// ADC2 Channels
#define EXAMPLE_ADC2_CHAN0 ADC_CHANNEL_0
#endif // #if EXAMPLE_USE_ADC2

#define EXAMPLE_ADC_ATTEN ADC_ATTEN_DB_12

static int adc_raw[2][10];
static int voltage[2][10];
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel,
                                         adc_atten_t atten,
                                         adc_cali_handle_t *out_handle);
static void example_adc_calibration_deinit(adc_cali_handle_t handle);

void app_main(void) {
  //-------------ADC1 Init---------------//
  adc_oneshot_unit_handle_t adc1_handle;
  adc_oneshot_unit_init_cfg_t init_config1 = {
      .unit_id = ADC_UNIT_1,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

  //-------------ADC1 Config---------------//
  adc_oneshot_chan_cfg_t config = {
      .atten = EXAMPLE_ADC_ATTEN,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  ESP_ERROR_CHECK(
      adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN0, &config));
  ESP_ERROR_CHECK(
      adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN1, &config));

  //-------------ADC1 Calibration Init---------------//
  adc_cali_handle_t adc1_cali_chan0_handle = NULL;
  adc_cali_handle_t adc1_cali_chan1_handle = NULL;
  bool do_calibration1_chan0 =
      example_adc_calibration_init(ADC_UNIT_1, EXAMPLE_ADC1_CHAN0,
                                   EXAMPLE_ADC_ATTEN, &adc1_cali_chan0_handle);
  bool do_calibration1_chan1 =
      example_adc_calibration_init(ADC_UNIT_1, EXAMPLE_ADC1_CHAN1,
                                   EXAMPLE_ADC_ATTEN, &adc1_cali_chan1_handle);

#define UNIT_MS 100
#define THRESHOLD_RAW 1500
#define DOT_MS 150

#define DOT_DASH_THRESHOLD 300
#define LETTER_GAP_THRESHOLD 300
#define WORD_GAP_THRESHOLD 750
  ESP_ERROR_CHECK(
      adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN1, &adc_raw[0][1]));

  bool last_state = (adc_raw[0][1] > THRESHOLD_RAW);

  int64_t last_change_us = esp_timer_get_time();
  while (1) {
    char sentence[];
    char morseRaw[];
    while (1) {
      ESP_ERROR_CHECK(
          adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN1, &adc_raw[0][1]));
      bool lit = (adc_raw[0][1] > THRESHOLD_RAW);
      int64_t now_us = esp_timer_get_time();
      if (lit != last_state) {
        int64_t duration_ms = (now_us - last_change_us) / 1000;
        if (!lit) {
          // ON period ended
          ESP_LOGI(TAG, "%c (%lld ms)",
                   duration_ms < DOT_DASH_THRESHOLD ? '.' : '-', duration_ms);
        } else {
          ESP_LOGI(TAG, "OFF GAP %lld ms", duration_ms);

          if (duration_ms < LETTER_GAP_THRESHOLD) {
            // same letter
          } else if (duration_ms < WORD_GAP_THRESHOLD) {
            ESP_LOGI(TAG, "LETTER END");
          } else {
            ESP_LOGI(TAG, "WORD END");
          }
        }
        last_state = lit;
        last_change_us = now_us;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
  }
  // Tear Down
  ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
  if (do_calibration1_chan0) {
    example_adc_calibration_deinit(adc1_cali_chan0_handle);
  }
  if (do_calibration1_chan1) {
    example_adc_calibration_deinit(adc1_cali_chan1_handle);
  }
}

/*conversion of morse code to english text*/
char[] convertToEnglish_Text(char str[])
{

     char word[] =""; //To form one word from input text
     char output[]="";
     int start=0;
     register int i=0;

    for(i=0;i<text.length();++i)
    {
        if(text.substr(i,1) == " ")//when three spaces trace, word send to convertToEnglish_Word
        {
            word=text.substr(start,i-start);//for content of one word
            output+=convertToEnglish_Word(word)+' ';
            start=i+1;//to kerp trace of text

        }


    }
    word=text.substr(start,i-start);//for last word
    output+=convertToEnglish_Word(word);
    cout<<"English Text is \""<<output<<"\""<<endl;


}
char[] convertToEnglish_Word(char morse[])
{
    string output = "";
    string currentLetter = "";
    istringstream ss(morse);//string stream

    size_t const characters = 26;


    while(ss >> currentLetter)//when string extract till space
    {

        size_t index = 0;
        while(currentLetter != morseCode[index] && index < characters)//get the index in array where this code present
        {
            ++index;
        }

        output += 'A' + index;//add the index to get appropriate character
    }


    return output;
}

/*---------------------------------------------------------------
  ADC Calibration
  ---------------------------------------------------------------*/
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel,
                                         adc_atten_t atten,
                                         adc_cali_handle_t *out_handle) {
  adc_cali_handle_t handle = NULL;
  esp_err_t ret = ESP_FAIL;
  bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  if (!calibrated) {
    ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
      calibrated = true;
    }
  }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  if (!calibrated) {
    ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
      calibrated = true;
    }
  }
#endif

  *out_handle = handle;
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Calibration Success");
  } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
    ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
  } else {
    ESP_LOGE(TAG, "Invalid arg or no memory");
  }

  return calibrated;
}

static void example_adc_calibration_deinit(adc_cali_handle_t handle) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
  ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
  ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}
