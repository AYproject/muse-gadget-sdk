# Copyright (c) Meta Platforms, Inc. and affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Exercise production PSRAM reply handlers with host-side event sinks."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
JSON = Path(os.environ.get(
    'CJSON_SOURCE_DIR', ROOT / 'managed_components/espressif__cjson/cJSON'
))


class ChatSession(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        out = Path(cls.tmp.name)
        source = (ROOT / 'components/muse/muse_chat_session.cpp').read_text()
        constants = source[source.index('#define MIC_RATE'):source.index('/* ---- Voice task')]
        types = source[source.index('enum phase_t'):source.index('/* 10 KB')]
        handlers = source[source.index('static int find_msg('):source.index('static void on_chat_ack(')]
        reset = source[source.index('static bool turn_start('):source.index('static void turn_begin(')]
        code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "host_compat.h"
#include "cJSON.h"
#include "minimp3.h"
#include "muse_chat_priv.h"
#include "muse_tts_protocol.h"
#include "muse_reply_media.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
''' + constants + types + r'''
static turn_t s_turn;
#if CONFIG_MUSE_VOLC_TTS
static struct { char vm_id[128]; } s_vm;
#endif
static char s_reply_shown[EV_TEXT];
static int64_t s_last_seq, s_marks[4];
static int captions, console_events;
enum mark_t { M_TEXT, M_DONE };
static void mark(mark_t) {}
static int64_t now_us() { return 12345; }
static void emit(muse_hatch_ev_t type, const char *) {
    if (type == MUSE_HATCH_EV_REPLY) captions++;
}
void muse_hatch_console(const char *, const char *, const char *, ...) { console_events++; }
void muse_hatch_tail_words(const char *text, char *out, size_t cap) { strlcpy(out, text, cap); }
bool muse_hatch_caption_at(const char *text, size_t, char *out, size_t cap) {
    strlcpy(out, text, cap); return text[0];
}
static void turn_finish() {
#if CONFIG_MUSE_VOLC_TTS
    for (auto &m : s_turn.msgs) { free(m.media); m.media = nullptr; }
#endif
    s_turn.phase = P_IDLE;
}
#if CONFIG_MUSE_VOLC_TTS
static void *psram_alloc(size_t n) { return malloc(n); }
#endif
static void turn_fail(const char *) { turn_finish(); }
static bool ensure_connected() { return true; }
bool muse_hatch_configured() { return true; }
static void resampler_init(resampler_t *, int, int) {}
''' + reset + handlers + r'''
static void begin(bool typed = false) {
    assert(turn_start(s_turn.gen + 1, typed));
    s_turn.phase = P_WAIT_REPLY;
    s_turn.acked = true;
    strlcpy(s_turn.user_ids[0], "note", sizeof(s_turn.user_ids[0]));
    strlcpy(s_turn.user_ids[1], "parent", sizeof(s_turn.user_ids[1]));
    captions = console_events = 0;
}
static void event(const char *kind, const char *id, const char *parent = "", const char *text = "") {
    cJSON *root = cJSON_CreateObject(), *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "event");
    cJSON_AddStringToObject(root, "event", kind);
    cJSON_AddItemToObject(root, "payload", payload);
    cJSON_AddStringToObject(payload, "message_id", id);
    if (parent[0]) cJSON_AddStringToObject(payload, "reply_to_message_id", parent);
    cJSON_AddStringToObject(payload, "text", text);
    cJSON_AddStringToObject(payload, "display_text", text);
    on_event(root);
    cJSON_Delete(root);
}
static void rejected_deltas() {
    for (bool typed : {false, true}) {
        begin(typed);
        event("delta.message_start", "other", "elsewhere");
        event("delta.text_append", "other", "", "Wrong reply");
        event("delta.message_done", "other");
        event("message.assistant", "other", "", "Wrong final");
        assert(!s_turn.nmsgs && !captions && !console_events);
        assert(!s_turn.last_content_us && !s_turn.last_event_us);
        event("delta.message_start", "reply", "note");
        event("delta.text_append", "reply", "", "Our reply");
        event("delta.message_done", "reply");
        assert(s_turn.nmsgs == 1 && s_turn.msgs[0].done && s_turn.msgs[0].len == 9);
        assert(typed ? console_events == 2 : captions == 1);
        begin(typed);
        event("message.assistant", "other", "", "New turn");
        assert(s_turn.nmsgs == 1 && s_turn.msgs[0].done);
    }
}
static void valid_parents() {
    begin();
    event("message.assistant", "first", "note", "First");
    event("message.assistant", "second", "parent", "Second");
    event("message.assistant", "third", "first", "Third");
    event("message.assistant", "fourth", "", "Live");
    assert(s_turn.nmsgs == 4);
    for (int i = 0; i < s_turn.nmsgs; i++) assert(s_turn.msgs[i].done);
    cJSON *payload = cJSON_Parse("{\"parent_message_id\":\"elsewhere\"}");
    assert(bind_msg("fallback", payload) == -1);
    cJSON_Delete(payload);
    event("message.assistant", "fallback", "", "Wrong final");
    assert(s_turn.nmsgs == 4);
}
static void bounded_rejections() {
    begin();
    event("delta.message_start", "reply", "note");
    char id[20];
    for (int i = 0; i < 9; i++) { /* exceed the eight rejected IDs retained per turn */
        snprintf(id, sizeof(id), "other%d", i);
        event("delta.message_start", id, "elsewhere");
    }
    event("message.assistant", "other0", "", "Wrong final");
    event("message.assistant", id, "", "Overflow final");
    assert(s_turn.nmsgs == 1 && !captions && !console_events);
    event("delta.text_append", "reply", "", "Our reply");
    event("delta.message_done", "reply");
    event("message.assistant", "second", "note", "Second");
    assert(s_turn.nmsgs == 2 && s_turn.msgs[0].done && s_turn.msgs[1].done);
}
#if CONFIG_MUSE_VOLC_TTS
static void presentation() {
    s_turn.texts = static_cast<char *>(calloc(MAX_MSGS, TEXT_MAX));
    assert(s_turn.texts);
    begin();
    const char *json = R"({"type":"event","event":"delta.presentation","payload":{
      "message_id":"voice-reply","reply_to_message_id":"note","kind":"audio",
      "data":{"audio":{"mime":"audio/mpeg","missing":false,
        "path":"/mnt/data/example.mp3","variants":{"original":"/api/media/example?variant=original"}},
        "fallback_text":"Speech caption"}}})";
    cJSON *root = cJSON_Parse(json);
    assert(root);
    on_event(root); on_event(root);
    cJSON_Delete(root);
    assert(s_turn.nmsgs == 1 && s_turn.msgs[0].done);
    assert(s_turn.msgs[0].tts == TTS_QUEUED);
    assert(!strcmp(s_turn.msgs[0].media, "/api/media/example?variant=original"));
    assert(!s_turn.msgs[0].media_https);
    assert(!strcmp(s_turn.texts, "Speech caption"));
    /* A presentation referring to another user's message must stay rejected. */
    root = cJSON_Parse(json);
    cJSON *payload = cJSON_GetObjectItem(root, "payload");
    cJSON_SetValuestring(cJSON_GetObjectItem(payload, "message_id"), "other-reply");
    cJSON_SetValuestring(cJSON_GetObjectItem(payload, "reply_to_message_id"), "elsewhere");
    on_event(root);
    assert(s_turn.nmsgs == 1);
    cJSON_Delete(root);
    turn_finish();
    root = cJSON_Parse(json); on_event(root); cJSON_Delete(root);
    assert(!s_turn.msgs[0].media); /* no retained URL after turn/cancel */
    free(s_turn.texts); s_turn.texts = nullptr;
}
#endif
int main(int argc, char **argv) {
    assert(argc == 2);
    switch (atoi(argv[1])) {
    case 0: rejected_deltas(); break;
    case 1: valid_parents(); break;
    case 2: bounded_rejections(); break;
#if CONFIG_MUSE_VOLC_TTS
    case 3: presentation(); break;
#endif
    default: return 2;
    }
}
'''
        (out / 'session.cpp').write_text(code)
        flags = ['-Wall', '-Wextra', '-Werror', '-I', str(JSON),
                 '-I', str(ROOT / 'tests'), '-I', str(ROOT / 'components/muse'),
                 '-I', str(ROOT / 'components/minimp3/include')]
        commands = [
            [*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', *flags,
             '-c', str(JSON / 'cJSON.c'), '-o', str(out / 'cjson.o')],
            [*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', *flags,
             '-c', str(ROOT / 'components/muse/muse_tts_protocol.c'), '-o', str(out / 'tts.o')],
            [*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', *flags,
             '-c', str(ROOT / 'components/muse/muse_reply_media.c'), '-o', str(out / 'media.o')],
        ]
        for enabled in (0, 1):
            commands.append([*shlex.split(os.environ.get('CXX', 'c++')), '-std=gnu++17', *flags,
                f'-DCONFIG_MUSE_VOLC_TTS={enabled}',
                str(out / 'session.cpp'), str(out / 'cjson.o'), str(out / 'tts.o'), str(out / 'media.o'),
                '-o', str(out / f'session{enabled}')])
        for command in commands:
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
        cls.binaries = [out / f'session{enabled}' for enabled in (0, 1)]

    def run_case(self, case):
        for binary in self.binaries if case < 3 else self.binaries[1:]:
            result = subprocess.run([str(binary), str(case)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_native_audio_presentation(self):
        self.run_case(3)

    def test_rejected_deltas_do_not_emit_or_complete_voice_or_typed_replies(self):
        self.run_case(0)

    def test_ack_parent_reply_chain_and_parentless_messages_still_work(self):
        self.run_case(1)

    def test_rejection_capacity_preserves_correlation(self):
        self.run_case(2)
