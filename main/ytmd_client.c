#include "ytmd_client.h"
#include "lvgl_lock.h"
#include "ui/screens.h"
#include "tjpgd.h"

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

#define YTMD_IP          "192.168.0.30"
#define YTMD_PORT        26538
#define POLL_INTERVAL_MS 2000
#define CONNECT_RETRY_MS 3000

#define ART_W  360
#define ART_H  360
#define ART_BUF_SIZE (ART_W * ART_H * 2)

#define BUF_SCALE_X2  2

#define JPEG_DL_MAX   (BUF_SCALE_X2 * 512 * 1024)
#define RESP_BUF_MAX  (BUF_SCALE_X2 * 4096)
#define TJPGD_POOL_SZ (BUF_SCALE_X2 * 3500)

/* Persistent decoded pixel buffer (RGB565, 360x360) */
static uint8_t *s_art_buf = NULL;
static lv_img_dsc_t s_art_dsc;

/* Per-download scratch buffer (allocated once, reused) */
static uint8_t *s_dl_buf = NULL;
static int      s_dl_len = 0;

/* State */
static char s_cur_video_id[64] = {0};

/* HTTP poll response buffer */
static char s_resp_buf[RESP_BUF_MAX];
static int  s_resp_len = 0;

/* ------------------------------------------------------------------ */
/* JPEG decoder                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    const uint8_t *src;
    int            src_len;
    int            src_pos;
    uint16_t      *dst;
    int            dst_w;
    int            dst_h;
    int            src_w;   /* tjpgd decoded width  (after scale) */
    int            src_h;   /* tjpgd decoded height (after scale) */
} jpg_ctx_t;

static size_t jpg_infunc(JDEC *jd, uint8_t *buf, size_t nb)
{
    jpg_ctx_t *ctx = (jpg_ctx_t *)jd->device;
    int avail = ctx->src_len - ctx->src_pos;
    if (avail <= 0) return 0;
    if ((int)nb > avail) nb = (size_t)avail;
    if (buf) memcpy(buf, ctx->src + ctx->src_pos, nb);
    ctx->src_pos += (int)nb;
    return nb;
}

static int jpg_outfunc(JDEC *jd, void *data, JRECT *rect)
{
    jpg_ctx_t *ctx = (jpg_ctx_t *)jd->device;
    uint16_t  *src = (uint16_t *)data;

    for (int y = rect->top; y <= (int)rect->bottom; y++) {
        for (int x = rect->left; x <= (int)rect->right; x++) {
            /* Nearest-neighbor scale: map tjpgd output coords → dst coords */
            int dx = x * ctx->dst_w / ctx->src_w;
            int dy = y * ctx->dst_h / ctx->src_h;
            if ((unsigned)dx < (unsigned)ctx->dst_w &&
                (unsigned)dy < (unsigned)ctx->dst_h) {
                uint16_t px = *src;
                /* Byte-swap for LV_COLOR_16_SWAP */
                ctx->dst[dy * ctx->dst_w + dx] = (px << 8) | (px >> 8);
            }
            src++;
        }
    }
    return 1; /* continue */
}

static bool decode_jpeg(const uint8_t *jpeg, int jpeg_len, uint8_t *out_rgb565)
{
    static uint8_t pool[TJPGD_POOL_SZ];
    JDEC jd;
    jpg_ctx_t ctx = {
        .src     = jpeg,
        .src_len = jpeg_len,
        .src_pos = 0,
        .dst     = (uint16_t *)out_rgb565,
        .dst_w   = ART_W,
        .dst_h   = ART_H,
    };

    JRESULT res = jd_prepare(&jd, jpg_infunc, pool, sizeof(pool), &ctx);
    if (res != JDR_OK) {
        ESP_LOGE(TAG, "jd_prepare: %d", res);
        return false;
    }

    /* Pick largest tjpgd scale that still covers ART_W x ART_H.
       Then nearest-neighbor downscale to exactly ART_W x ART_H in outfunc. */
    uint8_t scale = 0;
    while (scale < 3) {
        int nw = (int)(jd.width  >> (scale + 1));
        int nh = (int)(jd.height >> (scale + 1));
        if (nw < ART_W || nh < ART_H) break;
        scale++;
    }

    int out_w = (int)(jd.width  >> scale);
    int out_h = (int)(jd.height >> scale);
    ctx.src_w = out_w;
    ctx.src_h = out_h;

    memset(out_rgb565, 0, ART_BUF_SIZE);

    res = jd_decomp(&jd, jpg_outfunc, scale);
    if (res != JDR_OK) {
        ESP_LOGE(TAG, "jd_decomp: %d (img %dx%d scale=%d)", res, jd.width, jd.height, scale);
        return false;
    }

    ESP_LOGI(TAG, "JPEG decoded %dx%d -> %dx%d -> %dx%d (scale=%d)",
             jd.width, jd.height, out_w, out_h, ART_W, ART_H, scale);
    return true;
}

/* ------------------------------------------------------------------ */
/* HTTP download                                                        */
/* ------------------------------------------------------------------ */

static esp_err_t dl_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (s_dl_len + evt->data_len <= JPEG_DL_MAX) {
            memcpy(s_dl_buf + s_dl_len, evt->data, evt->data_len);
            s_dl_len += evt->data_len;
        } else {
            ESP_LOGW(TAG, "Download buffer full, truncating at %d bytes", s_dl_len);
        }
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
    s_dl_len = 0;

    char http_url[512];
    make_http_url(url, http_url, sizeof(http_url));

    esp_http_client_config_t cfg = {
        .url           = http_url,
        .event_handler = dl_event_handler,
        .timeout_ms    = 8000,
        .buffer_size   = 8192,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    esp_err_t err    = esp_http_client_perform(client);
    int       status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "Art download failed err=%d status=%d url=%s", err, status, http_url);
        return false;
    }
    ESP_LOGI(TAG, "Art downloaded %d bytes", s_dl_len);
    return s_dl_len > 100; /* sanity: at least a tiny JPEG */
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
    char url[64];
    snprintf(url, sizeof(url), "http://" YTMD_IP ":%d/api/v1/song", YTMD_PORT);

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
        if (*p != ':') continue;           /* key without colon – skip */
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

static void parse_song(const char *js, char *vid_out, int vid_sz,
                       char *art_out, int art_sz)
{
    vid_out[0] = '\0';
    art_out[0] = '\0';

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
}

/* ------------------------------------------------------------------ */
/* Display                                                              */
/* ------------------------------------------------------------------ */

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

    lv_img_set_src(objects.album_art, &s_art_dsc);
    lv_obj_invalidate(objects.album_art);

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
    s_dl_buf  = heap_caps_malloc(JPEG_DL_MAX,  MALLOC_CAP_SPIRAM);
    if (!s_art_buf || !s_dl_buf) {
        ESP_LOGE(TAG, "Cannot alloc buffers (art=%d dl=%d) spiram_free=%d – task exiting",
                 ART_BUF_SIZE, JPEG_DL_MAX, (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        vTaskDelete(NULL);
        return;
    }
    memset(s_art_buf, 0, ART_BUF_SIZE);

    /* Wait for WiFi IP before any network activity */
    ESP_LOGI(TAG, "Waiting for IP address...");
    wait_for_ip();
    ESP_LOGI(TAG, "IP ready. Connecting to YTMD at %s:%d ...", YTMD_IP, YTMD_PORT);

    /* Wait until YTMD server responds */
    while (!poll_ytmd()) {
        vTaskDelay(pdMS_TO_TICKS(CONNECT_RETRY_MS));
    }
    ESP_LOGI(TAG, "YTMD connected – system started");

    char new_id[64];
    char new_art[512];

    /* Main poll loop */
    while (1) {
        if (poll_ytmd()) {
            parse_song(s_resp_buf, new_id, sizeof(new_id), new_art, sizeof(new_art));

            /* Song changed? */
            if (new_id[0] != '\0' && strcmp(new_id, s_cur_video_id) != 0) {
                strncpy(s_cur_video_id, new_id, sizeof(s_cur_video_id) - 1);
                ESP_LOGI(TAG, "Song: %s  art: %.80s", new_id, new_art);

                if (new_art[0] != '\0' && download_art(new_art)) {
                    if (decode_jpeg(s_dl_buf, s_dl_len, s_art_buf)) {
                        display_art();
                    }
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
