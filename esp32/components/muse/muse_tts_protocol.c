/* SPDX-License-Identifier: Apache-2.0 */
#include "muse_tts_protocol.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

size_t muse_tts_utf8_prefix(const char *text, size_t max_bytes)
{
    size_t length = strlen(text);
    if (length <= max_bytes) return length;
    size_t n = max_bytes;
    while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
    return n;
}

bool muse_tts_valid_key(const char *key)
{
    size_t n = key ? strlen(key) : 0;
    if (!n || n > MUSE_TTS_KEY_MAX) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = key[i];
        if (c <= 0x20 || c >= 0x7f) return false;
    }
    return true;
}

bool muse_tts_valid_speaker(const char *speaker)
{
    size_t n = speaker ? strlen(speaker) : 0;
    if (!n || n > MUSE_TTS_SPEAKER_MAX) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = speaker[i];
        if (!(isalnum(c) || c == '_' || c == '-')) return false;
    }
    return true;
}

char *muse_tts_request_json(const char *text, const char *speaker)
{
    if (!text || !text[0] || !muse_tts_valid_speaker(speaker)) return NULL;
    size_t n = muse_tts_utf8_prefix(text, MUSE_TTS_TEXT_MAX - 1);
    char *bounded = malloc(n + 1);
    if (!bounded) return NULL;
    memcpy(bounded, text, n);
    bounded[n] = '\0';
    cJSON *root = cJSON_CreateObject();
    cJSON *params = root ? cJSON_AddObjectToObject(root, "req_params") : NULL;
    cJSON *audio = params ? cJSON_AddObjectToObject(params, "audio_params") : NULL;
    bool ok = audio && cJSON_AddStringToObject(params, "text", bounded) &&
        cJSON_AddStringToObject(params, "speaker", speaker) &&
        cJSON_AddStringToObject(audio, "format", "mp3") &&
        cJSON_AddNumberToObject(audio, "sample_rate", 24000);
    char *body = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    free(bounded);
    return body;
}

static int base64_value(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '+' ? 62 : (c == '/' ? 63 : -1);
}

/* Decode in cJSON's owned string, which is longer than its decoded audio. */
static bool decode64(char *s, size_t *out)
{
    size_t n = strlen(s), used = 0;
    if (!n || n % 4) return false;
    for (size_t i = 0; i < n; i += 4) {
        int a = base64_value(s[i]), b = base64_value(s[i + 1]);
        bool pad2 = s[i + 2] == '=', pad3 = s[i + 3] == '=';
        int c = pad2 ? 0 : base64_value(s[i + 2]);
        int d = pad3 ? 0 : base64_value(s[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0 || (pad2 && !pad3) ||
            ((pad2 || pad3) && i + 4 != n) || (pad2 && (b & 15)) ||
            (pad3 && !pad2 && (c & 3))) return false;
        s[used++] = (char)((a << 2) | (b >> 4));
        if (!pad2) s[used++] = (char)((b << 4) | (c >> 2));
        if (!pad3) s[used++] = (char)((c << 6) | d);
    }
    *out = used;
    return true;
}

muse_tts_frame_t muse_tts_parse_line(const char *line, muse_tts_audio_cb_t audio,
                                      void *ctx, int *service_code)
{
    *service_code = 0;
    while (*line && isspace((unsigned char)*line)) ++line;
    if (!*line || *line == ':' || !strncmp(line, "event:", 6)) return MUSE_TTS_FRAME_IGNORE;
    if (!strncmp(line, "data:", 5)) line += 5;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithOpts(line, &end, true);
    if (!root) return MUSE_TTS_FRAME_ERROR;
    cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    muse_tts_frame_t result = MUSE_TTS_FRAME_ERROR;
    if (!cJSON_IsNumber(code) || (code->valuedouble != 0 && code->valuedouble != 20000000)) {
        if (cJSON_IsNumber(code)) *service_code = code->valueint;
    } else {
        cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
        if (cJSON_IsString(data) && data->valuestring[0]) {
            size_t n = 0;
            if (decode64(data->valuestring, &n) && audio((uint8_t *)data->valuestring, n, ctx))
                result = code->valuedouble == 20000000 ? MUSE_TTS_FRAME_END : MUSE_TTS_FRAME_AUDIO;
        } else if (!data || cJSON_IsNull(data) || (cJSON_IsString(data) && !data->valuestring[0])) {
            result = code->valuedouble == 20000000 ? MUSE_TTS_FRAME_END : MUSE_TTS_FRAME_IGNORE;
        }
    }
    cJSON_Delete(root);
    return result;
}
