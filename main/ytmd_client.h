#ifndef YTMD_CLIENT_H
#define YTMD_CLIENT_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the YTMD client task.
 * Waits for TCP connection to YTMD_IP:YTMD_PORT, then polls /api/v1/song.
 * On song change, downloads and displays album art on objects.album_art.
 */
esp_err_t ytmd_client_start(void);

#ifdef __cplusplus
}
#endif

#endif /* YTMD_CLIENT_H */
