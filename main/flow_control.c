#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "nvs_flash.h"

#define VALVE_OUTPUT GPIO_NUM_23
#define LED_OUTPUT GPIO_NUM_17

#define PULSES_STEP_UPDATE 1
#define PCNT_LOW_LIMIT  -1

#define CHANNEL_GPIO 27

#define ML_PER_PULSE_DEFAULT "2.0"

typedef enum {
    IDLE,
    AWAITING_FLOW,
    FLOWING,
    FINISHED
} flow_state_t;

typedef struct{
    flow_state_t flow_state;
    float volume_consumed;
    float volume_balance;
    char *msg;
}ws_response_msg_t;

struct ws_ctx_t {
    httpd_handle_t hd;
    int fd;
} ws_ctx;

float ml_per_pulse;

flow_state_t flow_state = IDLE;
QueueHandle_t queue_pulse_event;
QueueHandle_t queue_web_socket;
pcnt_unit_handle_t pcnt_unit = NULL;
float volume_released = 0.0;
float volume_consumed = 0.0;
float volume_balance = 0.0;

static bool cb_reach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx) {
    BaseType_t high_task_wakeup;

    // send event data to queue, from this interrupt callback
    xQueueSendFromISR(queue_pulse_event, &(edata->watch_point_value), &high_task_wakeup);

    return (high_task_wakeup == pdTRUE);
}

static void queue_ws_message(flow_state_t state, char* msg, float volume_consumed, float volume_balance)
{
    ws_response_msg_t response_msg = {
        .flow_state = state,
        .msg = msg,
        .volume_consumed = volume_consumed,
        .volume_balance = volume_balance,
    };

    if(xQueueSend(queue_web_socket, &response_msg, pdMS_TO_TICKS(1000)) != pdPASS){
        ESP_LOGE("TEST", "Failed to push to 'queue_web_socket'");
    }
}

static void close_ws_async()
{
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_CLOSE;

    httpd_ws_send_frame_async(ws_ctx.hd, ws_ctx.fd, &ws_pkt);

    free(ws_pkt.payload);
}

static void handle_send_ws_message_task(){

    ws_response_msg_t response_msg;

    while(1){
        if (xQueueReceive(queue_web_socket, &response_msg, pdMS_TO_TICKS(10000))) {

            cJSON* responseObj = cJSON_CreateObject();

            cJSON_AddNumberToObject(responseObj, "state", response_msg.flow_state);
            cJSON_AddStringToObject(responseObj, "msg", response_msg.msg);
            cJSON_AddNumberToObject(responseObj, "volume_consumed", response_msg.volume_consumed);
            cJSON_AddNumberToObject(responseObj, "volume_balance", response_msg.volume_balance);

            char* response = cJSON_Print(responseObj);
            httpd_ws_frame_t ws_pkt;
            memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

            ws_pkt.payload = (uint8_t*) response;
            ws_pkt.len = strlen(response);
            ws_pkt.type = HTTPD_WS_TYPE_TEXT;

            httpd_ws_send_frame_async(ws_ctx.hd, ws_ctx.fd, &ws_pkt);

            free(response);
            cJSON_Delete(responseObj);

            if (response_msg.flow_state == FINISHED){
                close_ws_async();
                flow_state = IDLE;

                vTaskDelete( NULL );
            }
        }
    }
}

static void handle_flow_task (void *params)
{
    //release valve
    gpio_set_level(VALVE_OUTPUT, 1);
    gpio_set_level(LED_OUTPUT, 1);

    ESP_LOGI("TEST", "Waiting for flow start.");
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
    flow_state = AWAITING_FLOW;

    xTaskCreate(handle_send_ws_message_task, "HandleSendWsMessageTask", 1024 * 2, (void *) NULL, 5, NULL);
    queue_ws_message(flow_state, "Liberado. Inicie o fluxo.", volume_consumed, volume_balance);

    // Report counter value
    int pulse_count = 0;
    int event_count = 0;

    // Await for flow start
    if (xQueueReceive(queue_pulse_event, &event_count, pdMS_TO_TICKS(10000))) {
        ESP_LOGI("TEST", "Flow has started. Event, count: %d", event_count);
        flow_state = FLOWING;
        queue_ws_message(flow_state, "Fluxo iniciado.", volume_consumed, volume_balance);
    }
    else {
        gpio_set_level(VALVE_OUTPUT, 0);
        gpio_set_level(LED_OUTPUT, 0);

        ESP_LOGI("TEST", "Flow has not started in time. Cancelling operation.");
        flow_state = FINISHED;
        queue_ws_message(flow_state, "Fluxo não iniciado no tempo. Cancelando.", volume_consumed, volume_balance);

        ESP_ERROR_CHECK(pcnt_unit_stop(pcnt_unit));
        ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
        xQueueReset(queue_pulse_event);

        vTaskDelete( NULL );
    }

    while(1){
        if (xQueueReceive(queue_pulse_event, &event_count, pdMS_TO_TICKS(5000))) {
            ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
            volume_consumed = pulse_count * ml_per_pulse;
            volume_balance = volume_released - volume_consumed;

            if(volume_consumed >= volume_released) {
                gpio_set_level(VALVE_OUTPUT, 0);
                gpio_set_level(LED_OUTPUT, 0);

                flow_state = FINISHED;
                volume_consumed = volume_released;
                volume_balance = 0;

                ESP_ERROR_CHECK(pcnt_unit_stop(pcnt_unit));
                ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
                xQueueReset(queue_pulse_event);

                ESP_LOGI("TEST", "Stopping. Volume Released: %.2f ml, Volume Consumed: %.2f ml, Balance: %.2f ml", volume_released, volume_consumed, volume_balance);
                queue_ws_message(flow_state, "Volume liberado esgotado. Finalizando.", volume_consumed, volume_balance);

                vTaskDelete( NULL );
            }

            ESP_LOGI("TEST", "Updating. Pulse Count: %d,  Volume Consumed: %.2f ml, Balance: %.2f ml", pulse_count, volume_consumed, volume_balance);
            queue_ws_message(flow_state, "Fluxo iniciado.", volume_consumed, volume_balance);
        }
        else
        {
            gpio_set_level(VALVE_OUTPUT, 0);
            gpio_set_level(LED_OUTPUT, 0);

            ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
            volume_consumed = pulse_count * ml_per_pulse;
            volume_balance = volume_released - volume_consumed;

            ESP_LOGI("TEST", "Flow has stoped. Volume Released: %.2f ml, Volume Consumed: %.2f ml, Balance: %.2f ml", volume_released, volume_consumed, volume_balance);
            flow_state = FINISHED;

            queue_ws_message(flow_state, "Fluxo interrompido. Finalizando.", volume_consumed, volume_balance);

            ESP_ERROR_CHECK(pcnt_unit_stop(pcnt_unit));
            ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));

            vTaskDelete( NULL );
        }
    }
    vTaskDelete( NULL );
}

static esp_err_t start_new_flow(float volume, httpd_handle_t http_handle, httpd_req_t* req)
{
    if (flow_state == IDLE){
        volume_released = volume;
        volume_consumed = 0;
        volume_balance = volume;

        ws_ctx.hd = http_handle;
        ws_ctx.fd = httpd_req_to_sockfd(req);

        xTaskCreate(handle_flow_task, "HandleFlowTask", 1024 * 2, (void *) NULL, 10, NULL);
        return ESP_OK;
    }
    return ESP_FAIL;
}


static void set_ml_per_pulse_to_nvs(float ml_per_pulse) {
    // Open NVS
    ESP_LOGI("NVS", "Opening Non-Volatile Storage (NVS) handle... ");
    esp_err_t ret;
    nvs_handle_t main_nvs_handle;
    ret = nvs_open("nvs", NVS_READWRITE, &main_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGI("NVS", "Error (%s) opening NVS handle!\n", esp_err_to_name(ret));
    }
    else {
        ESP_LOGI("NVS", "Non-Volatile Storage (NVS) opened");
    }

    char ml_per_pulse_str[10];
    sprintf(ml_per_pulse_str, "%f", ml_per_pulse);

    ret = nvs_set_str(main_nvs_handle, "ml_per_pulse", ml_per_pulse_str);
    ESP_LOGI("NVS", "Setting Ml_per_pulse with %s is %s", ml_per_pulse_str, (ret != ESP_OK) ? "Failed!\n" : "Done\n");
    
    ESP_LOGI("NVS", "Committing updates in NVS ... ");
    ret = nvs_commit(main_nvs_handle);
    ESP_LOGI("NVS", "%s" ,(ret != ESP_OK) ? "Failed!\n" : "Done\n");
    
    // Close
    nvs_close(main_nvs_handle);
}


static char * get_ml_per_pulse_from_nvs() {
    // Open NVS
    ESP_LOGI("NVS", "Opening Non-Volatile Storage (NVS) handle... ");
    esp_err_t ret;
    nvs_handle_t main_nvs_handle;
    ret = nvs_open("nvs", NVS_READWRITE, &main_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGI("NVS", "Error (%s) opening NVS handle!\n", esp_err_to_name(ret));
    }
    else {
        ESP_LOGI("NVS", "Non-Volatile Storage (NVS) opened");
    }

    size_t req_size;
    nvs_get_str(main_nvs_handle, "ml_per_pulse", NULL, &req_size);
    char *ml_per_pulse = malloc(req_size);
    ret = nvs_get_str(main_nvs_handle, "ml_per_pulse", ml_per_pulse, &req_size);
    nvs_get_str(main_nvs_handle, "ml_per_pulse", NULL, &req_size);

    // Close
    nvs_close(main_nvs_handle);

    switch (ret) {
        case ESP_OK:
            ESP_LOGI("NVS", "Ml_per_pulse getted with success from NVS. Ml_per_pulse: %s", ml_per_pulse);
            return ml_per_pulse;

        case ESP_ERR_NVS_NOT_FOUND:
            ESP_LOGI("NVS", "Ml_per_pulse is not set. Initializing with default value: %s.", ML_PER_PULSE_DEFAULT);
            set_ml_per_pulse_to_nvs(strtof(ML_PER_PULSE_DEFAULT, NULL));
            return ML_PER_PULSE_DEFAULT;
        default :
            ESP_LOGI("NVS", "Error (%s) reading!\n", esp_err_to_name(ret));
            esp_restart();
    }
}

static void update_ml_per_pulse(float factor) {
    ml_per_pulse *= factor;
    set_ml_per_pulse_to_nvs(ml_per_pulse);
    ESP_LOGI("TEST", "Ml per pulse was updated to: %0.5f", ml_per_pulse);
}

static void finish_calibration(float adjust_factor){
    ESP_LOGI("TEST", "Adjust factor: %0.3f", adjust_factor);
    update_ml_per_pulse(adjust_factor);
    flow_state = IDLE;
}

static void pcnt_init()
{
    char * ml_per_pulse_str = get_ml_per_pulse_from_nvs();

    ml_per_pulse = strtof(ml_per_pulse_str, NULL);

    ESP_LOGI("TEST", "Using ML per Pulse : %0.5f", ml_per_pulse);

    pcnt_unit_config_t unit_config = {
        .flags.accum_count = 1,
        .high_limit = PULSES_STEP_UPDATE,
        .low_limit = PCNT_LOW_LIMIT,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    pcnt_chan_config_t channel_config = {
        .edge_gpio_num = CHANNEL_GPIO,
        .level_gpio_num = -1
    };
    pcnt_channel_handle_t pcnt_channel = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &channel_config, &pcnt_channel));

    // decrease the counter on rising edge, increase the counter on falling edge
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_channel, PCNT_CHANNEL_EDGE_ACTION_HOLD, PCNT_CHANNEL_EDGE_ACTION_INCREASE));

    // add watch points
    int watch_points[] = {PULSES_STEP_UPDATE};
    for (size_t i = 0; i < sizeof(watch_points) / sizeof(watch_points[0]); i++) {
        ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, watch_points[i]));
    }

    pcnt_event_callbacks_t cbs = {
        .on_reach = cb_reach
    };

    queue_web_socket = xQueueCreate(10, sizeof(ws_response_msg_t));

    queue_pulse_event = xQueueCreate(10, sizeof(int));
    ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_unit, &cbs, queue_pulse_event));

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
}