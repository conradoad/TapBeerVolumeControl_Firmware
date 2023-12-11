#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_spiffs.h"

#include "wifi.c"
#include "web_server.c"

#define WEB_MOUNT_POINT "/www"

#define SEL1_INPUT GPIO_NUM_32
#define SEL2_INPUT GPIO_NUM_33
#define SEL3_INPUT GPIO_NUM_25

esp_err_t init_fs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = WEB_MOUNT_POINT,
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = false
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    return ESP_OK;
}

void app_main(void)
{
    //Configure IOs
    gpio_set_direction(VALVE_OUTPUT, GPIO_MODE_OUTPUT);
    gpio_set_level(VALVE_OUTPUT, 0);

    gpio_set_direction(LED_OUTPUT, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(LED_OUTPUT, GPIO_PULLDOWN_ONLY);
    gpio_set_level(LED_OUTPUT, 0);

    gpio_set_direction(SEL1_INPUT, GPIO_MODE_INPUT);
    gpio_set_pull_mode(SEL1_INPUT, GPIO_PULLUP_ONLY);

    gpio_set_direction(SEL2_INPUT, GPIO_MODE_INPUT);
    gpio_set_pull_mode(SEL2_INPUT, GPIO_PULLUP_ONLY);

    gpio_set_direction(SEL3_INPUT, GPIO_MODE_INPUT);
    gpio_set_pull_mode(SEL3_INPUT, GPIO_PULLUP_ONLY);


    //Only for testing purpose

    // if (!gpio_get_level(SEL1_INPUT) || !gpio_get_level(SEL2_INPUT) || !gpio_get_level(SEL3_INPUT)) {
    //     gpio_set_level(LED_OUTPUT, 1);
    //     vTaskDelay(pdMS_TO_TICKS(3000));
    //     gpio_set_level(LED_OUTPUT, 0);
    // }

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //Initialize WiFi Station
    // ESP_LOGI("WiFi", "ESP_WIFI_MODE_STA");
    // wifi_init_sta();
    
    ESP_LOGI("WiFi", "ESP_WIFI_MODE_AP");
    wifi_init_ap();

    //Initialize Pulse Counter Unit
    pcnt_init();

    //Initialize web server
    ESP_ERROR_CHECK(init_fs());
    ESP_ERROR_CHECK(start_web_server(WEB_MOUNT_POINT));
}

