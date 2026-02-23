#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

bool time_service_init(i2c_master_bus_handle_t bus);
void time_service_start_sntp(void);
void time_service_format(char *out_time, size_t out_time_len, char *out_date, size_t out_date_len);

// Gets the current local time snapshot.
// Returns false if system time is not set to a sensible value.
bool time_service_get_localtime(struct tm *out);

#ifdef __cplusplus
}
#endif
