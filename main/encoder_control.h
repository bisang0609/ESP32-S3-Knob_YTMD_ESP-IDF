#ifndef ENCODER_CONTROL_H
#define ENCODER_CONTROL_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t encoder_control_start(void);
void encoder_control_reset_counter(void);
int encoder_control_get_counter(void);

#ifdef __cplusplus
}
#endif

#endif /* ENCODER_CONTROL_H */
