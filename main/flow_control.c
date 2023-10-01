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


#define PULSES_STEP_STARTED 5
#define PULSES_STEP_UPDATE 10
#define PCNT_LOW_LIMIT  -1

#define CHANNEL_GPIO 4

typedef enum {
    IDLE,
    AWAITING_FLOW,
    FLOWING,
    CALIB
} flow_state_t;

typedef enum {
    STATUS,
    VOLUME
} ws_msg_type_t;

struct ws_ctx_t {
    httpd_handle_t hd;
    int fd;
} ws_ctx;

float ml_per_pulse;

flow_state_t flow_state = IDLE;
QueueHandle_t queue;
pcnt_unit_handle_t pcnt_unit = NULL;
float volume_released = 0.0;
float volume_consumed = 0.0;
float volume_balance = 0.0;

static bool cb_reach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx) {
    BaseType_t high_task_wakeup;

    if ((flow_state == FLOWING || flow_state == CALIB) && edata->watch_point_value == PULSES_STEP_UPDATE)
    {
        // send event data to queue, from this interrupt callback
        xQueueSendFromISR(queue, &(edata->watch_point_value), &high_task_wakeup);
    }
    else if (flow_state == AWAITING_FLOW && edata->watch_point_value == PULSES_STEP_STARTED)
    {
        // send event data to queue, from this interrupt callback
        xQueueSendFromISR(queue, &(edata->watch_point_value), &high_task_wakeup);
    }

    return (high_task_wakeup == pdTRUE);
}

static void send_status_msg_async(char* msg)
{
    cJSON* responseObj = cJSON_CreateObject();

    cJSON_AddNumberToObject(responseObj, "type", STATUS);
    cJSON_AddStringToObject(responseObj, "msg", msg);

    char* response = cJSON_Print(responseObj);

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    ws_pkt.payload = (uint8_t*) response;
    ws_pkt.len = strlen(response);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(ws_ctx.hd, ws_ctx.fd, &ws_pkt);
}

static void send_volume_async(float volume_consumed, float volume_balance)
{
    cJSON* responseObj = cJSON_CreateObject();

    cJSON_AddNumberToObject(responseObj, "type", VOLUME);
    cJSON_AddNumberToObject(responseObj, "volume_consumed", volume_consumed);
    cJSON_AddNumberToObject(responseObj, "volume_balance", volume_balance);

    char* response = cJSON_Print(responseObj);

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    ws_pkt.payload = (uint8_t*) response;
    ws_pkt.len = strlen(response);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(ws_ctx.hd, ws_ctx.fd, &ws_pkt);
}

static void close_ws_async()
{
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_CLOSE;

    httpd_ws_send_frame_async(ws_ctx.hd, ws_ctx.fd, &ws_pkt);
}

static void handle_flow_task (void *params)
{

    QueueHandle_t queue = (QueueHandle_t)params;

    ESP_LOGI("TEST", "Waiting for flow start.");
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
    flow_state = AWAITING_FLOW;

    send_status_msg_async("Liberado. Inicie o fluxo.");

    // Report counter value
    int pulse_count = 0;
    int event_count = 0;

    // Await for flow start
    if (xQueueReceive(queue, &event_count, pdMS_TO_TICKS(10000))) {
        ESP_LOGI("TEST", "Flow has started. Event, count: %d", event_count);
        flow_state = FLOWING;
        send_status_msg_async("Fluxo iniciado.");
    }
    else {
        ESP_LOGI("TEST", "Flow has not started in time. Cancelling operation.");
        flow_state = IDLE;

        send_status_msg_async("Fluxo não iniciado no tempo. Cancelando.");
        close_ws_async();

        ESP_ERROR_CHECK(pcnt_unit_stop(pcnt_unit));
        ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));

        vTaskDelete( NULL );
    }

    while(1){
        if (xQueueReceive(queue, &event_count, pdMS_TO_TICKS(5000))) {
            ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
            volume_consumed = pulse_count * ml_per_pulse;
            volume_balance = volume_released - volume_consumed;

            if(volume_consumed >= volume_released) {

                volume_consumed = volume_released;
                volume_balance = 0;

                ESP_LOGI("TEST", "Updating. Pulse Count: %d,  Volume Consumed: %.2f ml, Balance: %.2f ml", pulse_count, volume_consumed, volume_balance);
                send_volume_async(volume_consumed, volume_balance);

                ESP_LOGI("TEST", "Stopping. Volume Released: %.2f ml, Volume Consumed: %.2f ml, Balance: %.2f ml", volume_released, volume_consumed, volume_balance);
                send_status_msg_async("Volume liberado esgotado. Finalizando.");
                close_ws_async();

                flow_state = IDLE;
                ESP_ERROR_CHECK(pcnt_unit_stop(pcnt_unit));
                ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));

                vTaskDelete( NULL );
            }

            ESP_LOGI("TEST", "Updating. Pulse Count: %d,  Volume Consumed: %.2f ml, Balance: %.2f ml", pulse_count, volume_consumed, volume_balance);
            send_volume_async(volume_consumed, volume_balance);
        }
        else
        {
            ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
            volume_consumed = pulse_count * ml_per_pulse;
            volume_balance = volume_released - volume_consumed;

            ESP_LOGI("TEST", "Flow has stoped. Volume Released: %.2f ml, Volume Consumed: %.2f ml, Balance: %.2f ml", volume_released, volume_consumed, volume_balance);
            flow_state = IDLE;

            send_status_msg_async("Fluxo interrompido. Finalizando.");
            close_ws_async();

            ESP_ERROR_CHECK(pcnt_unit_stop(pcnt_unit));
            ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));

            vTaskDelete( NULL );
        }
    }
    vTaskDelete( NULL );
}

static void handle_calibration_task (void *params)
{
    flow_state = CALIB;
    QueueHandle_t queue = (QueueHandle_t)params;

    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    // Report counter value
    int pulse_count = 0;
    int event_count = 0;

    while(flow_state == CALIB){
        if (xQueueReceive(queue, &event_count, pdMS_TO_TICKS(1000))) {

            ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
            volume_consumed = pulse_count * ml_per_pulse;

            ESP_LOGI("TEST", "Updating. Pulse Count: %d,  Volume Consumed: %.2f ml", pulse_count, volume_consumed);
            send_volume_async(volume_consumed, 0);
        }
        else
        {
        }
    }

    ESP_ERROR_CHECK(pcnt_unit_stop(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    close_ws_async();

    vTaskDelete( NULL );
}

static void start_new_flow_control(float volume, httpd_handle_t http_handle, httpd_req_t* req)
{
    volume_released = volume;
    volume_consumed = 0;
    volume_balance = volume;

    ws_ctx.hd = http_handle;
    ws_ctx.fd = httpd_req_to_sockfd(req);

    xTaskCreate(handle_flow_task, "HandleFlowTask", 1024 * 4, (void *)queue, 10, NULL);
}

static void start_calibration(httpd_handle_t http_handle, httpd_req_t* req)
{
    volume_consumed = 0;

    ws_ctx.hd = http_handle;
    ws_ctx.fd = httpd_req_to_sockfd(req);

    xTaskCreate(handle_calibration_task, "HandleCalibrationTask", 1024 * 4, (void *)queue, 10, NULL);
}

static void update_ml_per_pulse(float factor) {
    ml_per_pulse *= factor;
    ESP_LOGI("TEST", "Ml per pulse was updated to: %0.3f", ml_per_pulse);
}

static void finish_calibration(float adjust_factor){
    ESP_LOGI("TEST", "Adjust factor: %0.3f", adjust_factor);
    update_ml_per_pulse(adjust_factor);
    flow_state = IDLE;
}

static void pcnt_init()
{
    ml_per_pulse = 2;

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
    int watch_points[] = {PULSES_STEP_STARTED, PULSES_STEP_UPDATE};
    for (size_t i = 0; i < sizeof(watch_points) / sizeof(watch_points[0]); i++) {
        ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, watch_points[i]));
    }

    pcnt_event_callbacks_t cbs = {
        .on_reach = cb_reach
    };

    queue = xQueueCreate(10, sizeof(int));
    ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_unit, &cbs, queue));

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
}