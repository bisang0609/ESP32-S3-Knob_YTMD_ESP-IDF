#include "encoder_control.h"

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "bidi_switch_knob.h"
#include "user_config.h"
#include "ytmd_client.h"

static const char *TAG = "encoder_ctl";

#define ENCODER_THRESHOLD     15
#define ENCODER_IDLE_RESET_MS 500
#define ENCODER_TASK_STACK    (3 * 1024)
#define ENCODER_TASK_PRIO     2

#define ENC_EVENT_LEFT        BIT0
#define ENC_EVENT_RIGHT       BIT1
#define ENC_EVENT_ALL         (ENC_EVENT_LEFT | ENC_EVENT_RIGHT)

static EventGroupHandle_t s_events = NULL;
static knob_handle_t s_knob = NULL;
static TaskHandle_t s_task = NULL;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static int s_counter = 0;
static bool s_wait_song_change = false;

static void knob_left_cb(void *arg, void *data)
{
    (void)arg;
    (void)data;
    if (s_events) {
        xEventGroupSetBits(s_events, ENC_EVENT_LEFT);
    }
}

static void knob_right_cb(void *arg, void *data)
{
    (void)arg;
    (void)data;
    if (s_events) {
        xEventGroupSetBits(s_events, ENC_EVENT_RIGHT);
    }
}

static void encoder_task(void *arg)
{
    (void)arg;

    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(
            s_events,
            ENC_EVENT_ALL,
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(ENCODER_IDLE_RESET_MS));

        if (bits == 0) {
            bool need_reset = false;
            portENTER_CRITICAL(&s_lock);
            if (s_counter != 0 || s_wait_song_change) {
                s_counter = 0;
                s_wait_song_change = false;
                need_reset = true;
            }
            portEXIT_CRITICAL(&s_lock);

            if (need_reset) {
                ESP_LOGI(TAG, "counter idle reset (%dms no movement)", ENCODER_IDLE_RESET_MS);
            }
            continue;
        }

        bool need_next = false;
        bool need_prev = false;

        portENTER_CRITICAL(&s_lock);
        if (!s_wait_song_change) {
            if (bits & ENC_EVENT_RIGHT) {
                if (s_counter < ENCODER_THRESHOLD) {
                    s_counter++;
                }
                if (s_counter >= ENCODER_THRESHOLD) {
                    s_counter = ENCODER_THRESHOLD;
                    s_wait_song_change = true;
                    need_next = true;
                }
            }

            if (bits & ENC_EVENT_LEFT) {
                if (s_counter > -ENCODER_THRESHOLD) {
                    s_counter--;
                }
                if (s_counter <= -ENCODER_THRESHOLD) {
                    s_counter = -ENCODER_THRESHOLD;
                    s_wait_song_change = true;
                    need_prev = true;
                }
            }
        }
        int local_counter = s_counter;
        bool local_wait = s_wait_song_change;
        portEXIT_CRITICAL(&s_lock);

        ESP_LOGI(TAG, "counter=%d wait_song_change=%d bits=0x%02x",
                 local_counter, (int)local_wait, (unsigned int)bits);

        if (need_next) {
            esp_err_t ret = ytmd_client_send_command(YTMD_CMD_NEXT);
            ESP_LOGI(TAG, "send NEXT ret=%s", esp_err_to_name(ret));
        } else if (need_prev) {
            esp_err_t ret = ytmd_client_send_command(YTMD_CMD_PREVIOUS);
            ESP_LOGI(TAG, "send PREVIOUS ret=%s", esp_err_to_name(ret));
        }
    }
}

esp_err_t encoder_control_start(void)
{
    if (s_task) {
        return ESP_OK;
    }

    s_events = xEventGroupCreate();
    if (!s_events) {
        ESP_LOGE(TAG, "failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    knob_config_t cfg = {
        .gpio_encoder_a = EXAMPLE_ENCODER_ECA_PIN,
        .gpio_encoder_b = EXAMPLE_ENCODER_ECB_PIN,
    };
    s_knob = iot_knob_create(&cfg);
    if (!s_knob) {
        ESP_LOGE(TAG, "failed to init knob");
        vEventGroupDelete(s_events);
        s_events = NULL;
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(iot_knob_register_cb(s_knob, KNOB_LEFT, knob_left_cb, NULL));
    ESP_ERROR_CHECK(iot_knob_register_cb(s_knob, KNOB_RIGHT, knob_right_cb, NULL));

    BaseType_t ok = xTaskCreate(encoder_task, "encoder_task", ENCODER_TASK_STACK, NULL, ENCODER_TASK_PRIO, &s_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create encoder task");
        iot_knob_delete(s_knob);
        s_knob = NULL;
        vEventGroupDelete(s_events);
        s_events = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "encoder control started (threshold=%d, A=%d, B=%d)",
             ENCODER_THRESHOLD, EXAMPLE_ENCODER_ECA_PIN, EXAMPLE_ENCODER_ECB_PIN);
    return ESP_OK;
}

void encoder_control_reset_counter(void)
{
    portENTER_CRITICAL(&s_lock);
    s_counter = 0;
    s_wait_song_change = false;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "counter reset");
}

int encoder_control_get_counter(void)
{
    portENTER_CRITICAL(&s_lock);
    int value = s_counter;
    portEXIT_CRITICAL(&s_lock);
    return value;
}
