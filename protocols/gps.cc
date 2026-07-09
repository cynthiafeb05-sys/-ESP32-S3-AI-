#include "gps.h"

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#define GPS_UART_NUM UART_NUM_1
// NEO-6M RX -> ESP32S3 GPIO17, NEO-6M TX -> ESP32S3 GPIO16.
#define GPS_UART_TX_PIN 17
#define GPS_UART_RX_PIN 16
#define BUF_SIZE 1024

static const char* TAG = "GPS";

static gps_data_t latest_data = {};
static SemaphoreHandle_t gps_mutex = nullptr;

static double nmea_to_decimal(double raw)
{
    int deg = (int)(raw / 100);
    double min = raw - deg * 100;
    return deg + min / 60.0;
}

static bool nmea_checksum_ok(const char* line)
{
    if (line == nullptr || line[0] != '$') {
        return false;
    }

    const char* star = strchr(line, '*');
    if (star == nullptr || strlen(star) < 3) {
        return false;
    }

    uint8_t checksum = 0;
    for (const char* p = line + 1; p < star; ++p) {
        checksum ^= (uint8_t)(*p);
    }

    char expected[3] = {star[1], star[2], 0};
    char* end = nullptr;
    long received = strtol(expected, &end, 16);
    return end != expected && checksum == (uint8_t)received;
}

static void update_latest_data(const gps_data_t& data)
{
    if (gps_mutex != nullptr) {
        xSemaphoreTake(gps_mutex, portMAX_DELAY);
    }
    latest_data = data;
    if (gps_mutex != nullptr) {
        xSemaphoreGive(gps_mutex);
    }
}

static void parse_gprmc(char* line)
{
    if (strlen(line) < 50) {
        return;
    }

    if (strncmp(line, "$GNRMC", 6) != 0 &&
        strncmp(line, "$GPRMC", 6) != 0) {
        return;
    }

    if (!nmea_checksum_ok(line)) {
        ESP_LOGW(TAG, "Invalid checksum: %s", line);
        return;
    }

    static double last_lat = 0;
    static double last_lon = 0;
    static uint32_t last_update_time = 0;

    char buffer[256];
    strncpy(buffer, line, sizeof(buffer));
    buffer[sizeof(buffer) - 1] = 0;

    char* token = nullptr;
    char* rest = buffer;
    int field = 0;

    double lat = 0;
    double lon = 0;
    double speed = 0;
    double course = 0;
    char lat_dir = 'N';
    char lon_dir = 'E';
    char status = 'V';

    while ((token = strtok_r(rest, ",", &rest)) != nullptr) {
        field++;
        switch (field) {
            case 3: status = token[0]; break;
            case 4: lat = atof(token); break;
            case 5: lat_dir = token[0]; break;
            case 6: lon = atof(token); break;
            case 7: lon_dir = token[0]; break;
            case 8: speed = atof(token); break;
            case 9: course = atof(token); break;
            default: break;
        }
    }

    if (status != 'A') {
        ESP_LOGW(TAG, "No GPS fix");
        return;
    }

    if (lat == 0 || lon == 0) {
        return;
    }

    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now - last_update_time < 1000) {
        return;
    }
    last_update_time = now;

    double lat_d = nmea_to_decimal(lat);
    double lon_d = nmea_to_decimal(lon);
    if (lat_dir == 'S') {
        lat_d = -lat_d;
    }
    if (lon_dir == 'W') {
        lon_d = -lon_d;
    }

    if (fabs(lat_d - last_lat) < 0.00001 &&
        fabs(lon_d - last_lon) < 0.00001) {
        return;
    }

    last_lat = lat_d;
    last_lon = lon_d;

    gps_data_t data = {};
    data.latitude = lat_d;
    data.longitude = lon_d;
    data.speed = speed * 1.852;
    data.course = course;
    data.valid = true;
    update_latest_data(data);

    ESP_LOGI(TAG, "FIX: %.6f, %.6f | %.2f km/h", data.latitude, data.longitude, data.speed);
}

void gps_init(void)
{
    if (gps_mutex == nullptr) {
        gps_mutex = xSemaphoreCreateMutex();
    }

    uart_config_t cfg = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        .source_clk = UART_SCLK_DEFAULT
#endif
    };

    ESP_ERROR_CHECK(uart_param_config(GPS_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(GPS_UART_NUM, GPS_UART_TX_PIN, GPS_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(GPS_UART_NUM, BUF_SIZE * 2, 0, 0, nullptr, 0));

    ESP_LOGI(TAG, "GPS UART initialized, TX=%d RX=%d", GPS_UART_TX_PIN, GPS_UART_RX_PIN);
}

gps_data_t gps_get_data(void)
{
    gps_data_t data;
    if (gps_mutex != nullptr) {
        xSemaphoreTake(gps_mutex, portMAX_DELAY);
    }
    data = latest_data;
    if (gps_mutex != nullptr) {
        xSemaphoreGive(gps_mutex);
    }
    return data;
}

void gps_task(void* arg)
{
    uint8_t data[BUF_SIZE];
    static char line_buf[256];
    int line_pos = 0;

    while (true) {
        int len = uart_read_bytes(GPS_UART_NUM, data, BUF_SIZE, pdMS_TO_TICKS(1000));

        for (int i = 0; i < len; i++) {
            char c = (char)data[i];
            if (c == '\n') {
                line_buf[line_pos] = 0;
                parse_gprmc(line_buf);
                line_pos = 0;
            } else if (c != '\r') {
                if (line_pos < (int)sizeof(line_buf) - 1) {
                    line_buf[line_pos++] = c;
                } else {
                    line_pos = 0;
                }
            }
        }
    }
}
