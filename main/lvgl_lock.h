#ifndef LVGL_LOCK_H
#define LVGL_LOCK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool lvgl_lock(int timeout_ms);
void lvgl_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* LVGL_LOCK_H */
