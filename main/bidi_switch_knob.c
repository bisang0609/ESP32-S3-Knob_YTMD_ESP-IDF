/*
 * SPDX-FileCopyrightText: 2016-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bidi_switch_knob.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "knob";

#define TICKS_INTERVAL_US  (3000U)
#define DEBOUNCE_TICKS     (2U)

#define KNOB_CHECK(a, str, ret_val)                                        \
    do {                                                                    \
        if (!(a)) {                                                         \
            ESP_LOGE(TAG, "%s(%d): %s", __func__, __LINE__, (str));        \
            return (ret_val);                                               \
        }                                                                   \
    } while (0)

#define KNOB_CHECK_GOTO(a, str, label)                                     \
    do {                                                                    \
        if (!(a)) {                                                         \
            ESP_LOGE(TAG, "%s(%d): %s", __func__, __LINE__, (str));        \
            goto label;                                                     \
        }                                                                   \
    } while (0)

typedef struct knob_dev {
    uint8_t debounce_a_cnt;
    uint8_t debounce_b_cnt;
    uint8_t encoder_a_level;
    uint8_t encoder_b_level;
    knob_event_t event;
    int count_value;
    uint8_t (*hal_knob_level)(void *hardware_data);
    void *encoder_a;
    void *encoder_b;
    void *usr_data[KNOB_EVENT_MAX];
    knob_cb_t cb[KNOB_EVENT_MAX];
    struct knob_dev *next;
} knob_dev_t;

static knob_dev_t *s_head_handle = NULL;
static esp_timer_handle_t s_knob_timer_handle = NULL;
static bool s_is_timer_running = false;

static inline void call_event_cb(knob_dev_t *knob, knob_event_t ev)
{
    if (knob->cb[ev]) {
        knob->cb[ev](knob, knob->usr_data[ev]);
    }
}

static void process_knob_channel(uint8_t current_level, uint8_t *prev_level,
                                 uint8_t *debounce_cnt, int *count_value,
                                 knob_event_t event, bool is_increment, knob_dev_t *knob)
{
    if (current_level == 0) {
        if (current_level != *prev_level) {
            *debounce_cnt = 0;
        } else {
            (*debounce_cnt)++;
        }
    } else {
        if (current_level != *prev_level && (++(*debounce_cnt) >= DEBOUNCE_TICKS)) {
            *debounce_cnt = 0;
            *count_value += is_increment ? 1 : -1;
            knob->event = event;
            call_event_cb(knob, event);
        } else {
            *debounce_cnt = 0;
        }
    }
    *prev_level = current_level;
}

static void knob_handler(knob_dev_t *knob)
{
    uint8_t pha_value = knob->hal_knob_level(knob->encoder_a);
    uint8_t phb_value = knob->hal_knob_level(knob->encoder_b);

    process_knob_channel(pha_value, &knob->encoder_a_level,
                         &knob->debounce_a_cnt, &knob->count_value,
                         KNOB_RIGHT, true, knob);

    process_knob_channel(phb_value, &knob->encoder_b_level,
                         &knob->debounce_b_cnt, &knob->count_value,
                         KNOB_LEFT, false, knob);
}

static void knob_timer_cb(void *args)
{
    (void)args;
    for (knob_dev_t *target = s_head_handle; target; target = target->next) {
        knob_handler(target);
    }
}

knob_handle_t iot_knob_create(const knob_config_t *config)
{
    KNOB_CHECK(config != NULL, "config is NULL", NULL);
    KNOB_CHECK(config->gpio_encoder_a != config->gpio_encoder_b,
               "encoder A and B must differ", NULL);

    knob_dev_t *knob = (knob_dev_t *)calloc(1, sizeof(knob_dev_t));
    KNOB_CHECK(knob != NULL, "alloc knob failed", NULL);

    esp_err_t ret = knob_gpio_init(config->gpio_encoder_a);
    KNOB_CHECK_GOTO(ret == ESP_OK, "encoder A gpio init failed", fail_alloc);

    ret = knob_gpio_init(config->gpio_encoder_b);
    KNOB_CHECK_GOTO(ret == ESP_OK, "encoder B gpio init failed", fail_encoder_a);

    knob->hal_knob_level = knob_gpio_get_key_level;
    knob->encoder_a = (void *)(uintptr_t)config->gpio_encoder_a;
    knob->encoder_b = (void *)(uintptr_t)config->gpio_encoder_b;
    knob->encoder_a_level = knob->hal_knob_level(knob->encoder_a);
    knob->encoder_b_level = knob->hal_knob_level(knob->encoder_b);
    knob->event = KNOB_NONE;

    knob->next = s_head_handle;
    s_head_handle = knob;

    if (!s_knob_timer_handle) {
        esp_timer_create_args_t knob_timer = {
            .callback = knob_timer_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "knob_timer",
            .skip_unhandled_events = true,
        };
        ret = esp_timer_create(&knob_timer, &s_knob_timer_handle);
        KNOB_CHECK_GOTO(ret == ESP_OK, "knob timer create failed", fail_remove_head);
    }

    if (!s_is_timer_running) {
        ret = esp_timer_start_periodic(s_knob_timer_handle, TICKS_INTERVAL_US);
        KNOB_CHECK_GOTO(ret == ESP_OK, "knob timer start failed", fail_remove_head);
        s_is_timer_running = true;
    }

    ESP_LOGI(TAG, "knob init ok (A=%u, B=%u)", config->gpio_encoder_a, config->gpio_encoder_b);
    return (knob_handle_t)knob;

fail_remove_head:
    if (s_head_handle == knob) {
        s_head_handle = knob->next;
    }
    knob_gpio_deinit(config->gpio_encoder_b);
fail_encoder_a:
    knob_gpio_deinit(config->gpio_encoder_a);
fail_alloc:
    free(knob);
    return NULL;
}

esp_err_t iot_knob_delete(knob_handle_t knob_handle)
{
    KNOB_CHECK(knob_handle != NULL, "invalid knob handle", ESP_ERR_INVALID_ARG);
    knob_dev_t *knob = (knob_dev_t *)knob_handle;

    knob_dev_t **curr = &s_head_handle;
    while (*curr) {
        if (*curr == knob) {
            *curr = knob->next;
            break;
        }
        curr = &((*curr)->next);
    }

    knob_gpio_deinit((uint32_t)(uintptr_t)knob->encoder_a);
    knob_gpio_deinit((uint32_t)(uintptr_t)knob->encoder_b);
    free(knob);

    if (s_head_handle == NULL && s_knob_timer_handle != NULL) {
        if (s_is_timer_running) {
            esp_timer_stop(s_knob_timer_handle);
            s_is_timer_running = false;
        }
        esp_timer_delete(s_knob_timer_handle);
        s_knob_timer_handle = NULL;
    }

    return ESP_OK;
}

esp_err_t iot_knob_register_cb(knob_handle_t knob_handle, knob_event_t event, knob_cb_t cb, void *usr_data)
{
    KNOB_CHECK(knob_handle != NULL, "invalid knob handle", ESP_ERR_INVALID_ARG);
    KNOB_CHECK(event < KNOB_EVENT_MAX, "invalid event", ESP_ERR_INVALID_ARG);

    knob_dev_t *knob = (knob_dev_t *)knob_handle;
    knob->cb[event] = cb;
    knob->usr_data[event] = usr_data;
    return ESP_OK;
}

esp_err_t iot_knob_unregister_cb(knob_handle_t knob_handle, knob_event_t event)
{
    KNOB_CHECK(knob_handle != NULL, "invalid knob handle", ESP_ERR_INVALID_ARG);
    KNOB_CHECK(event < KNOB_EVENT_MAX, "invalid event", ESP_ERR_INVALID_ARG);

    knob_dev_t *knob = (knob_dev_t *)knob_handle;
    knob->cb[event] = NULL;
    knob->usr_data[event] = NULL;
    return ESP_OK;
}

knob_event_t iot_knob_get_event(knob_handle_t knob_handle)
{
    if (knob_handle == NULL) {
        return KNOB_NONE;
    }
    knob_dev_t *knob = (knob_dev_t *)knob_handle;
    return knob->event;
}

int iot_knob_get_count_value(knob_handle_t knob_handle)
{
    if (knob_handle == NULL) {
        return 0;
    }
    knob_dev_t *knob = (knob_dev_t *)knob_handle;
    return knob->count_value;
}

esp_err_t iot_knob_clear_count_value(knob_handle_t knob_handle)
{
    KNOB_CHECK(knob_handle != NULL, "invalid knob handle", ESP_ERR_INVALID_ARG);
    knob_dev_t *knob = (knob_dev_t *)knob_handle;
    knob->count_value = 0;
    return ESP_OK;
}

esp_err_t iot_knob_resume(void)
{
    KNOB_CHECK(s_knob_timer_handle != NULL, "timer handle invalid", ESP_ERR_INVALID_STATE);
    KNOB_CHECK(!s_is_timer_running, "timer already running", ESP_ERR_INVALID_STATE);

    esp_err_t err = esp_timer_start_periodic(s_knob_timer_handle, TICKS_INTERVAL_US);
    KNOB_CHECK(err == ESP_OK, "timer start failed", ESP_FAIL);
    s_is_timer_running = true;
    return ESP_OK;
}

esp_err_t iot_knob_stop(void)
{
    KNOB_CHECK(s_knob_timer_handle != NULL, "timer handle invalid", ESP_ERR_INVALID_STATE);
    KNOB_CHECK(s_is_timer_running, "timer not running", ESP_ERR_INVALID_STATE);

    esp_err_t err = esp_timer_stop(s_knob_timer_handle);
    KNOB_CHECK(err == ESP_OK, "timer stop failed", ESP_FAIL);
    s_is_timer_running = false;
    return ESP_OK;
}

esp_err_t knob_gpio_init(uint32_t gpio_num)
{
    gpio_config_t gpio_cfg = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&gpio_cfg);
}

esp_err_t knob_gpio_deinit(uint32_t gpio_num)
{
    return gpio_reset_pin((gpio_num_t)gpio_num);
}

uint8_t knob_gpio_get_key_level(void *gpio_num)
{
    return (uint8_t)gpio_get_level((gpio_num_t)(uintptr_t)gpio_num);
}
