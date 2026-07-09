#include "uart_tts.h"

#include "application.h"

#include <driver/uart.h>
#include <esp_err.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <string>

// Dedicated UART link for STM32.
#define UART_TTS_UART_NUM UART_NUM_1
#define UART_TTS_TX_PIN 48
#define UART_TTS_RX_PIN 47
#define UART_TTS_BAUD_RATE 115200
#define UART_TTS_BUF_SIZE 1024
#define UART_TTS_MAX_LINE 256

static const char* TAG = "UART_TTS";

static bool hex_to_nibble(char c, uint8_t& out)
{
    if (c >= '0' && c <= '9') {
        out = static_cast<uint8_t>(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        out = static_cast<uint8_t>(10 + c - 'a');
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        out = static_cast<uint8_t>(10 + c - 'A');
        return true;
    }
    return false;
}

static void append_utf8_from_codepoint(uint32_t cp, std::string& out)
{
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
        return;
    }
    if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        return;
    }
    if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        return;
    }
    if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Decode ASCII escapes like "\u4F60\u597D" into UTF-8.
static std::string decode_unicode_escapes(const std::string& in)
{
    std::string out;
    out.reserve(in.size());

    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '\\' || i + 5 >= in.size() || (in[i + 1] != 'u' && in[i + 1] != 'U')) {
            out.push_back(in[i]);
            continue;
        }

        uint8_t n0 = 0;
        uint8_t n1 = 0;
        uint8_t n2 = 0;
        uint8_t n3 = 0;
        if (!hex_to_nibble(in[i + 2], n0) || !hex_to_nibble(in[i + 3], n1) ||
            !hex_to_nibble(in[i + 4], n2) || !hex_to_nibble(in[i + 5], n3)) {
            out.push_back(in[i]);
            continue;
        }

        uint32_t cp = static_cast<uint32_t>((n0 << 12) | (n1 << 8) | (n2 << 4) | n3);
        append_utf8_from_codepoint(cp, out);
        i += 5;
    }

    return out;
}

static void uart_tts_send_ack(const std::string& text)
{
    std::string ack = "ACK:" + text + "\r\n";
    uart_write_bytes(UART_TTS_UART_NUM, ack.c_str(), ack.size());
}

static void handle_text_line(const std::string& line)
{
    std::string text = line;
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' ||
                             text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.erase(0, 1);
    }

    if (text.empty()) {
        return;
    }

    std::string normalized = decode_unicode_escapes(text);
    uart_tts_send_ack(normalized);
    ESP_LOGI(TAG, "Speak text: %s", normalized.c_str());
    Application::GetInstance().SpeakText(normalized);
}

void uart_tts_init(void)
{
    uart_config_t cfg = {
        .baud_rate = UART_TTS_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        .source_clk = UART_SCLK_DEFAULT
#endif
    };

    esp_err_t err = uart_driver_install(UART_TTS_UART_NUM, UART_TTS_BUF_SIZE * 2, 0, 0, nullptr, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_ERROR_CHECK(uart_param_config(UART_TTS_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_TTS_UART_NUM, UART_TTS_TX_PIN, UART_TTS_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART TTS ready, UART=%d TX=%d RX=%d baud=%d",
             UART_TTS_UART_NUM, UART_TTS_TX_PIN, UART_TTS_RX_PIN, UART_TTS_BAUD_RATE);
    ESP_LOGI(TAG, "STM32 send one line (\\n) to IO47 to speak");
    uart_write_bytes(UART_TTS_UART_NUM, "DNESP32S3 UART READY\r\n", 21);
}

void uart_tts_task(void* arg)
{
    uint8_t data[UART_TTS_BUF_SIZE];
    std::string line;
    line.reserve(128);
    TickType_t last_rx_tick = 0;
    bool has_data = false;

    while (true) {
        int len = uart_read_bytes(UART_TTS_UART_NUM, data, sizeof(data), pdMS_TO_TICKS(50));
        if (len > 0) {
            last_rx_tick = xTaskGetTickCount();
            has_data = true;
        }

        if (len > 0) {
            for (int i = 0; i < len; ++i) {
                char c = static_cast<char>(data[i]);
                if (c == '\n') {
                    handle_text_line(line);
                    line.clear();
                    has_data = false;
                    continue;
                }

                if (c == '\r') {
                    continue;
                }

                if (line.size() < UART_TTS_MAX_LINE) {
                    line.push_back(c);
                } else {
                    line.clear();
                    has_data = false;
                    ESP_LOGW(TAG, "Input line too long, discarded");
                }
            }
        }

        if (has_data && !line.empty()) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_rx_tick) > pdMS_TO_TICKS(400)) {
                handle_text_line(line);
                line.clear();
                has_data = false;
            }
        }
    }
}

void uart_tts_start_console_fallback(void)
{
    // No-op in dedicated UART1 mode.
}
