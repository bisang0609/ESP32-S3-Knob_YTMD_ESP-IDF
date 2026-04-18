#ifndef YTMD_CLIENT_H
#define YTMD_CLIENT_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YTMD_CMD_NEXT = 0,
    YTMD_CMD_PREVIOUS = 1,
    YTMD_CMD_PAUSE = 2,
    YTMD_CMD_PLAY = 3,
} ytmd_cmd_t;

/* Start the YTMD client task.
 * Waits for TCP connection to configured targetIP:YTMD_PORT, then polls /api/v1/song.
 * On song change, downloads and displays album art on objects.album_art.
 */
esp_err_t ytmd_client_start(void);

/* Send transport command to YTMD server (next/previous track). */
esp_err_t ytmd_client_send_command(ytmd_cmd_t cmd);

#ifdef __cplusplus
}
#endif

#endif /* YTMD_CLIENT_H */
