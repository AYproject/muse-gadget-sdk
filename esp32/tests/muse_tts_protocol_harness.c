/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "muse_tts_protocol.h"
#include "cJSON.h"

static unsigned char received[32];
static size_t length;
static bool receive_audio(const uint8_t *data, size_t n, void *ctx)
{
    assert(ctx == received);
    assert(n <= sizeof(received));
    memcpy(received, data, n);
    length = n;
    return true;
}
static bool reject_audio(const uint8_t *data, size_t n, void *ctx)
{
    (void)data; (void)n; (void)ctx;
    return false;
}

int main(void)
{
    int code;
    assert(muse_tts_parse_line("{\"code\":0,\"data\":\"SUQzBA==\"}", receive_audio, received, &code) == MUSE_TTS_FRAME_AUDIO);
    assert(length == 4 && !memcmp(received, "ID3\4", 4));
    assert(muse_tts_parse_line("data: {\"code\":0,\"data\":\"AP8Q\"}\r", receive_audio, received, &code) == MUSE_TTS_FRAME_AUDIO);
    assert(length == 3 && received[0] == 0 && received[1] == 255 && received[2] == 16);
    assert(muse_tts_parse_line("{\"code\":20000000,\"data\":null}", receive_audio, received, &code) == MUSE_TTS_FRAME_END);
    assert(muse_tts_parse_line("event: 352", receive_audio, received, &code) == MUSE_TTS_FRAME_IGNORE);
    assert(muse_tts_parse_line(": heartbeat", receive_audio, received, &code) == MUSE_TTS_FRAME_IGNORE);
    assert(muse_tts_parse_line("", receive_audio, received, &code) == MUSE_TTS_FRAME_IGNORE);
    assert(muse_tts_parse_line("{\"code\":45000000,\"message\":\"private body\"}", receive_audio, received, &code) == MUSE_TTS_FRAME_ERROR && code == 45000000);
    const char *bad[] = {
        "<html>bad</html>", "{\"data\":\"SUQz\"}", "{\"code\":0,\"data\":33}",
        "{\"code\":0,\"data\":\"A===\"}", "{\"code\":0,\"data\":\"AB==\"}",
        "{\"code\":0,\"data\":\"AA=A\"}", "{\"code\":0,\"data\":\"AA==AA==\"}",
        "{\"code\":0,\"data\":\"%%%#\"}", "{\"code\":0,\"data\":\"QQ\"}",
        "{\"code\":0,\"data\":\"SUQz\"} extra"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        assert(muse_tts_parse_line(bad[i], receive_audio, received, &code) == MUSE_TTS_FRAME_ERROR);
    assert(muse_tts_parse_line("{\"code\":0,\"data\":\"SUQz\"}", reject_audio, received, &code) == MUSE_TTS_FRAME_ERROR);
    assert(muse_tts_valid_key("synthetic-api-key") && !muse_tts_valid_key("bad\r\nInjected: yes"));
    assert(!muse_tts_valid_key("") && !muse_tts_valid_key("contains spaces"));
    assert(muse_tts_valid_speaker(MUSE_TTS_DEFAULT_SPEAKER));
    assert(!muse_tts_valid_speaker("bad\nvoice") && !muse_tts_valid_speaker("https://invalid"));
    const char *unicode = "hello\xe4\xbd\xa0\xe5\xa5\xbd";
    assert(muse_tts_utf8_prefix(unicode, 6) == 5);
    assert(muse_tts_utf8_prefix(unicode, 8) == 8);
    assert(muse_tts_utf8_prefix(unicode, 9) == 8);
    assert(muse_tts_utf8_prefix(unicode, 11) == 11);
    char text[MUSE_TTS_TEXT_MAX + 8];
    memset(text, 'x', MUSE_TTS_TEXT_MAX - 2);
    strcpy(text + MUSE_TTS_TEXT_MAX - 2, "\xe4\xbd\xa0");
    char *body = muse_tts_request_json(text, MUSE_TTS_DEFAULT_SPEAKER);
    assert(body);
    cJSON *root = cJSON_Parse(body);
    assert(root);
    cJSON *params = cJSON_GetObjectItem(root, "req_params");
    assert(strlen(cJSON_GetStringValue(cJSON_GetObjectItem(params, "text"))) == MUSE_TTS_TEXT_MAX - 2);
    cJSON *audio = cJSON_GetObjectItem(params, "audio_params");
    assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(audio, "format")), "mp3"));
    assert(cJSON_GetObjectItem(audio, "sample_rate")->valueint == 24000);
    cJSON_Delete(root);
    cJSON_free(body);
    assert(!muse_tts_request_json("", MUSE_TTS_DEFAULT_SPEAKER));
    puts("TTS JSON, binary audio, errors, cancellation callback, credentials and UTF-8 bounds passed");
    return 0;
}
