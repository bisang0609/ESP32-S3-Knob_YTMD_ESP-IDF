#include "ytmd_client.h"
#include "encoder_control.h"
#include "app_ui.h"
#include "app_runtime_config.h"
#include "lvgl_lock.h"
#include "ui/screens.h"
#include "esp_jpeg_dec.h"

#include <string.h>
#include <stdio.h>
#include <limits.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include <stdlib.h>
#include <lvgl.h>

static const char *TAG = "ytmd";

#define YTMD_PORT        26538
#define POLL_INTERVAL_MS 2000
#define CONNECT_RETRY_MS 3000
#define PREV_DOUBLE_THRESHOLD_S 3
#define PREV_DOUBLE_DELAY_MS    180
#define ART_DL_RETRY_MAX        3
#define ART_DL_RETRY_DELAY_MS   200

#define ART_W  360
#define ART_H  360
#define ART_BUF_SIZE (ART_W * ART_H * 2)
#define ART2_W 200
#define ART2_H 200
#define ART2_BUF_SIZE (ART2_W * ART2_H * 2)

#define BUF_SCALE_X2  2

#define JPEG_DL_MAX   (BUF_SCALE_X2 * 512 * 1024)
#define RESP_BUF_MAX  (BUF_SCALE_X2 * 4096)

/* Persistent decoded pixel buffer (RGB565, 360x360) */
static uint8_t *s_art_buf = NULL;
static lv_img_dsc_t s_art_dsc;
static uint8_t *s_art2_buf = NULL;
static lv_img_dsc_t s_art2_dsc;

/* Per-download scratch buffer (allocated once, reused) */
static uint8_t *s_dl_buf = NULL;
static int      s_dl_len = 0;
static int      s_dl_expected_len = 0;

/* State */
static char s_cur_video_id[64] = {0};
static char s_cur_title[160] = {0};
static char s_cur_artist[160] = {0};
static volatile int s_last_elapsed_seconds = -1;
static volatile int s_last_seek_percent = -1;

/* HTTP poll response buffer */
static char s_resp_buf[RESP_BUF_MAX];
static int  s_resp_len = 0;

/* UI helpers used from HTTP callbacks */
static void display_loadingbar(bool visible, int percent);
static void display_loading_spinner(bool visible);
static void display_seek_arc(int percent);
static void display_song_meta(const char *title, const char *artist);
static void display_main_startup_status(const char *status);
static void display_main_startup_done(void);
static int parse_elapsed_seconds(const char *json);

static const char *ytmd_target_ip(void)
{
    const app_runtime_config_t *cfg = app_runtime_config_get();
    if (cfg && cfg->target_ip[0] != '\0') {
        return cfg->target_ip;
    }
    return "192.168.0.30";
}

/* ------------------------------------------------------------------ */
/* JPEG decoder                                                         */
/* ------------------------------------------------------------------ */

static void resize_rgb565_le_to_art(const uint8_t *src_buf, int src_w, int src_h, uint8_t *dst_buf)
{
    if (!src_buf || !dst_buf || src_w <= 0 || src_h <= 0) {
        return;
    }

    const uint16_t *src = (const uint16_t *)src_buf;
    uint16_t *dst = (uint16_t *)dst_buf;

    for (int y = 0; y < ART_H; y++) {
        int sy = (y * src_h) / ART_H;
        if (sy >= src_h) sy = src_h - 1;

        for (int x = 0; x < ART_W; x++) {
            int sx = (x * src_w) / ART_W;
            if (sx >= src_w) sx = src_w - 1;

            uint16_t px = src[sy * src_w + sx];
            /* Byte-swap for LV_COLOR_16_SWAP */
            dst[y * ART_W + x] = (uint16_t)((px << 8) | (px >> 8));
        }
    }
}

static bool decode_jpeg(const uint8_t *jpeg, int jpeg_len, uint8_t *out_rgb565)
{
    if (!jpeg || jpeg_len <= 0 || !out_rgb565) {
        return false;
    }

    memset(out_rgb565, 0, ART_BUF_SIZE);

    jpeg_error_t err = JPEG_ERR_OK;
    jpeg_dec_handle_t jpeg_dec = NULL;
    uint8_t *decode_buf = NULL;
    jpeg_dec_io_t io = {
        .inbuf = (uint8_t *)jpeg,
        .inbuf_len = jpeg_len,
    };
    jpeg_dec_header_info_t out_info = {0};

    /* Pass 1: parse source dimensions to decide optional pre-scale */
    jpeg_dec_config_t cfg_probe = DEFAULT_JPEG_DEC_CONFIG();
    cfg_probe.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    err = jpeg_dec_open(&cfg_probe, &jpeg_dec);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_open(probe): %d", err);
        return false;
    }

    err = jpeg_dec_parse_header(jpeg_dec, &io, &out_info);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_parse_header(probe): %d", err);
        jpeg_dec_close(jpeg_dec);
        return false;
    }
    int src_w = out_info.width;
    int src_h = out_info.height;
    jpeg_dec_close(jpeg_dec);
    jpeg_dec = NULL;

    int scale_w = ART_W;
    int scale_h = ART_H;
    bool target_aligned = ((ART_W & 0x7) == 0) && ((ART_H & 0x7) == 0);
    bool within_ratio_limit = (src_w <= (ART_W * 8)) && (src_h <= (ART_H * 8));
    bool use_scale = target_aligned &&
                     within_ratio_limit &&
                     (src_w >= ART_W) &&
                     (src_h >= ART_H);

    /* Pass 2: decode */
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    if (use_scale) {
        cfg.scale.width = (uint16_t)scale_w;
        cfg.scale.height = (uint16_t)scale_h;
    }

    err = jpeg_dec_open(&cfg, &jpeg_dec);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_open: %d", err);
        return false;
    }

    io.inbuf = (uint8_t *)jpeg;
    io.inbuf_len = jpeg_len;
    err = jpeg_dec_parse_header(jpeg_dec, &io, &out_info);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_parse_header: %d", err);
        goto fail;
    }

    int outbuf_len = 0;
    err = jpeg_dec_get_outbuf_len(jpeg_dec, &outbuf_len);
    if (err != JPEG_ERR_OK || outbuf_len <= 0) {
        ESP_LOGE(TAG, "jpeg_dec_get_outbuf_len: %d", err);
        goto fail;
    }

    decode_buf = (uint8_t *)jpeg_calloc_align((size_t)outbuf_len, 16);
    if (!decode_buf) {
        ESP_LOGE(TAG, "jpeg_calloc_align failed (%d bytes)", outbuf_len);
        goto fail;
    }

    io.outbuf = decode_buf;
    err = jpeg_dec_process(jpeg_dec, &io);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_process: %d", err);
        goto fail;
    }

    resize_rgb565_le_to_art(decode_buf, out_info.width, out_info.height, out_rgb565);

    ESP_LOGI(TAG, "JPEG decoded %dx%d -> %dx%d -> %dx%d (esp_new_jpeg, scale=%s)",
             src_w, src_h, out_info.width, out_info.height, ART_W, ART_H,
             use_scale ? "on" : "off");

    jpeg_free_align(decode_buf);
    jpeg_dec_close(jpeg_dec);
    return true;

fail:
    if (decode_buf) {
        jpeg_free_align(decode_buf);
    }
    if (jpeg_dec) {
        jpeg_dec_close(jpeg_dec);
    }
    return false;
}

/* Resize already-swapped RGB565 buffer (no byte-swap) */
static void resize_rgb565_swapped(const uint8_t *src_buf, int src_w, int src_h,
                                  uint8_t *dst_buf, int dst_w, int dst_h)
{
    if (!src_buf || !dst_buf || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return;
    }

    const uint16_t *src = (const uint16_t *)src_buf;
    uint16_t *dst = (uint16_t *)dst_buf;

    for (int y = 0; y < dst_h; y++) {
        int sy = (y * src_h) / dst_h;
        if (sy >= src_h) sy = src_h - 1;

        for (int x = 0; x < dst_w; x++) {
            int sx = (x * src_w) / dst_w;
            if (sx >= src_w) sx = src_w - 1;
            dst[y * dst_w + x] = src[sy * src_w + sx];
        }
    }
}

/* ------------------------------------------------------------------ */
/* HTTP download                                                        */
/* ------------------------------------------------------------------ */

static esp_err_t dl_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_CONNECTED) {
        s_dl_expected_len = 0;
        display_loading_spinner(false);
        display_loadingbar(true, 0);
    } else if (evt->event_id == HTTP_EVENT_ON_HEADER) {
        if (evt->header_key && evt->header_value &&
            strcmp(evt->header_key, "Content-Length") == 0) {
            s_dl_expected_len = atoi(evt->header_value);
        }
    } else if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (s_dl_len + evt->data_len <= JPEG_DL_MAX) {
            memcpy(s_dl_buf + s_dl_len, evt->data, evt->data_len);
            s_dl_len += evt->data_len;
        } else {
            ESP_LOGW(TAG, "Download buffer full, truncating at %d bytes", s_dl_len);
        }

        if (s_dl_expected_len <= 0 && evt->client) {
            int content_len = esp_http_client_get_content_length(evt->client);
            if (content_len > 0) {
                s_dl_expected_len = content_len;
            }
        }

        int pct = 0;
        if (s_dl_expected_len > 0) {
            pct = (s_dl_len * 100) / s_dl_expected_len;
            if (pct > 99) pct = 99;
        } else {
            pct = (s_dl_len * 100) / JPEG_DL_MAX;
            if (pct > 95) pct = 95;
        }
        display_loadingbar(true, pct);
    }
    return ESP_OK;
}

/* Force HTTP (replace https:// with http://) to avoid TLS overhead */
static void make_http_url(const char *in, char *out, int out_size)
{
    if (strncmp(in, "https://", 8) == 0) {
        snprintf(out, out_size, "http://%s", in + 8);
    } else {
        strncpy(out, in, out_size - 1);
        out[out_size - 1] = '\0';
    }
}

static bool download_art(const char *url)
{
    if (!url || url[0] == '\0' || !s_dl_buf) return false;
    char urls[2][512] = {{0}};
    int url_count = 0;

    make_http_url(url, urls[url_count], sizeof(urls[url_count]));
    url_count++;

    if (strncmp(url, "https://", 8) == 0 && strncmp(urls[0], url, sizeof(urls[0])) != 0) {
        strncpy(urls[url_count], url, sizeof(urls[url_count]) - 1);
        urls[url_count][sizeof(urls[url_count]) - 1] = '\0';
        url_count++;
    }

    for (int u = 0; u < url_count; u++) {
        for (int attempt = 1; attempt <= ART_DL_RETRY_MAX; attempt++) {
            s_dl_len = 0;
            s_dl_expected_len = 0;
            display_loading_spinner(false);
            display_loadingbar(true, 0);

            esp_http_client_config_t cfg = {
                .url           = urls[u],
                .event_handler = dl_event_handler,
                .timeout_ms    = 8000,
                .buffer_size   = 8192,
            };
            esp_http_client_handle_t client = esp_http_client_init(&cfg);
            if (!client) {
                display_loadingbar(false, 0);
                return false;
            }

            esp_err_t err = esp_http_client_perform(client);
            int status = esp_http_client_get_status_code(client);
            bool complete = esp_http_client_is_complete_data_received(client);
            esp_http_client_cleanup(client);

            bool ok = (err == ESP_OK) && (status == 200) && complete && (s_dl_len > 100);
            if (ok) {
                ESP_LOGI(TAG, "Art downloaded %d bytes (attempt %d/%d, url_idx=%d)",
                         s_dl_len, attempt, ART_DL_RETRY_MAX, u);
                display_loadingbar(true, 100);
                return true;
            }

            ESP_LOGW(TAG, "Art download retry %d/%d failed err=%s status=%d complete=%d len=%d expected=%d url=%s",
                     attempt, ART_DL_RETRY_MAX, esp_err_to_name(err), status, (int)complete,
                     s_dl_len, s_dl_expected_len, urls[u]);

            if (attempt < ART_DL_RETRY_MAX) {
                vTaskDelay(pdMS_TO_TICKS(ART_DL_RETRY_DELAY_MS));
            }
        }
    }

    ESP_LOGE(TAG, "Art download failed after retries: %s", url);
    display_loadingbar(false, 0);
    return false;
}

/* ------------------------------------------------------------------ */
/* YTMD API poll                                                        */
/* ------------------------------------------------------------------ */

static esp_err_t poll_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (s_resp_len + evt->data_len < RESP_BUF_MAX - 1) {
            memcpy(s_resp_buf + s_resp_len, evt->data, evt->data_len);
            s_resp_len += evt->data_len;
        }
    }
    return ESP_OK;
}

static bool poll_ytmd(void)
{
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/api/v1/song", ytmd_target_ip(), YTMD_PORT);

    s_resp_len = 0;
    esp_http_client_config_t cfg = {
        .url           = url,
        .event_handler = poll_event_handler,
        .timeout_ms    = 3000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    esp_err_t err    = esp_http_client_perform(client);
    int       status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) return false;
    s_resp_buf[s_resp_len] = '\0';
    return s_resp_len > 0;
}

static bool is_http_success(int status_code)
{
    return (status_code >= 200) && (status_code < 300);
}

static esp_err_t try_transport_endpoint(const char *path, esp_http_client_method_t method)
{
    char url[160];
    snprintf(url, sizeof(url), "http://%s:%d%s", ytmd_target_ip(), YTMD_PORT, path);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 1500,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_method(client, method);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && is_http_success(status)) {
        ESP_LOGI(TAG, "transport cmd ok (%s %s): %d",
                 method == HTTP_METHOD_POST ? "POST" : "GET", path, status);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "transport cmd failed (%s %s): err=%s status=%d",
             method == HTTP_METHOD_POST ? "POST" : "GET",
             path, esp_err_to_name(err), status);
    return (err != ESP_OK) ? err : ESP_FAIL;
}

esp_err_t ytmd_client_send_command(ytmd_cmd_t cmd)
{
    static const char *next_paths[] = {
        "/api/v1/next",
        "/api/v1/nextTrack",
    };
    static const char *prev_paths[] = {
        "/api/v1/previous",
        "/api/v1/prev",
        "/api/v1/previousTrack",
    };
    static const char *pause_paths[] = {
        "/api/v1/pause",
        "/api/v1/player/pause",
        "/api/v1/togglePause",
        "/api/v1/toggle-play",
    };
    static const char *play_paths[] = {
        "/api/v1/play",
        "/api/v1/player/play",
        "/api/v1/resume",
        "/api/v1/togglePause",
        "/api/v1/toggle-play",
    };

    const char **paths = NULL;
    size_t path_count = 0;
    bool need_double_prev = false;

    switch (cmd) {
        case YTMD_CMD_NEXT:
            paths = next_paths;
            path_count = sizeof(next_paths) / sizeof(next_paths[0]);
            break;
        case YTMD_CMD_PREVIOUS:
            paths = prev_paths;
            path_count = sizeof(prev_paths) / sizeof(prev_paths[0]);
            need_double_prev = (s_last_elapsed_seconds >= PREV_DOUBLE_THRESHOLD_S);
            break;
        case YTMD_CMD_PAUSE:
            paths = pause_paths;
            path_count = sizeof(pause_paths) / sizeof(pause_paths[0]);
            break;
        case YTMD_CMD_PLAY:
            paths = play_paths;
            path_count = sizeof(play_paths) / sizeof(play_paths[0]);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    esp_err_t last_err = ESP_FAIL;

    for (size_t i = 0; i < path_count; i++) {
        last_err = try_transport_endpoint(paths[i], HTTP_METHOD_POST);
        if (last_err == ESP_OK) {
            if (need_double_prev) {
                vTaskDelay(pdMS_TO_TICKS(PREV_DOUBLE_DELAY_MS));
                esp_err_t second = try_transport_endpoint(paths[i], HTTP_METHOD_POST);
                ESP_LOGI(TAG, "previous double-send (POST) elapsed=%d sec ret=%s",
                         (int)s_last_elapsed_seconds, esp_err_to_name(second));
            }
            return ESP_OK;
        }

        last_err = try_transport_endpoint(paths[i], HTTP_METHOD_GET);
        if (last_err == ESP_OK) {
            if (need_double_prev) {
                vTaskDelay(pdMS_TO_TICKS(PREV_DOUBLE_DELAY_MS));
                esp_err_t second = try_transport_endpoint(paths[i], HTTP_METHOD_GET);
                ESP_LOGI(TAG, "previous double-send (GET) elapsed=%d sec ret=%s",
                         (int)s_last_elapsed_seconds, esp_err_to_name(second));
            }
            return ESP_OK;
        }
    }

    return last_err;
}

/* ------------------------------------------------------------------ */
/* Minimal JSON helpers (no external library)                           */
/* ------------------------------------------------------------------ */

/* Extract string value for first matching key: "key":"value"
   Handles basic backslash escapes. Returns true if found. */
static bool json_str(const char *json, const char *key,
                     char *out, int out_sz)
{
    char needle[72];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char *p = json;
    while ((p = strstr(p, needle)) != NULL) {
        p += strlen(needle);
        while (*p == ' ' || *p == '\t') p++;
        if (*p != ':') continue;           /* key without colon, skip */
        p++;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '"') continue;           /* value is not a string */
        p++;                               /* skip opening quote */
        int i = 0;
        while (*p && *p != '"' && i < out_sz - 1) {
            if (*p == '\\' && *(p + 1)) p++; /* skip escape char */
            out[i++] = *p++;
        }
        out[i] = '\0';
        return i > 0;
    }
    return false;
}

/* Extract numeric value for first matching key: "key":123 or "key":"123.4" */
static bool json_number(const char *json, const char *key, double *out)
{
    char needle[72];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char *p = json;
    while ((p = strstr(p, needle)) != NULL) {
        p += strlen(needle);
        while (*p == ' ' || *p == '\t') p++;
        if (*p != ':') continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;

        char num_buf[32] = {0};
        int i = 0;
        if (*p == '"') {
            p++;
            while (*p && *p != '"' && i < (int)sizeof(num_buf) - 1) {
                num_buf[i++] = *p++;
            }
        } else {
            while (*p && i < (int)sizeof(num_buf) - 1) {
                char c = *p;
                bool valid = ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+');
                if (!valid) break;
                num_buf[i++] = c;
                p++;
            }
        }

        if (i > 0) {
            *out = strtod(num_buf, NULL);
            return true;
        }
    }
    return false;
}

static const char *skip_json_ws(const char *p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        p++;
    }
    return p;
}

static const char *find_matching_brace(const char *start, char open_ch, char close_ch)
{
    if (!start || *start != open_ch) {
        return NULL;
    }

    int depth = 0;
    bool in_str = false;
    bool esc = false;

    for (const char *p = start; *p; p++) {
        char c = *p;

        if (in_str) {
            if (esc) {
                esc = false;
            } else if (c == '\\') {
                esc = true;
            } else if (c == '"') {
                in_str = false;
            }
            continue;
        }

        if (c == '"') {
            in_str = true;
            continue;
        }

        if (c == open_ch) {
            depth++;
        } else if (c == close_ch) {
            depth--;
            if (depth == 0) {
                return p;
            }
        }
    }

    return NULL;
}

static bool json_read_string_at(const char *p, char *out, int out_sz)
{
    if (!p || !out || out_sz <= 1 || *p != '"') {
        return false;
    }

    p++;
    int i = 0;
    while (*p && *p != '"' && i < out_sz - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;
        }
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

static const char *json_find_value_start(const char *json, const char *key)
{
    char needle[72];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char *p = json;
    while ((p = strstr(p, needle)) != NULL) {
        const char *v = p + strlen(needle);
        v = skip_json_ws(v);
        if (*v != ':') {
            p = v;
            continue;
        }
        v++;
        v = skip_json_ws(v);
        return v;
    }
    return NULL;
}

static bool parse_artist_from_obj(const char *obj_start, const char *obj_end,
                                  char *artist_out, int artist_sz)
{
    if (!obj_start || !obj_end || obj_end <= obj_start) {
        return false;
    }

    int n = (int)(obj_end - obj_start + 1);
    char excerpt[1024];
    if (n > (int)sizeof(excerpt) - 1) n = (int)sizeof(excerpt) - 1;
    memcpy(excerpt, obj_start, n);
    excerpt[n] = '\0';

    return json_str(excerpt, "name", artist_out, artist_sz) ||
           json_str(excerpt, "title", artist_out, artist_sz) ||
           json_str(excerpt, "text", artist_out, artist_sz) ||
           json_str(excerpt, "author", artist_out, artist_sz);
}

static bool parse_artist_from_value(const char *value_start, char *artist_out, int artist_sz)
{
    if (!value_start || !artist_out || artist_sz <= 1) {
        return false;
    }

    value_start = skip_json_ws(value_start);
    if (*value_start == '"') {
        return json_read_string_at(value_start, artist_out, artist_sz);
    }

    if (*value_start == '{') {
        const char *obj_end = find_matching_brace(value_start, '{', '}');
        return parse_artist_from_obj(value_start, obj_end, artist_out, artist_sz);
    }

    if (*value_start == '[') {
        const char *arr_end = find_matching_brace(value_start, '[', ']');
        if (!arr_end) return false;

        const char *p = value_start + 1;
        while (p && p < arr_end) {
            p = skip_json_ws(p);
            if (p >= arr_end || *p == ']') break;
            if (*p == ',') {
                p++;
                continue;
            }

            if (*p == '"') {
                return json_read_string_at(p, artist_out, artist_sz);
            }

            if (*p == '{') {
                const char *obj_end = find_matching_brace(p, '{', '}');
                if (parse_artist_from_obj(p, obj_end, artist_out, artist_sz)) {
                    return true;
                }
                if (!obj_end) return false;
                p = obj_end + 1;
                continue;
            }

            if (*p == '[') {
                const char *sub_end = find_matching_brace(p, '[', ']');
                if (!sub_end) return false;
                p = sub_end + 1;
                continue;
            }

            while (p < arr_end && *p != ',') p++;
        }
    }

    return false;
}

static bool parse_artist_by_key(const char *js, const char *key, char *artist_out, int artist_sz)
{
    if (json_str(js, key, artist_out, artist_sz)) {
        return true;
    }
    const char *v = json_find_value_start(js, key);
    return parse_artist_from_value(v, artist_out, artist_sz);
}

static int parse_elapsed_seconds(const char *json)
{
    static const char *elapsed_keys[] = {
        "elapsedSeconds", "elapsed", "elapsedTime", "currentTime", "position"
    };

    double elapsed = -1.0;
    for (int i = 0; i < (int)(sizeof(elapsed_keys) / sizeof(elapsed_keys[0])); i++) {
        if (json_number(json, elapsed_keys[i], &elapsed)) {
            break;
        }
    }

    if (elapsed < 0.0) {
        return -1;
    }
    return (int)elapsed;
}

static int parse_duration_seconds(const char *json)
{
    static const char *duration_keys[] = {
        "durationSeconds", "songDuration", "duration", "totalTime", "lengthSeconds"
    };

    double duration = -1.0;
    for (int i = 0; i < (int)(sizeof(duration_keys) / sizeof(duration_keys[0])); i++) {
        if (json_number(json, duration_keys[i], &duration)) {
            break;
        }
    }

    if (duration <= 0.0) {
        return -1;
    }
    return (int)duration;
}

static bool parse_paused_state(const char *json, bool *paused_out)
{
    const char *v = json_find_value_start(json, "isPaused");
    if (!v) {
        v = json_find_value_start(json, "paused");
    }
    if (!v) {
        return false;
    }

    if (strncmp(v, "true", 4) == 0 || strncmp(v, "\"true\"", 6) == 0 || *v == '1') {
        *paused_out = true;
        return true;
    }
    if (strncmp(v, "false", 5) == 0 || strncmp(v, "\"false\"", 7) == 0 || *v == '0') {
        *paused_out = false;
        return true;
    }
    return false;
}

static int parse_seek_percent(const char *json)
{
    static const char *elapsed_keys[] = {
        "elapsedSeconds", "elapsed", "elapsedTime", "currentTime", "position"
    };
    static const char *duration_keys[] = {
        "durationSeconds", "songDuration", "duration", "totalTime", "lengthSeconds"
    };
    double elapsed = -1.0;
    double duration = -1.0;

    for (int i = 0; i < (int)(sizeof(elapsed_keys) / sizeof(elapsed_keys[0])); i++) {
        if (json_number(json, elapsed_keys[i], &elapsed)) break;
    }
    for (int i = 0; i < (int)(sizeof(duration_keys) / sizeof(duration_keys[0])); i++) {
        if (json_number(json, duration_keys[i], &duration)) break;
    }
    if (elapsed >= 0.0 && duration > 0.0) {
        int pct = (int)((elapsed * 100.0) / duration);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        return pct;
    }

    /* Fallback: only trust songProgress/progress when they are ratio-like (0.0 ~ 1.0).
       Ignore generic percentage-like values to prevent 100% spikes on pause/resume. */
    double progress = -1.0;
    if (json_number(json, "songProgress", &progress) && progress >= 0.0) {
        int pct = (progress <= 1.0) ? (int)(progress * 100.0) :
                  (progress <= 100.0 ? (int)progress : -1);
        if (pct >= 0) {
            if (pct > 100) pct = 100;
            return pct;
        }
    }

    progress = -1.0;
    if (json_number(json, "progress", &progress) && progress >= 0.0 && progress <= 1.0) {
        int pct = (int)(progress * 100.0);
        if (pct > 100) pct = 100;
        return pct;
    }

    return -1;
}

/* Scan thumbnails array for the entry with width closest to ART_W.
   Fills art_out with the best URL found. */
static void pick_thumbnail_url(const char *json, char *art_out, int art_sz)
{
    /* Find the thumbnails array */
    const char *arr = strstr(json, "\"thumbnails\"");
    if (!arr) return;
    arr = strchr(arr + 12, '[');
    if (!arr) return;
    arr++;                                  /* step past '[' */

    int  best_diff = INT_MAX;
    char cur_url[512];
    char best_url[512];
    best_url[0] = '\0';

    /* Iterate over objects inside the array */
    const char *p = arr;
    while (*p && *p != ']') {
        /* Find next object start */
        while (*p && *p != '{' && *p != ']') p++;
        if (*p != '{') break;

        /* Find matching closing brace (no nesting inside thumbnail objs) */
        const char *obj_end = strchr(p + 1, '}');
        if (!obj_end) break;

        /* Copy this object into a scratch buffer */
        int obj_len = (int)(obj_end - p) + 1;
        char obj[600];
        if (obj_len > (int)sizeof(obj) - 1) obj_len = (int)sizeof(obj) - 1;
        memcpy(obj, p, obj_len);
        obj[obj_len] = '\0';

        /* Extract url and width */
        cur_url[0] = '\0';
        char width_str[16] = {0};
        json_str(obj, "url",   cur_url,   sizeof(cur_url));
        json_str(obj, "width", width_str, sizeof(width_str));

        if (cur_url[0] && width_str[0]) {
            int w    = atoi(width_str);
            int diff = w - ART_W;
            if (diff < 0) diff = -diff;
            if (diff < best_diff) {
                best_diff = diff;
                strncpy(best_url, cur_url, sizeof(best_url) - 1);
            }
        }

        p = obj_end + 1;
    }

    if (best_url[0])
        strncpy(art_out, best_url, art_sz - 1);
}

static void parse_artists_name(const char *js, char *artist_out, int artist_sz)
{
    artist_out[0] = '\0';
    const char *v = json_find_value_start(js, "artists");
    parse_artist_from_value(v, artist_out, artist_sz);
}

static void parse_song(const char *js, char *vid_out, int vid_sz,
                       char *art_out, int art_sz,
                       char *title_out, int title_sz,
                       char *artist_out, int artist_sz)
{
    vid_out[0] = '\0';
    art_out[0] = '\0';
    title_out[0] = '\0';
    artist_out[0] = '\0';

    /* --- video ID ---
       Try "video":{..."id":"VALUE"} first, then root "videoId" */
    const char *vp = strstr(js, "\"video\"");
    if (vp) {
        vp = strchr(vp, '{');
        if (vp) {
            /* Excerpt up to ~1KB covering the video object */
            char excerpt[1024];
            size_t n = strlen(vp);
            if (n > sizeof(excerpt) - 1) n = sizeof(excerpt) - 1;
            memcpy(excerpt, vp, n);
            excerpt[n] = '\0';
            json_str(excerpt, "id", vid_out, vid_sz);
        }
    }
    if (!vid_out[0])
        json_str(js, "videoId", vid_out, vid_sz);

    /* --- art URL (in priority order) --- */
    pick_thumbnail_url(js, art_out, art_sz);
    if (!art_out[0]) json_str(js, "imageSrc", art_out, art_sz);
    if (!art_out[0]) json_str(js, "cover",    art_out, art_sz);

    /* --- title / artist --- */
    json_str(js, "title", title_out, title_sz);
    if (!title_out[0]) json_str(js, "name", title_out, title_sz);
    if (!title_out[0]) strncpy(title_out, "-", title_sz - 1);

    parse_artist_by_key(js, "artist", artist_out, artist_sz);
    if (!artist_out[0]) parse_artist_by_key(js, "author", artist_out, artist_sz);
    if (!artist_out[0]) parse_artist_by_key(js, "artistName", artist_out, artist_sz);
    if (!artist_out[0]) parse_artist_by_key(js, "artistNames", artist_out, artist_sz);
    if (!artist_out[0]) parse_artists_name(js, artist_out, artist_sz);
    if (!artist_out[0]) parse_artist_by_key(js, "subtitle", artist_out, artist_sz);
    if (!artist_out[0]) strncpy(artist_out, "-", artist_sz - 1);
}

/* ------------------------------------------------------------------ */
/* Display                                                              */
/* ------------------------------------------------------------------ */

static void display_loadingbar(bool visible, int percent)
{
    if (!lvgl_lock(200)) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    app_ui_set_loading_progress(visible, percent);

    lvgl_unlock();
}

static void display_loading_spinner(bool visible)
{
    if (!lvgl_lock(200)) return;
    app_ui_set_loading_spinner_visible(visible);

    lvgl_unlock();
}

static void display_seek_arc(int percent)
{
    if (!lvgl_lock(200)) return;

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    if (objects.seek) {
        lv_arc_set_value(objects.seek, percent);
    }

    lvgl_unlock();
}

static void display_song_meta(const char *title, const char *artist)
{
    if (!lvgl_lock(200)) return;

    if (objects.title) {
        lv_label_set_text(objects.title, (title && title[0]) ? title : "-");
    }
    if (objects.artist) {
        lv_label_set_text(objects.artist, (artist && artist[0]) ? artist : "-");
    }

    lvgl_unlock();
}

static void display_main_startup_status(const char *status)
{
    if (!lvgl_lock(200)) return;

    app_ui_set_main_startup_status(status);

    lvgl_unlock();
}

static void display_main_startup_done(void)
{
    if (!lvgl_lock(200)) return;

    app_ui_finish_main_startup_status();

    lvgl_unlock();
}

static void display_art(void)
{
    if (!lvgl_lock(1000)) return;

    s_art_dsc.header.cf          = LV_IMG_CF_TRUE_COLOR;
    s_art_dsc.header.always_zero = 0;
    s_art_dsc.header.reserved    = 0;
    s_art_dsc.header.w           = ART_W;
    s_art_dsc.header.h           = ART_H;
    s_art_dsc.data_size          = ART_BUF_SIZE;
    s_art_dsc.data               = s_art_buf;

    app_ui_set_info_background_art(&s_art_dsc);

    if (objects.album_art) {
        int dst_w = lv_obj_get_width(objects.album_art);
        int dst_h = lv_obj_get_height(objects.album_art);
        if (dst_w <= 0) dst_w = ART_W;
        if (dst_h <= 0) dst_h = ART_H;

        lv_img_set_src(objects.album_art, &s_art_dsc);
        lv_obj_set_size(objects.album_art, dst_w, dst_h);
        lv_img_set_zoom(objects.album_art, (uint16_t)((dst_w * 256) / ART_W));
        lv_obj_invalidate(objects.album_art);
    }

    if (objects.art2) {
        if (s_art2_buf) {
            resize_rgb565_swapped(s_art_buf, ART_W, ART_H, s_art2_buf, ART2_W, ART2_H);

            s_art2_dsc.header.cf          = LV_IMG_CF_TRUE_COLOR;
            s_art2_dsc.header.always_zero = 0;
            s_art2_dsc.header.reserved    = 0;
            s_art2_dsc.header.w           = ART2_W;
            s_art2_dsc.header.h           = ART2_H;
            s_art2_dsc.data_size          = ART2_BUF_SIZE;
            s_art2_dsc.data               = s_art2_buf;

            lv_img_set_src(objects.art2, &s_art2_dsc);
            lv_obj_set_size(objects.art2, ART2_W, ART2_H);
            lv_img_set_zoom(objects.art2, 256);
            lv_obj_invalidate(objects.art2);
        }
    }

    lvgl_unlock();
}

/* ------------------------------------------------------------------ */
/* Main task                                                            */
/* ------------------------------------------------------------------ */

static void wait_for_ip(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) return;
    esp_netif_ip_info_t ip_info;
    for (;;) {
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) return;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void ytmd_task(void *arg)
{
    /* Allocate large buffers in PSRAM */
    s_art_buf = heap_caps_malloc(ART_BUF_SIZE, MALLOC_CAP_SPIRAM);
    s_art2_buf = heap_caps_malloc(ART2_BUF_SIZE, MALLOC_CAP_SPIRAM);
    s_dl_buf  = heap_caps_malloc(JPEG_DL_MAX,  MALLOC_CAP_SPIRAM);
    if (!s_art_buf || !s_art2_buf || !s_dl_buf) {
        ESP_LOGE(TAG, "Cannot alloc buffers (art=%d art2=%d dl=%d) spiram_free=%d task exiting",
                 ART_BUF_SIZE, ART2_BUF_SIZE, JPEG_DL_MAX,
                 (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        vTaskDelete(NULL);
        return;
    }
    memset(s_art_buf, 0, ART_BUF_SIZE);
    memset(s_art2_buf, 0, ART2_BUF_SIZE);
    display_loading_spinner(true);
    display_loadingbar(false, 0);

    /* Wait for WiFi IP before any network activity */
    ESP_LOGI(TAG, "Waiting for IP address...");
    wait_for_ip();
    display_loading_spinner(false);
    ESP_LOGI(TAG, "IP ready. Connecting to YTMD at %s:%d ...", ytmd_target_ip(), YTMD_PORT);
    display_main_startup_status("Searching for YTMD");

    /* Wait until YTMD server responds */
    while (!poll_ytmd()) {
        vTaskDelay(pdMS_TO_TICKS(CONNECT_RETRY_MS));
    }
    ESP_LOGI(TAG, "YTMD connected - system started");
    display_main_startup_status("Loading album art");

    char new_id[64];
    char new_art[512];
    char new_title[160];
    char new_artist[160];
    bool first_art_displayed = false;

    /* Main poll loop */
    while (1) {
        if (poll_ytmd()) {
            s_last_elapsed_seconds = parse_elapsed_seconds(s_resp_buf);
            int duration_seconds = parse_duration_seconds(s_resp_buf);
            bool paused = false;
            bool has_paused = parse_paused_state(s_resp_buf, &paused);
            int seek_pct = parse_seek_percent(s_resp_buf);
            if (seek_pct >= 0) {
                bool ignore_seek_spike = false;

                if (s_last_seek_percent >= 0) {
                    bool jumped_to_end = (seek_pct >= 100) && (s_last_seek_percent < 98);
                    bool near_real_end = (duration_seconds > 0) &&
                                         (s_last_elapsed_seconds >= 0) &&
                                         (s_last_elapsed_seconds >= (duration_seconds - 2));

                    if (jumped_to_end && !near_real_end) {
                        ignore_seek_spike = true;
                    }

                    if (!ignore_seek_spike && has_paused && paused &&
                        seek_pct > (s_last_seek_percent + 1)) {
                        ignore_seek_spike = true;
                    }
                }

                if (!ignore_seek_spike) {
                    display_seek_arc(seek_pct);
                    s_last_seek_percent = seek_pct;
                } else {
                    ESP_LOGW(TAG, "seek spike ignored prev=%d new=%d paused=%d elapsed=%d duration=%d",
                             (int)s_last_seek_percent, seek_pct, has_paused ? (int)paused : -1,
                             (int)s_last_elapsed_seconds, duration_seconds);
                }
            }

            parse_song(s_resp_buf, new_id, sizeof(new_id), new_art, sizeof(new_art),
                       new_title, sizeof(new_title), new_artist, sizeof(new_artist));

            if (strcmp(new_title, s_cur_title) != 0 || strcmp(new_artist, s_cur_artist) != 0) {
                strncpy(s_cur_title, new_title, sizeof(s_cur_title) - 1);
                s_cur_title[sizeof(s_cur_title) - 1] = '\0';
                strncpy(s_cur_artist, new_artist, sizeof(s_cur_artist) - 1);
                s_cur_artist[sizeof(s_cur_artist) - 1] = '\0';
                ESP_LOGI(TAG, "Meta title='%s' artist='%s'", s_cur_title, s_cur_artist);
                display_song_meta(s_cur_title, s_cur_artist);
            }

            /* Song changed? */
            if (new_id[0] != '\0' && strcmp(new_id, s_cur_video_id) != 0) {
                strncpy(s_cur_video_id, new_id, sizeof(s_cur_video_id) - 1);
                s_cur_video_id[sizeof(s_cur_video_id) - 1] = '\0';
                s_last_seek_percent = -1;
                encoder_control_reset_counter();
                ESP_LOGI(TAG, "Song: %s  art: %.80s", new_id, new_art);

                if (new_art[0] != '\0' && download_art(new_art)) {
                    if (decode_jpeg(s_dl_buf, s_dl_len, s_art_buf)) {
                        display_art();
                        display_loadingbar(false, 100);
                        if (!first_art_displayed) {
                            first_art_displayed = true;
                            display_main_startup_done();
                        }
                    } else {
                        display_loadingbar(false, 0);
                    }
                } else if (new_art[0] == '\0') {
                    display_loading_spinner(false);
                    display_loadingbar(false, 0);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

esp_err_t ytmd_client_start(void)
{
    BaseType_t ret = xTaskCreate(ytmd_task, "ytmd", 8 * 1024, NULL, 3, NULL);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
