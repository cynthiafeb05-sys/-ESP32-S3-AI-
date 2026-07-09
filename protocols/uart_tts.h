#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void uart_tts_init(void);
void uart_tts_task(void* arg);
void uart_tts_start_console_fallback(void);

#ifdef __cplusplus
}
#endif
