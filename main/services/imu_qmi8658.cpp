#include "services/imu_qmi8658.h"

#include "esp_log.h"

#include "SensorLib.h"
#include "SensorQMI8658.hpp"

static const char *TAG = "imu";

static SensorQMI8658 s_qmi;
static bool s_ready = false;

bool imu_qmi8658_init(i2c_master_bus_handle_t bus)
{
    // Demo pack uses addr 0x6B and SDA/SCL on 15/14.
    if (!s_qmi.begin(bus, 0x6B)) {
        ESP_LOGW(TAG, "QMI8658 not detected");
        s_ready = false;
        return false;
    }

    s_qmi.configAccelerometer(
        SensorQMI8658::ACC_RANGE_4G,
        SensorQMI8658::ACC_ODR_1000Hz,
        SensorQMI8658::LPF_MODE_0,
        true);
    s_qmi.configGyroscope(
        SensorQMI8658::GYR_RANGE_64DPS,
        SensorQMI8658::GYR_ODR_896_8Hz,
        SensorQMI8658::LPF_MODE_3,
        true);
    s_qmi.enableGyroscope();
    s_qmi.enableAccelerometer();
    s_ready = true;
    ESP_LOGI(TAG, "QMI8658 init OK (chip=0x%02x)", s_qmi.getChipID());
    return true;
}

bool imu_qmi8658_read(float *ax, float *ay, float *az, float *gx, float *gy, float *gz)
{
    if (!s_ready) {
        return false;
    }
    if (!s_qmi.getDataReady()) {
        return false;
    }
    IMUdata acc = {};
    IMUdata gyr = {};
    if (!s_qmi.getAccelerometer(acc.x, acc.y, acc.z)) {
        return false;
    }
    if (!s_qmi.getGyroscope(gyr.x, gyr.y, gyr.z)) {
        return false;
    }
    if (ax) *ax = acc.x;
    if (ay) *ay = acc.y;
    if (az) *az = acc.z;
    if (gx) *gx = gyr.x;
    if (gy) *gy = gyr.y;
    if (gz) *gz = gyr.z;
    return true;
}
