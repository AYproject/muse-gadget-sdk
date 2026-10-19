/* SPDX-License-Identifier: Apache-2.0 */
#include "muse_reply_media.h"
#include <string.h>

bool muse_reply_media_source(const char *source, bool *https)
{
    if (!source || !source[0] || strlen(source) >= MUSE_REPLY_MEDIA_MAX) return false;
    for (const unsigned char *p = (const unsigned char *)source; *p; ++p)
        if (*p <= 32 || *p == 127 || *p == '\\' || *p == '#') return false;
    bool absolute = !strncmp(source, "https://", 8);
    if (absolute) {
        const char *host = source + 8;
        const char *end = strpbrk(host, "/?");
        if (!end) end = host + strlen(host);
        if (end == host || memchr(host, '@', (size_t)(end - host))) return false;
    } else if (source[0] != '/' || source[1] == '/') {
        return false;
    }
    if (https) *https = absolute;
    return true;
}

bool muse_reply_audio(const cJSON *payload, muse_reply_audio_t *result)
{
    if (!result) return false;
    memset(result, 0, sizeof(*result));
    const char *kind = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(payload, "kind"));
    if (!kind || strcmp(kind, "audio")) return false;
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(payload, "data");
    const cJSON *audio = cJSON_GetObjectItemCaseSensitive(data, "audio");
    if (!cJSON_IsObject(audio) || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(audio, "missing"))) return false;
    const char *mime = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(audio, "mime"));
    if (!mime || (strcmp(mime, "audio/mpeg") && strcmp(mime, "audio/mp3"))) return false;
    const cJSON *variants = cJSON_GetObjectItemCaseSensitive(audio, "variants");
    const char *source = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(variants, "original"));
    if (!muse_reply_media_source(source, &result->https)) return false;
    result->source = source;
    result->caption = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(payload, "display_text"));
    if (!result->caption || !result->caption[0])
        result->caption = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data, "fallback_text"));
    if (!result->caption || !result->caption[0])
        result->caption = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(payload, "fallback_text"));
    /* A sandbox Markdown link is a file reference, not a speech transcript. */
    if (result->caption && (strstr(result->caption, "](sandbox:") || strstr(result->caption, "](https://")))
        result->caption = NULL;
    return true;
}

const char *muse_reply_vm_path(const char *source, const char *vm_id)
{
    bool https;
    if (!vm_id || strlen(vm_id) != 36 || !muse_reply_media_source(source, &https) || !https) return NULL;
    for (const char *p = vm_id; *p; ++p)
        if (!strchr("0123456789abcdefABCDEF-", *p)) return NULL;
    const char *host = source + 8;
    if (strncmp(host, vm_id, 36)) return NULL;
    const char *suffix = ".metaaivm.com";
    if (strncmp(host + 36, suffix, strlen(suffix))) return NULL;
    const char *path = host + 36 + strlen(suffix);
    if (!strncmp(path, ":443/", 5)) path += 4;
    return muse_reply_media_source(path, &https) && !https ? path : NULL;
}
