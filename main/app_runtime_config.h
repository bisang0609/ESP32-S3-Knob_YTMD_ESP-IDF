#ifndef APP_RUNTIME_CONFIG_H
#define APP_RUNTIME_CONFIG_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    char target_ip[64];
} app_runtime_config_t;

esp_err_t app_runtime_config_init(void);
const app_runtime_config_t *app_runtime_config_get(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_RUNTIME_CONFIG_H */
