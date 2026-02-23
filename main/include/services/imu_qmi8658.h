#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

bool imu_qmi8658_init(i2c_master_bus_handle_t bus);
bool imu_qmi8658_read(float *ax, float *ay, float *az, float *gx, float *gy, float *gz);

#ifdef __cplusplus
}
#endif
