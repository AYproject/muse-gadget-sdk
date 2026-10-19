/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_err.h"
#include "muse_tts_protocol.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MUSE_TTS_WAITING, MUSE_TTS_DONE, MUSE_TTS_FAILED } muse_tts_state_t;
esp_err_t muse_tts_init(void);
/* Provisioned over USB; never exposes the key in status or logs. */
esp_err_t muse_tts_set_key(const char *key);
esp_err_t muse_tts_set_speaker(const char *speaker);
bool muse_tts_configured(void);
bool muse_tts_reply_text(void);
esp_err_t muse_tts_set_reply_text(bool enabled);
/* Session task is the sole caller of begin/read/cancel. A worker owns HTTPS. */
uint32_t muse_tts_begin(const char *text);
/* Native Muse HTTPS MP3 attachment; never adds the Agent Plan API key. */
uint32_t muse_reply_fetch_begin(const char *url);
size_t muse_tts_read(uint32_t ticket, uint8_t *out, size_t cap);
muse_tts_state_t muse_tts_state(uint32_t ticket);
void muse_tts_cancel(uint32_t ticket);

#ifdef __cplusplus
}
#endif
