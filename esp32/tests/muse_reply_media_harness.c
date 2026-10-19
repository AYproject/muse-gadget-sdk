/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <string.h>
#include "muse_reply_media.h"
#include "muse_chat_priv.h"
int main(void)
{
    bool https = false;
    assert(muse_reply_media_source("https://media.example/reply.mp3?signature=test", &https) && https);
    assert(muse_reply_media_source("/api/media/example?variant=original", &https) && !https);
    const char *bad[] = {"", "http://example/", "//example/", "/api/a\r\nAuthorization:test", "https://user:key@example/", "https:///", "/a\\b", "/a#b"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) assert(!muse_reply_media_source(bad[i], &https));
    char large[MUSE_REPLY_MEDIA_MAX + 1]; memset(large, 'a', sizeof(large)); large[0] = '/'; large[sizeof(large)-1] = 0;
    assert(!muse_reply_media_source(large, &https));
    const char *vm = "aabbccdd-0011-2233-4455-66778899aabb";
    assert(!strcmp(muse_reply_vm_path("https://aabbccdd-0011-2233-4455-66778899aabb.metaaivm.com/media/audio.mp3?q=1", vm), "/media/audio.mp3?q=1"));
    assert(!strcmp(muse_reply_vm_path("https://aabbccdd-0011-2233-4455-66778899aabb.metaaivm.com:443/media/audio.mp3", vm), "/media/audio.mp3"));
    assert(!muse_reply_vm_path("https://aabbccdd-0011-2233-4455-66778899aabb.metaaivm.com.evil/media/audio.mp3", vm));
    assert(!muse_reply_vm_path("https://aabbccdd-0011-2233-4455-66778899aabc.metaaivm.com/media/audio.mp3", vm));
    assert(!muse_reply_vm_path("https://example/media/audio.mp3", vm));
    cJSON *payload = cJSON_Parse("{\"kind\":\"audio\",\"data\":{\"audio\":{\"mime\":\"audio/mpeg\",\"missing\":false,\"variants\":{\"original\":\"/api/media/example\"}},\"fallback_text\":\"Voice caption\"}}");
    assert(payload);
    muse_reply_audio_t out;
    assert(muse_reply_audio(payload, &out));
    assert(!strcmp(out.source, "/api/media/example") && !strcmp(out.caption, "Voice caption"));
    cJSON *audio = cJSON_GetObjectItem(cJSON_GetObjectItem(payload, "data"), "audio");
    cJSON_ReplaceItemInObject(audio, "missing", cJSON_CreateTrue());
    assert(!muse_reply_audio(payload, &out));
    cJSON_ReplaceItemInObject(audio, "missing", cJSON_CreateFalse());
    cJSON_SetValuestring(cJSON_GetObjectItem(audio, "mime"), "audio/ogg");
    assert(!muse_reply_audio(payload, &out));
    cJSON_Delete(payload);
    assert(!muse_reply_audio(NULL, &out));
    payload = cJSON_Parse(MUSE_HATCH_TTS_NOTE_HEAD "UklGRg==" MUSE_HATCH_NOTE_TAIL);
    assert(payload);
    assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(payload, "output_modality")), "text"));
    assert(strstr(cJSON_GetStringValue(cJSON_GetObjectItem(payload, "message")), "reads your reply aloud"));
    assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(payload, "items"), 0), "data_base64")), "UklGRg=="));
    cJSON_Delete(payload);
    return 0;
}
