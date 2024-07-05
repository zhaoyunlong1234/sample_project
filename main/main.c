#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "protocol_examples_utils.h"
#include "esp_tls.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

#include "esp_http_client.h"

#include "driver/gpio.h"
#include "cJSON.h"
#include "led_strip.h"
#include "sdkconfig.h"

#define GPIO_INPUT_PIN GPIO_NUM_0
#define MAX_HTTP_OUTPUT_BUFFER 2048
#define BLINK_GPIO CONFIG_BLINK_GPIO

static const char *TAG = "HTTP_CLIENT";

static int temp = 0;
static char *name;
static char *text;
static char *wind_class;
static int rh;

static uint8_t s_led_state = 0;
static led_strip_handle_t led_strip;

static uint8_t base_r = 0;
static uint8_t base_g = 0;
static uint8_t base_b = 0;

SemaphoreHandle_t date_semaphore;
SemaphoreHandle_t json_done;

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            // Clean the buffer in case of a new request
            if (output_len == 0 && evt->user_data) {
                // we are just starting to copy the output data into the use
                memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
            }
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                int copy_len = 0;
                if (evt->user_data) {
                    // The last byte in evt->user_data is kept for the NULL character in case of out-of-bound access.
                    copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                    if (copy_len) {
                        memcpy(evt->user_data + output_len, evt->data, copy_len);
                    }
                } else {
                    int content_len = esp_http_client_get_content_length(evt->client);
                    if (output_buffer == NULL) {
                        // We initialize output_buffer with 0 because it is used by strlen() and similar functions therefore should be null terminated.
                        output_buffer = (char *) calloc(content_len + 1, sizeof(char));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    copy_len = MIN(evt->data_len, (content_len - output_len));
                    if (copy_len) {
                        memcpy(output_buffer + output_len, evt->data, copy_len);
                    }
                }
                output_len += copy_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            esp_http_client_set_header(evt->client, "From", "user@example.com");
            esp_http_client_set_header(evt->client, "Accept", "text/html");
            esp_http_client_set_redirection(evt->client);
            break;
    }
    return ESP_OK;
}

static void http_rest_with_url(void)
{
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
    
    esp_http_client_config_t config = {
        .url = "http://api.map.baidu.com/weather/v1/?district_id=310115&data_type=all&ak=X7FPVuwSRtZjx6aoSmFPWgu8xXPEx2PW",
        .event_handler = _http_event_handler,
        .user_data = local_response_buffer,        // Pass address of local buffer to get response
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // GET
    esp_err_t err = esp_http_client_perform(client);

    cJSON *root = cJSON_Parse(local_response_buffer);
    cJSON *result = cJSON_GetObjectItem(root,"result");
    cJSON *location = cJSON_GetObjectItem(result,"location");
    cJSON *now = cJSON_GetObjectItem(result,"now");
     
    name = cJSON_GetObjectItem(location,"name")->valuestring;
    text = cJSON_GetObjectItem(now,"text")->valuestring;
    temp = cJSON_GetObjectItem(now,"temp")->valueint;
    rh = cJSON_GetObjectItem(now,"rh")->valueint;
    wind_class = cJSON_GetObjectItem(now,"wind_class")->valuestring;

    ESP_LOGI(TAG, "地区: %s", name);
    ESP_LOGI(TAG, "天气: %s", text);
    ESP_LOGI(TAG, "温度: %d", temp);
    ESP_LOGI(TAG, "湿度: %d", rh);
    ESP_LOGI(TAG, "风力: %s", wind_class);

    xSemaphoreGive(date_semaphore);

    while(xSemaphoreTake(json_done, pdMS_TO_TICKS(10)) != pdTRUE) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    cJSON_Delete(root);
    esp_http_client_cleanup(client);
}

static void blink_led(void)
{
    /* If the addressable LED is enabled */
    if (s_led_state) {
        /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
        led_strip_set_pixel(led_strip, 0, base_r, base_g, base_b);
        /* Refresh the strip to send data */
        led_strip_refresh(led_strip);

        if (base_r < 16) {
            base_r++;
        } else if (base_g < 16) {
            base_g++;
        } else if (base_b < 16) {
            base_b++;
        } else {
            base_r = 0;
            base_g = 0;
            base_b = 0;
        }
    } else {
        /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
        led_strip_set_pixel(led_strip, 0, base_r, base_g, base_b);
        /* Refresh the strip to send data */
        led_strip_refresh(led_strip);

        if (base_r > 0) {
            base_r--;
        } else if (base_g > 0) {
            base_g--;
        } else if (base_b > 0) {
            base_b--;
        } else {
            base_r = 16;
            base_g = 16;
            base_b = 16;
        }
    }
}

static void configure_led(void)
{
    ESP_LOGI(TAG, "Example configured to blink addressable LED!");
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // at least one LED on board
    };
#if CONFIG_BLINK_LED_STRIP_BACKEND_RMT
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
#elif CONFIG_BLINK_LED_STRIP_BACKEND_SPI
    led_strip_spi_config_t spi_config = {
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));
#else
#error "unsupported LED strip backend"
#endif
    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip);
}


static void http_test_task(void *pvParameters)
{
    while (1) {
        if (gpio_get_level(GPIO_INPUT_PIN) == 0) {
            http_rest_with_url();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}

static void led_task(void *pvParameters)
{
    configure_led();
    while (1) {
        if (xSemaphoreTake(date_semaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
            
            if (strstr(text, "晴") != NULL) {
                s_led_state = 1;
            } else if (strstr(text, "雨") != NULL) {
                s_led_state = 0;
            }
            xSemaphoreGive(json_done);
        }
        blink_led();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}



void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());
    ESP_LOGI(TAG, "Connected to AP, begin http example");
    
    date_semaphore = xSemaphoreCreateBinary();
    json_done = xSemaphoreCreateBinary();

    xTaskCreate(http_test_task, "http_test_task", 8192, NULL, 5, NULL);
    xTaskCreate(led_task, "led_task", 8192, NULL, 5, NULL);
}
