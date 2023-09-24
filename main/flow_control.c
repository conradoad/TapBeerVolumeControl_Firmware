#include <stdio.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"


#define ML_PER_PULSE 2.10
#define ML_AVAILABLE 1000

#define ML_FLOW_STARTED 5
#define PULSES_FLOW_STARTED (int) (ML_FLOW_STARTED / ML_PER_PULSE)

#define ML_STEP_UPDATE 50

#define PCNT_HIGH_LIMIT (int) (ML_STEP_UPDATE / ML_PER_PULSE)
#define PCNT_LOW_LIMIT  -1

#define CHANNEL_GPIO 4

typedef enum {
    IDLE,
    AWAITING_FLOW,
    FLOWING,
} flow_state_t;

bool is_running = false;
flow_state_t flow_state = IDLE;
QueueHandle_t queue;
pcnt_unit_handle_t pcnt_unit = NULL;

static bool cb_reach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx) {
    BaseType_t high_task_wakeup;

    if (flow_state == FLOWING && edata->watch_point_value == PCNT_HIGH_LIMIT)
    {
        // send event data to queue, from this interrupt callback
        xQueueSendFromISR(queue, &(edata->watch_point_value), &high_task_wakeup);
    }
    else if (flow_state == AWAITING_FLOW && edata->watch_point_value == PULSES_FLOW_STARTED)
    {
        // send event data to queue, from this interrupt callback
        xQueueSendFromISR(queue, &(edata->watch_point_value), &high_task_wakeup);
    }

    return (high_task_wakeup == pdTRUE);
}

void handle_flow_task (void *params)
{

    QueueHandle_t queue = (QueueHandle_t)params;

    ESP_LOGI("TEST", "Waiting for flow start.");
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
    flow_state = AWAITING_FLOW;

    // Report counter value
    int pulse_count = 0;
    int event_count = 0;

    // Await for flow start
    if (xQueueReceive(queue, &event_count, pdMS_TO_TICKS(10000))) {
        ESP_LOGI("TEST", "Flow has started. Event, count: %d", event_count);
        flow_state = FLOWING;
    }
    else {
        ESP_LOGI("TEST", "Flow has not started in time. Cancelling operation.");
        flow_state = IDLE;

        ESP_ERROR_CHECK(pcnt_unit_stop(pcnt_unit));
        ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));

        vTaskDelete( NULL );
    }


    while(1){
        if (xQueueReceive(queue, &event_count, pdMS_TO_TICKS(5000))) {
            ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
            ESP_LOGI("TEST", "Updating. Event count: %d. Pulses: %d. Volume: %.2f ml" , event_count, pulse_count, pulse_count * ML_PER_PULSE);

        } else {
            ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
            ESP_LOGI("TEST", "Flow has stoped. Finishing operation. Pulses count: %d. Total ML: %.2f", pulse_count, pulse_count * ML_PER_PULSE);
            flow_state = IDLE;

            ESP_ERROR_CHECK(pcnt_unit_stop(pcnt_unit));
            ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));

            vTaskDelete( NULL );
        }
    }

    vTaskDelete( NULL );
}

void start_new_flow_control() {
    xTaskCreate(handle_flow_task, "HandleFlowTask", 1024 * 2, (void *)queue, 10, NULL);
}

void pcnt_init()
{
    pcnt_unit_config_t unit_config = {
        .flags.accum_count = 1,
        .high_limit = PCNT_HIGH_LIMIT,
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
    int watch_points[] = {PULSES_FLOW_STARTED, PCNT_HIGH_LIMIT};
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