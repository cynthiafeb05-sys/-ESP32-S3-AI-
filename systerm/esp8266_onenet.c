#include "esp8266_onenet.h"
#include <rtthread.h>
#include <stdio.h>
#include <string.h>

/*
 * ESP8266 OneNET模块说明：
 * 1. USART3连接ESP8266，发送AT指令完成WiFi和MQTT连接；
 * 2. ESP8266_ONENET_Publish把温湿度、空气质量、距离、心率、姿态等数据组装为JSON并上传OneNET；
 * 3. 接收中断把ESP8266返回内容保存到缓冲区，等待AT指令函数检查关键字。
 */

static volatile uint16_t esp_rx_len = 0;          /* ESP8266接收缓冲区当前有效长度。 */
static char esp_rx_buf[ESP8266_RX_BUF_SIZE];      /* 保存USART3收到的ESP8266响应内容。 */
static uint8_t esp_mqtt_connected = 0;            /* MQTT连接状态标志：1表示已连接OneNET。 */

static void esp8266_format_x10(char *buf, uint16_t buf_size, int16_t value_x10)
{
    int16_t int_part = value_x10 / 10;
    int16_t dec_part = value_x10 % 10;

    if (dec_part < 0)
    {
        dec_part = -dec_part;
    }

    /*
     * Angles are stored as angle * 10 to avoid keeping float values in shared data.
     * This helper converts them back to a JSON-friendly decimal string and preserves
     * the minus sign for values such as -0.4 degrees.
     */
    if ((value_x10 < 0) && (int_part == 0))
    {
        rt_snprintf(buf, buf_size, "-0.%d", dec_part);
    }
    else
    {
        rt_snprintf(buf, buf_size, "%d.%d", int_part, dec_part);
    }
}

static void esp8266_usart3_send_char(char ch)
{
    while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET)
    {
    }
    USART_SendData(USART3, (uint8_t)ch);
}

static void esp8266_usart3_send_data(const char *data, uint16_t len)
{
    uint16_t i;

    for (i = 0; i < len; i++)
    {
        esp8266_usart3_send_char(data[i]);
    }
}

static void esp8266_usart3_send_string(const char *str)
{
    esp8266_usart3_send_data(str, (uint16_t)strlen(str));
}

static void esp8266_clear_rx(void)
{
    esp_rx_len = 0;
    memset(esp_rx_buf, 0, sizeof(esp_rx_buf));
}

static uint8_t esp8266_wait_reply(const char *reply1, const char *reply2, uint32_t wait_ms)
{
    uint32_t waited = 0;

    while (waited < wait_ms)
    {
        esp_rx_buf[esp_rx_len] = '\0';
        if ((reply1 != RT_NULL) && (strstr(esp_rx_buf, reply1) != RT_NULL))
        {
            return 1;
        }
        if ((reply2 != RT_NULL) && (strstr(esp_rx_buf, reply2) != RT_NULL))
        {
            return 1;
        }
        rt_thread_mdelay(20);
        waited += 20;
    }

    esp_rx_buf[esp_rx_len] = '\0';
    return 0;
}

static uint8_t esp8266_cmd(const char *cmd, const char *reply1, const char *reply2, uint32_t wait_ms)
{
    esp8266_clear_rx();

    if ((cmd != RT_NULL) && (cmd[0] != '\0'))
    {
        esp8266_usart3_send_string(cmd);
        esp8266_usart3_send_string("\r\n");
    }

    if ((reply1 == RT_NULL) && (reply2 == RT_NULL))
    {
        return 1;
    }

    if (esp8266_wait_reply(reply1, reply2, wait_ms))
    {
        return 1;
    }

    rt_kprintf("ESP8266 timeout: %s\r\n", cmd ? cmd : "");
    rt_kprintf("ESP8266 rx: %s\r\n", esp_rx_buf);
    return 0;
}

static uint8_t esp8266_send_raw_after_prompt(const char *cmd, const char *data, uint16_t data_len, uint32_t wait_ms)
{
    /*
     * Some ESP-AT MQTT commands, such as AT+MQTTLONGPASSWORD, first return a
     * '>' prompt and then expect exactly data_len raw bytes without extra CRLF.
     */
    if (!esp8266_cmd(cmd, ">", RT_NULL, wait_ms))
    {
        return 0;
    }

    esp8266_clear_rx();
    esp8266_usart3_send_data(data, data_len);

    if (!esp8266_wait_reply("OK", RT_NULL, wait_ms))
    {
        rt_kprintf("ESP8266 raw command failed, rx:%s\r\n", esp_rx_buf);
        return 0;
    }

    return 1;
}

void ESP8266_ONENET_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &USART_InitStructure);

    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
    USART_ITConfig(USART3, USART_IT_IDLE, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART3, ENABLE);
}

uint8_t ESP8266_ONENET_IsConnected(void)
{
    return esp_mqtt_connected;
}

uint8_t ESP8266_ONENET_Connect(void)
{
    char cmd[360];
    uint16_t password_len;
    uint8_t retry;

    esp_mqtt_connected = 0;
    rt_kprintf("ESP8266 OneNET connecting...\r\n");

    for (retry = 0; retry < 5; retry++)
    {
        if (esp8266_cmd("AT", "OK", RT_NULL, 1000))
        {
            break;
        }
        rt_thread_mdelay(1000);
    }

    if (retry >= 5)
    {
        rt_kprintf("ESP8266 AT failed\r\n");
        return 0;
    }

    (void)esp8266_cmd("ATE0", "OK", RT_NULL, 1000);
    (void)esp8266_cmd("AT+CWMODE=1", "OK", "no change", 2000);
    (void)esp8266_cmd("AT+CWDHCP=1,1", "OK", RT_NULL, 1000);
    (void)esp8266_cmd("AT+MQTTCLEAN=0", "OK", "ERROR", 1000);

    rt_snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ESP8266_WIFI_SSID, ESP8266_WIFI_PWD);
    if (!esp8266_cmd(cmd, "OK", "WIFI GOT IP", 15000))
    {
        rt_kprintf("ESP8266 WiFi failed\r\n");
        return 0;
    }

    /*
     * Keep MQTTUSERCFG short. OneNET's token password is long, and many ESP-AT
     * firmwares return ERROR if the whole MQTTUSERCFG command is too long.
     * The password is configured separately below.
     */
    rt_snprintf(cmd, sizeof(cmd),
                "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"\",0,0,\"\"",
                ONENET_DEVICE_NAME,
                ONENET_PRODUCT_ID);
    if (!esp8266_cmd(cmd, "OK", RT_NULL, 3000))
    {
        rt_kprintf("ESP8266 MQTTUSERCFG failed\r\n");
        return 0;
    }

    password_len = (uint16_t)strlen(ONENET_MQTT_PASSWORD);
    rt_snprintf(cmd, sizeof(cmd), "AT+MQTTLONGPASSWORD=0,%d", password_len);
    if (!esp8266_send_raw_after_prompt(cmd, ONENET_MQTT_PASSWORD, password_len, 3000))
    {
        /*
         * Older ESP8266 AT firmware uses AT+MQTTPASSWORD instead of
         * AT+MQTTLONGPASSWORD. Try it as a compatibility fallback.
         */
        rt_snprintf(cmd, sizeof(cmd), "AT+MQTTPASSWORD=0,\"%s\"", ONENET_MQTT_PASSWORD);
        if (!esp8266_cmd(cmd, "OK", RT_NULL, 3000))
        {
            rt_kprintf("ESP8266 MQTTPASSWORD failed\r\n");
            return 0;
        }
    }

    rt_snprintf(cmd, sizeof(cmd), "AT+MQTTCONN=0,\"%s\",%d,0", ONENET_MQTT_HOST, ONENET_MQTT_PORT);
    if (!esp8266_cmd(cmd, "OK", "ALREADY CONNECTED", 8000))
    {
        rt_kprintf("ESP8266 MQTTCONN failed\r\n");
        return 0;
    }

    rt_snprintf(cmd, sizeof(cmd), "AT+MQTTSUB=0,\"%s\",0", ONENET_TOPIC_PROPERTY_SET);
    (void)esp8266_cmd(cmd, "OK", RT_NULL, 3000);

    /*
     * Subscribe to the property post reply topic. ESP8266 may report "publish ok"
     * even when OneNET rejects the payload because a property identifier is missing
     * from the product model. This reply shows the real OneNET result.
     */
    rt_snprintf(cmd, sizeof(cmd), "AT+MQTTSUB=0,\"%s\",0", ONENET_TOPIC_PROPERTY_REPLY);
    (void)esp8266_cmd(cmd, "OK", RT_NULL, 3000);

    esp_mqtt_connected = 1;
    rt_kprintf("ESP8266 OneNET connected\r\n");
    /* GBK编码："WiFi已连接，系统上线" */
    rt_kprintf("\xCF\xB5\xCD\xB3\xD2\xD1\xC9\xCF\xCF\xDF\xA3\xAC\xCA\xFD\xBE\xDD\xC9\xCF\xB4\xAB\xD6\xD0\x0D\x0A");
    return 1;
}

uint8_t ESP8266_ONENET_Publish(const onenet_sensor_payload_t *payload)
{
    char cmd[180];
    char json[620];
    char pitch_str[12];
    char roll_str[12];
    char delta_pitch_str[12];
    char delta_roll_str[12];
    int json_len;

    if ((payload == RT_NULL) || (esp_mqtt_connected == 0))
    {
        return 0;
    }

    {
        int temp10 = (int)(payload->temperature * 10.0f);
        int humi10 = (int)(payload->humidity * 10.0f);
        int temp_dec;
        int humi_dec;

        /* Convert MPU6050 attitude values to decimal strings before building JSON. */
        esp8266_format_x10(pitch_str, sizeof(pitch_str), payload->pitch_angle_x10);
        esp8266_format_x10(roll_str, sizeof(roll_str), payload->roll_angle_x10);
        esp8266_format_x10(delta_pitch_str, sizeof(delta_pitch_str), payload->delta_pitch_angle_x10);
        esp8266_format_x10(delta_roll_str, sizeof(delta_roll_str), payload->delta_roll_angle_x10);

        temp_dec = temp10 % 10;
        humi_dec = humi10 % 10;
        if (temp_dec < 0)
        {
            temp_dec = -temp_dec;
        }
        if (humi_dec < 0)
        {
            humi_dec = -humi_dec;
        }

        /*
         * OneNET property post format. The identifiers here must match the product
         * model identifiers configured on the OneNET platform exactly.
         */
        rt_snprintf(json, sizeof(json),
                "{\"id\":\"0\",\"version\":\"1.0\",\"params\":{"
                "\"CurrentTemperature\":{\"value\":%d.%d},"
                "\"CurrentHumidity\":{\"value\":%d.%d},"
                "\"AirQuality\":{\"value\":%d},"
                "\"LightValue\":{\"value\":%d},"
                "\"Distance\":{\"value\":%d},"
                "\"HeartRate\":{\"value\":%d},"
                "\"PitchAngle\":{\"value\":%s},"
                "\"RollAngle\":{\"value\":%s},"
                "\"DeltaPitchAngle\":{\"value\":%s},"
                "\"DeltaRollAngle\":{\"value\":%s},"
                "\"FallWarn\":{\"value\":%d},"
                "\"FanState\":{\"value\":%d},"
                "\"FanSpeed\":{\"value\":%d},"
                "\"LedState\":{\"value\":%d},"
                "\"LedBrightness\":{\"value\":%d}"
                "}}",
                temp10 / 10,
                temp_dec,
                humi10 / 10,
                humi_dec,
                payload->air_quality,
                payload->light_value,
                payload->distance_mm,
                payload->heart_bpm,
                pitch_str,
                roll_str,
                delta_pitch_str,
                delta_roll_str,
                payload->fall_warn,
                payload->fan_state,
                payload->fan_speed,
                payload->led_state,
                payload->led_brightness);
    }

    json_len = (int)strlen(json);
    rt_snprintf(cmd, sizeof(cmd), "AT+MQTTPUBRAW=0,\"%s\",%d,0,0", ONENET_TOPIC_PROPERTY_POST, json_len);

    if (!esp8266_cmd(cmd, ">", RT_NULL, 3000))
    {
        esp_mqtt_connected = 0;
        return 0;
    }

    esp8266_clear_rx();
    esp8266_usart3_send_data(json, (uint16_t)json_len);

    if (!esp8266_wait_reply("OK", "+MQTTPUB:OK", 5000))
    {
        rt_kprintf("ESP8266 publish failed, rx:%s\r\n", esp_rx_buf);
        esp_mqtt_connected = 0;
        return 0;
    }

    rt_kprintf("OneNET publish ok\r\n");

    /*
     * Wait briefly for OneNET's property post reply. If the platform does not show
     * data, this line usually contains the error code and the rejected property.
     */
    if (esp8266_wait_reply(ONENET_TOPIC_PROPERTY_REPLY, "\"code\"", 800))
    {
        if (strstr(esp_rx_buf, "\"code\":200") == RT_NULL)
        {
            rt_kprintf("OneNET reply: %s\r\n", esp_rx_buf);
        }
    }

    return 1;
}

void ESP8266_USART3_IRQHandler(void)
{
    uint8_t data;

    if (USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)
    {
        data = (uint8_t)USART_ReceiveData(USART3);
        if (esp_rx_len < (ESP8266_RX_BUF_SIZE - 1U))
        {
            esp_rx_buf[esp_rx_len++] = (char)data;
        }
        USART_ClearITPendingBit(USART3, USART_IT_RXNE);
    }

    if (USART_GetITStatus(USART3, USART_IT_IDLE) != RESET)
    {
        (void)USART3->SR;
        (void)USART3->DR;
    }
}
