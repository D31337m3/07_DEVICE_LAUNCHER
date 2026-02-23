#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Open the built-in FileServer app (SoftAP + web-based upload/download/editor).
esp_err_t ui_fileserver_open(void);

#ifdef __cplusplus
}
#endif
