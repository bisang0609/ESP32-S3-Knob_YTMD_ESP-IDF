#include "wifi_sta_ui.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define WIFI_SSID "YeoSangMin_2G"
#define WIFI_PASS "min1596321"
#define WIFI_START_CONNECT_DELAY_US (2500 * 1000)

static const char *TAG = "wifi_sta_ui";

static SemaphoreHandle_t s_state_mutex;
static wifi_ui_snapshot_t s_state;
static bool s_state_dirty;

static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;
static esp_timer_handle_t s_connect_timer;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void connect_timer_cb(void *arg);

static void state_lock(void)
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
}

static void state_unlock(void)
{
    xSemaphoreGive(s_state_mutex);
}

static void state_set_locked(const char *status, const char *ip, uint32_t retry_count)
{
    snprintf(s_state.wifi_status, sizeof(s_state.wifi_status), "%s", status);
    snprintf(s_state.ip_address, sizeof(s_state.ip_address), "%s", ip);
    s_state.retry_count = retry_count;
    s_state_dirty = true;
}

static void state_set_status(const char *status)
{
    state_lock();
    snprintf(s_state.wifi_status, sizeof(s_state.wifi_status), "%s", status);
    s_state_dirty = true;
    state_unlock();
}

static void state_set_ip(const char *ip)
{
    state_lock();
    snprintf(s_state.ip_address, sizeof(s_state.ip_address), "%s", ip);
    s_state_dirty = true;
    state_unlock();
}

static void state_inc_retry(void)
{
    state_lock();
    s_state.retry_count++;
    s_state_dirty = true;
    state_unlock();
}

static esp_err_t nvs_init_safe(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static void connect_timer_cb(void *arg)
{
    (void)arg;

    state_set_status("Connecting...");
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect failed from startup timer: %s", esp_err_to_name(ret));
        state_set_status("Connect request failed");
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi started. Delay 2.5s then connect.");
        state_set_status("Wi-Fi start. Connect in 2.5s");
        state_set_ip("-");
        esp_err_t stop_ret = esp_timer_stop(s_connect_timer);
        if (stop_ret != ESP_OK && stop_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Timer stop failed: %s", esp_err_to_name(stop_ret));
        }
        ESP_ERROR_CHECK(esp_timer_start_once(s_connect_timer, WIFI_START_CONNECT_DELAY_US));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "AP connected. Waiting DHCP.");
        state_set_status("AP connected. Waiting DHCP...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        char ip_text[32];

        snprintf(ip_text, sizeof(ip_text), IPSTR, IP2STR(&event->ip_info.ip));
        state_set_ip(ip_text);
        state_set_status("Connected");
        ESP_LOGI(TAG, "Got IP: %s", ip_text);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
        ESP_LOGW(TAG, "IP lost");
        state_set_ip("-");
        state_set_status("IP lost");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;
        char status_text[96];
        uint32_t retry_snapshot;

        state_inc_retry();

        state_lock();
        retry_snapshot = s_state.retry_count;
        snprintf(status_text, sizeof(status_text), "Disconnected (reason=%u). Retry...", (unsigned)disconnected->reason);
        snprintf(s_state.wifi_status, sizeof(s_state.wifi_status), "%s", status_text);
        snprintf(s_state.ip_address, sizeof(s_state.ip_address), "-");
        s_state_dirty = true;
        state_unlock();

        ESP_LOGW(TAG, "Disconnected, reason=%u retry=%" PRIu32, (unsigned)disconnected->reason, retry_snapshot);

        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_connect failed after disconnect: %s", esp_err_to_name(ret));
            state_set_status("Reconnect request failed");
        }
    }
}

esp_err_t wifi_sta_ui_start(void)
{
    if (s_state_mutex == NULL) {
        s_state_mutex = xSemaphoreCreateMutex();
        if (s_state_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    state_lock();
    state_set_locked("Wi-Fi init...", "-", 0);
    state_unlock();

    ESP_ERROR_CHECK(nvs_init_safe());

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    esp_netif_t *wifi_sta_netif = esp_netif_create_default_wifi_sta();
    if (wifi_sta_netif == NULL) {
        return ESP_FAIL;
    }

    const esp_timer_create_args_t connect_timer_args = {
        .callback = connect_timer_cb,
        .name = "wifi_start_connect_delay"
    };
    ESP_ERROR_CHECK(esp_timer_create(&connect_timer_args, &s_connect_timer));

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, &s_wifi_event_instance));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, &s_ip_event_instance));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            }
        }
    };
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", WIFI_PASS);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}

bool wifi_sta_ui_consume_update(wifi_ui_snapshot_t *out_snapshot)
{
    bool has_update = false;

    if (out_snapshot == NULL || s_state_mutex == NULL) {
        return false;
    }

    state_lock();
    if (s_state_dirty) {
        memcpy(out_snapshot, &s_state, sizeof(*out_snapshot));
        s_state_dirty = false;
        has_update = true;
    }
    state_unlock();

    return has_update;
}
