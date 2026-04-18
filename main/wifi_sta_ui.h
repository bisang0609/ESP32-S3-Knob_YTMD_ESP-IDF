#ifndef WIFI_STA_UI_H
#define WIFI_STA_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char wifi_status[96];
    char ip_address[32];
    uint32_t retry_count;
} wifi_ui_snapshot_t;

esp_err_t wifi_sta_ui_start(void);
bool wifi_sta_ui_consume_update(wifi_ui_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_STA_UI_H */
