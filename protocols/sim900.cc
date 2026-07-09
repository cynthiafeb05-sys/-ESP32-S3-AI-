#include "sim900.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <utility>

#include "driver/uart.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SIM900_UART_NUM UART_NUM_2
// SIM900 RX -> ESP32S3 GPIO5, SIM900 TX -> ESP32S3 GPIO4.
#define SIM900_UART_TX_PIN 5
#define SIM900_UART_RX_PIN 4
#define SIM900_BUF_SIZE 1024
#define SIM900_DEFAULT_BAUD_RATE 9600

static const char* TAG = "SIM900";

static std::mutex sim900_mutex;
static std::string incoming_number;
static std::string last_dial_number;
static bool incoming_call = false;
static bool sim900_command_busy = false;
static TickType_t last_dial_tick = 0;
static std::function<void(Sim900Event, const std::string&)> event_callback;

static void notify_event(Sim900Event event, const std::string& value)
{
    std::function<void(Sim900Event, const std::string&)> callback;
    {
        std::lock_guard<std::mutex> lock(sim900_mutex);
        callback = event_callback;
    }
    if (callback) {
        callback(event, value);
    }
}

static void sim900_write_raw(const char* data, size_t len)
{
    uart_write_bytes(SIM900_UART_NUM, data, len);
}

static void sim900_write_command(const std::string& command)
{
    ESP_LOGI(TAG, ">> %s", command.c_str());
    sim900_write_raw(command.c_str(), command.size());
    sim900_write_raw("\r\n", 2);
}

static bool sim900_wait_for_text(const char* expected, uint32_t timeout_ms)
{
    uint8_t data[SIM900_BUF_SIZE];
    std::string response;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while ((int32_t)(deadline - xTaskGetTickCount()) > 0) {
        int len = uart_read_bytes(SIM900_UART_NUM, data, sizeof(data), pdMS_TO_TICKS(100));
        if (len <= 0) {
            continue;
        }
        response.append(reinterpret_cast<const char*>(data), len);
        if (response.find(expected) != std::string::npos) {
            ESP_LOGI(TAG, "<< %s", response.c_str());
            return true;
        }
    }

    if (!response.empty()) {
        ESP_LOGW(TAG, "<< unexpected: %s", response.c_str());
    }
    return false;
}

static bool sim900_probe_baud_rate(int baud_rate)
{
    uart_flush_input(SIM900_UART_NUM);
    ESP_ERROR_CHECK(uart_set_baudrate(SIM900_UART_NUM, baud_rate));
    vTaskDelay(pdMS_TO_TICKS(100));

    for (int i = 0; i < 3; ++i) {
        sim900_write_command("AT");
        if (sim900_wait_for_text("OK", 700)) {
            ESP_LOGI(TAG, "SIM900 detected at %d baud", baud_rate);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return false;
}

static void sim900_detect_baud_rate()
{
    ESP_LOGI(TAG, "Use fixed SIM900 baud rate: %d", SIM900_DEFAULT_BAUD_RATE);
    ESP_ERROR_CHECK(uart_set_baudrate(SIM900_UART_NUM, SIM900_DEFAULT_BAUD_RATE));
    uart_flush_input(SIM900_UART_NUM);
}

static void sim900_log_network_status()
{
    {
        std::lock_guard<std::mutex> lock(sim900_mutex);
        if (sim900_command_busy) {
            return;
        }
    }

    sim900_write_command("AT+CPIN?");
    vTaskDelay(pdMS_TO_TICKS(200));
    sim900_write_command("AT+CSQ");
    vTaskDelay(pdMS_TO_TICKS(200));
    sim900_write_command("AT+CREG?");
    vTaskDelay(pdMS_TO_TICKS(200));
    sim900_write_command("AT+COPS?");
}

static void sim900_configure_module()
{
    vTaskDelay(pdMS_TO_TICKS(1200));
    sim900_detect_baud_rate();
    sim900_write_command("AT");
    vTaskDelay(pdMS_TO_TICKS(300));
    sim900_write_command("ATE0");
    vTaskDelay(pdMS_TO_TICKS(300));
    sim900_write_command("AT+CFUN=1");
    vTaskDelay(pdMS_TO_TICKS(500));
    sim900_write_command("AT+CSCLK=0");
    vTaskDelay(pdMS_TO_TICKS(300));
    sim900_write_command("AT+CMEE=2");
    vTaskDelay(pdMS_TO_TICKS(300));
    sim900_write_command("AT+CLIP=1");
    vTaskDelay(pdMS_TO_TICKS(300));
    sim900_write_command("AT+CMGF=1");
    vTaskDelay(pdMS_TO_TICKS(300));
    sim900_write_command("AT+CSCS=\"GSM\"");
    vTaskDelay(pdMS_TO_TICKS(300));
    sim900_log_network_status();
}

static std::string normalize_phone_number(const std::string& text)
{
    std::string digits;
    for (char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digits.push_back(c);
        }
    }

    if (digits.size() == 11 && digits[0] == '1') {
        return "+86" + digits;
    }
    if (!digits.empty()) {
        return digits;
    }
    return text;
}

static void append_hex_byte(std::string& out, uint8_t byte)
{
    static const char* hex = "0123456789ABCDEF";
    out.push_back(hex[(byte >> 4) & 0x0F]);
    out.push_back(hex[byte & 0x0F]);
}

static std::string bytes_to_hex(const uint8_t* data, size_t len)
{
    std::string out;
    out.reserve(len * 3);
    for (size_t i = 0; i < len; ++i) {
        if (i > 0) {
            out.push_back(' ');
        }
        append_hex_byte(out, data[i]);
    }
    return out;
}

static std::string utf8_to_ucs2_hex(const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size();) {
        uint32_t cp = static_cast<unsigned char>(text[i]);
        if (cp < 0x80) {
            i += 1;
        } else if ((cp & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cp = ((cp & 0x1F) << 6) |
                 (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            i += 2;
        } else if ((cp & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cp = ((cp & 0x0F) << 12) |
                 ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            i += 3;
        } else {
            cp = '?';
            i += 1;
        }

        if (cp > 0xFFFF) {
            cp = '?';
        }
        append_hex_byte(out, static_cast<uint8_t>((cp >> 8) & 0xFF));
        append_hex_byte(out, static_cast<uint8_t>(cp & 0xFF));
    }
    return out;
}

static std::string parse_clip_number(const std::string& line)
{
    size_t first_quote = line.find('"');
    if (first_quote == std::string::npos) {
        return "";
    }
    size_t second_quote = line.find('"', first_quote + 1);
    if (second_quote == std::string::npos || second_quote <= first_quote + 1) {
        return "";
    }
    return line.substr(first_quote + 1, second_quote - first_quote - 1);
}

static void handle_line(const std::string& line)
{
    if (line.empty()) {
        return;
    }

    ESP_LOGI(TAG, "<< %s", line.c_str());

    if (line.find("RING") != std::string::npos) {
        {
            std::lock_guard<std::mutex> lock(sim900_mutex);
            incoming_call = true;
        }
        notify_event(Sim900Event::IncomingCall, sim900_get_incoming_number());
        return;
    }

    if (line.find("+CLIP:") != std::string::npos) {
        std::string number = parse_clip_number(line);
        {
            std::lock_guard<std::mutex> lock(sim900_mutex);
            incoming_number = number;
            incoming_call = true;
        }
        notify_event(Sim900Event::IncomingCall, number);
        return;
    }

    if (line.find("NO CARRIER") != std::string::npos ||
        line.find("BUSY") != std::string::npos ||
        line.find("NO ANSWER") != std::string::npos) {
        {
            std::lock_guard<std::mutex> lock(sim900_mutex);
            incoming_call = false;
        }
        notify_event(Sim900Event::CallEnded, "");
    }
}

void sim900_init()
{
    uart_config_t cfg = {
        .baud_rate = SIM900_DEFAULT_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        .source_clk = UART_SCLK_DEFAULT
#endif
    };

    ESP_ERROR_CHECK(uart_param_config(SIM900_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(SIM900_UART_NUM, SIM900_UART_TX_PIN, SIM900_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(SIM900_UART_NUM, SIM900_BUF_SIZE * 2, 0, 0, nullptr, 0));

    ESP_LOGI(TAG, "SIM900 UART initialized, TX=%d RX=%d", SIM900_UART_TX_PIN, SIM900_UART_RX_PIN);
}

void sim900_task(void* arg)
{
    uint8_t data[SIM900_BUF_SIZE];
    std::string line;
    line.reserve(160);
    TickType_t last_diag_tick = xTaskGetTickCount();

    sim900_configure_module();

    while (true) {
        int len = uart_read_bytes(SIM900_UART_NUM, data, sizeof(data), pdMS_TO_TICKS(500));
        if (len > 0) {
            ESP_LOGI(TAG, "RX HEX: %s", bytes_to_hex(data, len).c_str());
        }
        for (int i = 0; i < len; ++i) {
            char c = static_cast<char>(data[i]);
            if (c == '\n') {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                handle_line(line);
                line.clear();
            } else {
                if (line.size() < 240) {
                    line.push_back(c);
                } else {
                    line.clear();
                }
            }
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - last_diag_tick) > pdMS_TO_TICKS(30000)) {
            last_diag_tick = now;
            sim900_log_network_status();
        }
    }
}

bool sim900_answer_call()
{
    sim900_write_command("ATA");
    std::lock_guard<std::mutex> lock(sim900_mutex);
    incoming_call = false;
    return true;
}

bool sim900_hangup_call()
{
    sim900_write_command("ATH");
    std::lock_guard<std::mutex> lock(sim900_mutex);
    incoming_call = false;
    return true;
}

bool sim900_dial(const std::string& phone_number)
{
    std::string number = normalize_phone_number(phone_number);
    if (number.empty()) {
        return false;
    }

    TickType_t now = xTaskGetTickCount();
    {
        std::lock_guard<std::mutex> lock(sim900_mutex);
        if (number == last_dial_number && (now - last_dial_tick) < pdMS_TO_TICKS(8000)) {
            ESP_LOGW(TAG, "Suppress duplicate dial command: %s", number.c_str());
            return true;
        }
        last_dial_number = number;
        last_dial_tick = now;
    }

    sim900_write_command("ATD" + number + ";");
    return true;
}

bool sim900_send_sms(const std::string& phone_number, const std::string& content)
{
    std::string number = normalize_phone_number(phone_number);
    if (number.empty() || content.empty()) {
        return false;
    }

    std::string number_ucs2 = utf8_to_ucs2_hex(number);
    std::string content_ucs2 = utf8_to_ucs2_hex(content);

    {
        std::lock_guard<std::mutex> lock(sim900_mutex);
        sim900_command_busy = true;
    }

    sim900_write_command("AT+CMGF=1");
    vTaskDelay(pdMS_TO_TICKS(300));
    sim900_write_command("AT+CSCS=\"UCS2\"");
    vTaskDelay(pdMS_TO_TICKS(300));
    sim900_write_command("AT+CSMP=17,167,0,8");
    vTaskDelay(pdMS_TO_TICKS(300));
    sim900_write_command("AT+CMGS=\"" + number_ucs2 + "\"");
    vTaskDelay(pdMS_TO_TICKS(800));
    sim900_write_raw(content_ucs2.c_str(), content_ucs2.size());
    const char ctrl_z = 0x1A;
    sim900_write_raw(&ctrl_z, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    sim900_write_command("AT+CSCS=\"GSM\"");

    {
        std::lock_guard<std::mutex> lock(sim900_mutex);
        sim900_command_busy = false;
    }

    notify_event(Sim900Event::SmsSent, number);
    return true;
}

bool sim900_has_incoming_call()
{
    std::lock_guard<std::mutex> lock(sim900_mutex);
    return incoming_call;
}

std::string sim900_get_incoming_number()
{
    std::lock_guard<std::mutex> lock(sim900_mutex);
    return incoming_number;
}

void sim900_set_event_callback(std::function<void(Sim900Event, const std::string&)> callback)
{
    std::lock_guard<std::mutex> lock(sim900_mutex);
    event_callback = std::move(callback);
}
