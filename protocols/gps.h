#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    double latitude;
    double longitude;
    double speed;   // km/h
    double course;
    bool valid;
} gps_data_t;

void gps_init(void);
void gps_task(void *arg);
gps_data_t gps_get_data(void);

#ifdef __cplusplus
}
#endif
