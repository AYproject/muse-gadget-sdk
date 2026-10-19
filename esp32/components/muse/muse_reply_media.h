/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include "cJSON.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MUSE_REPLY_MEDIA_MAX 2048
/* Borrowed fields from the observed delta.presentation audio envelope. */
typedef struct {
    const char *source;
    const char *caption;
    bool https;
} muse_reply_audio_t;
bool muse_reply_media_source(const char *source, bool *https);
/* Only the current VM's exact advertised origin may use its Noise session. */
const char *muse_reply_vm_path(const char *source, const char *vm_id);
bool muse_reply_audio(const cJSON *payload, muse_reply_audio_t *audio);
#ifdef __cplusplus
}
#endif
