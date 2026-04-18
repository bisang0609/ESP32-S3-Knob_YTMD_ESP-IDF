#include "app_runtime_config.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "ff.h"
#include "soc/soc_caps.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "wear_levelling.h"

static const char *TAG = "app_cfg";

#define CFG_DEFAULT_WIFI_SSID   "YeoSangMin_2G"
#define CFG_DEFAULT_WIFI_PASS   "min1596321"
#define CFG_DEFAULT_TARGET_IP   "192.168.0.30"

#define CFG_BASE_PATH           "/usb"
#define CFG_FILE_PATH           CFG_BASE_PATH "/config.txt"
#define CFG_PARTITION_LABEL     "storage"
#define CFG_VOLUME_LABEL        "YTMD"

static app_runtime_config_t s_cfg;
static bool s_cfg_ready = false;

static tinyusb_msc_storage_handle_t s_storage_hdl = NULL;
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

static void cfg_set_defaults(app_runtime_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), "%s", CFG_DEFAULT_WIFI_SSID);
    snprintf(cfg->wifi_password, sizeof(cfg->wifi_password), "%s", CFG_DEFAULT_WIFI_PASS);
    snprintf(cfg->target_ip, sizeof(cfg->target_ip), "%s", CFG_DEFAULT_TARGET_IP);
}

static char *trim_ws(char *s)
{
    if (!s) {
        return s;
    }

    while (*s && isspace((unsigned char)*s)) {
        s++;
    }

    if (*s == '\0') {
        return s;
    }

    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

static void strip_quotes(char *s)
{
    size_t n;
    if (!s) {
        return;
    }
    n = strlen(s);
    if (n < 2) {
        return;
    }
    if ((s[0] == '"' && s[n - 1] == '"') || (s[0] == '\'' && s[n - 1] == '\'')) {
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
    }
}

static bool key_equals(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = (char)tolower((unsigned char)*a);
        char cb = (char)tolower((unsigned char)*b);
        if (ca != cb) {
            return false;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static void apply_key_value(app_runtime_config_t *cfg, const char *key, const char *value)
{
    if (!cfg || !key || !value || value[0] == '\0') {
        return;
    }

    if (key_equals(key, "ap") || key_equals(key, "ssid") || key_equals(key, "wifi_ssid")) {
        snprintf(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), "%s", value);
        return;
    }
    if (key_equals(key, "pw") || key_equals(key, "password") || key_equals(key, "wifi_pw")) {
        snprintf(cfg->wifi_password, sizeof(cfg->wifi_password), "%s", value);
        return;
    }
    if (key_equals(key, "targetip") || key_equals(key, "target_ip") ||
        key_equals(key, "tagetip") || key_equals(key, "taget_ip") ||
        key_equals(key, "ytmd_ip")) {
        snprintf(cfg->target_ip, sizeof(cfg->target_ip), "%s", value);
        return;
    }
}

static esp_err_t ensure_default_config_file(const app_runtime_config_t *cfg)
{
    FILE *f = fopen(CFG_FILE_PATH, "r");
    if (f) {
        fclose(f);
        return ESP_OK;
    }

    f = fopen(CFG_FILE_PATH, "w");
    if (!f) {
        ESP_LOGE(TAG, "cannot create %s", CFG_FILE_PATH);
        return ESP_FAIL;
    }

    fprintf(f, "# USB config file\r\n");
    fprintf(f, "# edit then reboot device\r\n");
    fprintf(f, "ap=%s\r\n", cfg->wifi_ssid);
    fprintf(f, "pw=%s\r\n", cfg->wifi_password);
    fprintf(f, "targetIP=%s\r\n", cfg->target_ip);
    fclose(f);
    ESP_LOGI(TAG, "created default %s", CFG_FILE_PATH);
    return ESP_OK;
}

static esp_err_t load_config_file(app_runtime_config_t *cfg)
{
    char line[256];
    FILE *f = fopen(CFG_FILE_PATH, "r");
    if (!f) {
        ESP_LOGW(TAG, "config not found: %s", CFG_FILE_PATH);
        return ESP_ERR_NOT_FOUND;
    }

    while (fgets(line, sizeof(line), f)) {
        char *key;
        char *val;
        char *sep;

        line[strcspn(line, "\r\n")] = '\0';
        key = trim_ws(line);
        if (key[0] == '\0' || key[0] == '#' || key[0] == ';') {
            continue;
        }

        sep = strchr(key, '=');
        if (!sep) {
            sep = strchr(key, ':');
        }
        if (!sep) {
            continue;
        }

        *sep = '\0';
        val = sep + 1;
        key = trim_ws(key);
        val = trim_ws(val);
        strip_quotes(val);
        apply_key_value(cfg, key, val);
    }

    fclose(f);
    return ESP_OK;
}

static esp_err_t init_usb_msc_and_load(void)
{
    const esp_partition_t *data_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, CFG_PARTITION_LABEL);
    if (!data_partition) {
        ESP_LOGE(TAG, "FAT partition '%s' not found", CFG_PARTITION_LABEL);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = wl_mount(data_partition, &s_wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wl_mount failed: %s", esp_err_to_name(err));
        return err;
    }

    tinyusb_msc_storage_config_t storage_cfg = {
        .medium = {
            .wl_handle = s_wl_handle,
        },
        .fat_fs = {
            .base_path = CFG_BASE_PATH,
            .config = {
                .format_if_mount_failed = true,
                .max_files = 5,
                .allocation_unit_size = 4096,
            },
            .do_not_format = false,
            .format_flags = FM_ANY,
        },
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP,
    };

    err = tinyusb_msc_new_storage_spiflash(&storage_cfg, &s_storage_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_msc_new_storage_spiflash failed: %s", esp_err_to_name(err));
        if (err == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGE(TAG, "check CONFIG_TINYUSB_MSC_BUFSIZE and CONFIG_WL_SECTOR_SIZE (must be compatible)");
        }
        return err;
    }

    err = ensure_default_config_file(&s_cfg);
    if (err != ESP_OK) {
        return err;
    }

    FRESULT fr = f_setlabel(CFG_VOLUME_LABEL);
    if (fr != FR_OK) {
        ESP_LOGW(TAG, "f_setlabel('%s') failed: %d", CFG_VOLUME_LABEL, (int)fr);
    } else {
        ESP_LOGI(TAG, "volume label set to '%s'", CFG_VOLUME_LABEL);
    }

    err = load_config_file(&s_cfg);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        return err;
    }

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    err = tinyusb_msc_set_storage_mount_point(s_storage_hdl, TINYUSB_MSC_STORAGE_MOUNT_USB);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount to USB failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "USB MSC ready. config file: %s", CFG_FILE_PATH);
    }

    return ESP_OK;
}

esp_err_t app_runtime_config_init(void)
{
    esp_err_t ret = ESP_OK;
    cfg_set_defaults(&s_cfg);

#if SOC_USB_OTG_SUPPORTED
    esp_err_t err = init_usb_msc_and_load();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "using defaults (USB config init failed: %s)", esp_err_to_name(err));
        ret = err;
    }
#else
    ESP_LOGW(TAG, "USB OTG not supported on this target, using defaults");
#endif

    s_cfg_ready = true;
    ESP_LOGI(TAG, "cfg: ap='%s' targetIP='%s' pw_len=%u",
             s_cfg.wifi_ssid, s_cfg.target_ip, (unsigned)strlen(s_cfg.wifi_password));
    return ret;
}

const app_runtime_config_t *app_runtime_config_get(void)
{
    if (!s_cfg_ready) {
        cfg_set_defaults(&s_cfg);
        s_cfg_ready = true;
    }
    return &s_cfg;
}
