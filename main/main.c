#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// #include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"

#include "wifi.c"
#include "flow_control.c"



void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //Initialize WiFi Station
    ESP_LOGI("WiFi", "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    //Initialize Pulse Counter Unit
    pcnt_init();
    start_new_flow_control();
}

