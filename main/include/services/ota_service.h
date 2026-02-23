#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ota_service_start_from_url(const char *url);

#ifdef __cplusplus
}
#endif
