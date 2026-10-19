/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MUSE_TTS_KEY_MAX 256
#define MUSE_TTS_SPEAKER_MAX 95
#define MUSE_TTS_TEXT_MAX 2560
#define MUSE_TTS_PLAN_URL "https://openspeech.bytedance.com/api/v3/plan/tts/unidirectional"
#define MUSE_TTS_DEFAULT_SPEAKER "zh_female_vv_uranus_bigtts"

typedef bool (*muse_tts_audio_cb_t)(const uint8_t *, size_t, void *);
typedef enum {
    MUSE_TTS_FRAME_IGNORE,
    MUSE_TTS_FRAME_AUDIO,
    MUSE_TTS_FRAME_END,
    MUSE_TTS_FRAME_ERROR
} muse_tts_frame_t;

/* Accepts both NDJSON and SSE data lines. Never logs a service response. */
muse_tts_frame_t muse_tts_parse_line(const char *line, muse_tts_audio_cb_t audio,
                                      void *ctx, int *service_code);
/* Largest prefix ending on a complete UTF-8 character, bounded by max_bytes. */
size_t muse_tts_utf8_prefix(const char *text, size_t max_bytes);
bool muse_tts_valid_key(const char *key);
bool muse_tts_valid_speaker(const char *speaker);
char *muse_tts_request_json(const char *text, const char *speaker);

#ifdef __cplusplus
}
#endif
