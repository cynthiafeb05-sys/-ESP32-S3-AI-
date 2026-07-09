#ifndef __ESP8266_ONENET_H
#define __ESP8266_ONENET_H

#include "stm32f10x.h"
#include <stdint.h>

#define ESP8266_WIFI_SSID       "vivo X100"
#define ESP8266_WIFI_PWD        "123456789"

#define ONENET_MQTT_HOST        "mqtts.heclouds.com"
#define ONENET_MQTT_PORT        1883

#define ONENET_PRODUCT_ID       "6V1ids8245"
#define ONENET_DEVICE_NAME      "dev-001"
#define ONENET_MQTT_PASSWORD    "version=2018-10-31&res=products%2F6V1ids8245%2Fdevices%2Fdev-001&et=1805615547&method=md5&sign=7ZJhq1jw%2BmGgKUASocTerA%3D%3D"

#define ONENET_TOPIC_PROPERTY_POST  "$sys/6V1ids8245/dev-001/thing/property/post"
#define ONENET_TOPIC_PROPERTY_SET   "$sys/6V1ids8245/dev-001/thing/property/set"
#define ONENET_TOPIC_PROPERTY_REPLY "$sys/6V1ids8245/dev-001/thing/property/post/reply"

#define ESP8266_RX_BUF_SIZE     1024

typedef struct
{
    float temperature;
    float humidity;
    uint16_t air_quality;
    uint16_t light_value;
    uint16_t distance_mm;
    uint16_t heart_bpm;
    /* MPU6050 attitude angles, stored as angle * 10. Example: 123 means 12.3 degrees. */
    int16_t pitch_angle_x10;
    int16_t roll_angle_x10;
    int16_t delta_pitch_angle_x10;
    int16_t delta_roll_angle_x10;
    /* 1 means the helmet posture exceeds the fall threshold, 0 means normal. */
    uint8_t fall_warn;
    uint8_t fan_state;
    uint8_t fan_speed;
    uint8_t led_state;
    uint8_t led_brightness;
} onenet_sensor_payload_t;

void ESP8266_ONENET_Init(void);
uint8_t ESP8266_ONENET_IsConnected(void);
uint8_t ESP8266_ONENET_Connect(void);
uint8_t ESP8266_ONENET_Publish(const onenet_sensor_payload_t *payload);
void ESP8266_USART3_IRQHandler(void);

#endif
