/* SPDX-License-Identifier: Apache-2.0 */
/* Agent Plan TTS: official /plan/ endpoint, HTTPS, MP3 NDJSON/SSE frames.
 * https://docs.volcengine.com/docs/ark/agent-plan-personal-voice-model?lang=zh
 * The session owns decoding; this worker only fetches bytes. */
#include "muse_tts.h"
#include "muse_reply_media.h"
#include <stdlib.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "cJSON.h"

#define RX_BYTES (64 * 1024)
#define TTS_LINE_BYTES (256 * 1024)
#define FETCH_MAX_US (150 * 1000000LL)
static const char *TAG = "muse_tts";
static SemaphoreHandle_t s_lock;
static nvs_handle_t s_nvs;
static char s_key[MUSE_TTS_KEY_MAX + 1];
static char s_speaker[MUSE_TTS_SPEAKER_MAX + 1] = MUSE_TTS_DEFAULT_SPEAKER;
static QueueHandle_t s_jobs;
static StreamBufferHandle_t s_rx;
static StaticStreamBuffer_t s_rx_control;
static uint8_t *s_rx_storage;
static uint32_t s_generation;
static bool s_busy;
static bool s_reply_text = true;
static int s_socket = -1;
static muse_tts_state_t s_state = MUSE_TTS_FAILED;

typedef struct {
    uint32_t ticket;
    int64_t started;
    size_t audio_bytes;
    char key[MUSE_TTS_KEY_MAX + 1];
    char speaker[MUSE_TTS_SPEAKER_MAX + 1];
    char text[MUSE_TTS_TEXT_MAX];
    char media[MUSE_REPLY_MEDIA_MAX];
} job_t;

#define LOCK() xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

esp_err_t muse_tts_init(void)
{
    if (s_lock) return ESP_OK;
    SemaphoreHandle_t lock = xSemaphoreCreateMutex();
    if (!lock) return ESP_ERR_NO_MEM;
    esp_err_t err = nvs_open("muse_tts", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) { vSemaphoreDelete(lock); return err; }
    size_t n = sizeof(s_key);
    if (nvs_get_str(s_nvs, "key", s_key, &n) != ESP_OK || !muse_tts_valid_key(s_key)) s_key[0] = '\0';
    n = sizeof(s_speaker);
    if (nvs_get_str(s_nvs, "speaker", s_speaker, &n) != ESP_OK || !muse_tts_valid_speaker(s_speaker))
        strcpy(s_speaker, MUSE_TTS_DEFAULT_SPEAKER);
    uint8_t reply_text;
    if (nvs_get_u8(s_nvs, "reply_text", &reply_text) == ESP_OK && reply_text <= 1) s_reply_text = reply_text != 0;
    s_lock = lock;
    ESP_LOGI(TAG, "Agent Plan HTTP speech %s", s_key[0] ? "configured" : "needs local setup");
    return ESP_OK;
}

esp_err_t muse_tts_set_key(const char *key)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (!key || (key[0] && !muse_tts_valid_key(key))) return ESP_ERR_INVALID_ARG;
    LOCK();
    esp_err_t err = nvs_set_str(s_nvs, "key", key);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err == ESP_OK) strcpy(s_key, key);
    UNLOCK();
    return err;
}

esp_err_t muse_tts_set_speaker(const char *speaker)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (!muse_tts_valid_speaker(speaker)) return ESP_ERR_INVALID_ARG;
    LOCK();
    esp_err_t err = nvs_set_str(s_nvs, "speaker", speaker);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err == ESP_OK) strcpy(s_speaker, speaker);
    UNLOCK();
    return err;
}

bool muse_tts_configured(void)
{
    if (!s_lock) return false;
    LOCK();
    bool configured = s_key[0] != '\0';
    UNLOCK();
    return configured;
}

bool muse_tts_reply_text(void)
{
    if (!s_lock) return false;
    LOCK(); bool enabled = s_reply_text; UNLOCK();
    return enabled;
}

esp_err_t muse_tts_set_reply_text(bool enabled)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    LOCK();
    esp_err_t err = nvs_set_u8(s_nvs, "reply_text", enabled);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err == ESP_OK) s_reply_text = enabled;
    UNLOCK();
    return err;
}

static bool current(uint32_t ticket)
{
    LOCK();
    bool active = ticket == s_generation;
    UNLOCK();
    return active;
}

void muse_tts_cancel(uint32_t ticket)
{
    if (!s_lock || !ticket) return;
    LOCK();
    if (ticket == s_generation) {
        ++s_generation;
        /* The worker clears this under the same lock before close/cleanup.
         * shutdown interrupts a blocked read without closing a reused fd. */
        if (s_socket >= 0) shutdown(s_socket, SHUT_RDWR);
        s_state = MUSE_TTS_FAILED;
    }
    UNLOCK();
}

static bool enqueue_audio(const uint8_t *data, size_t len, void *ctx)
{
    job_t *job = ctx;
    if (!job->audio_bytes)
        ESP_LOGI(TAG, "first speech bytes after %.2fs", (esp_timer_get_time() - job->started) / 1e6);
    while (len) {
        if (!current(job->ticket) || esp_timer_get_time() - job->started > FETCH_MAX_US) return false;
        size_t n = xStreamBufferSend(s_rx, data, len, pdMS_TO_TICKS(50));
        data += n;
        len -= n;
        job->audio_bytes += n;
    }
    return true;
}

static bool parse_frame(char *line, job_t *job, bool *finished)
{
    int code;
    muse_tts_frame_t frame = muse_tts_parse_line(line, enqueue_audio, job, &code);
    if (frame == MUSE_TTS_FRAME_ERROR) {
        /* Codes identify quota/auth/voice issues without revealing response text. */
        ESP_LOGW(TAG, "speech frame rejected (service code %d)", code);
        return false;
    }
    if (frame == MUSE_TTS_FRAME_END) *finished = true;
    return true;
}

static bool fetch(job_t *job)
{
    char *body = muse_tts_request_json(job->text, job->speaker);
    char *line = heap_caps_malloc(TTS_LINE_BYTES + 1, MALLOC_CAP_SPIRAM);
    uint8_t *buf = heap_caps_malloc(2048, MALLOC_CAP_SPIRAM);
    if (!body || !line || !buf) { cJSON_free(body); free(line); free(buf); return false; }
    esp_http_client_config_t cfg = {
        .url = MUSE_TTS_PLAN_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .disable_auto_redirect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    bool ok = false, finished = false;
    size_t used = 0;
    if (!client) goto done;
    if (esp_http_client_set_header(client, "X-Api-Key", job->key) != ESP_OK ||
        esp_http_client_set_header(client, "X-Api-Resource-Id", "seed-tts-2.0") != ESP_OK ||
        esp_http_client_set_header(client, "Content-Type", "application/json") != ESP_OK) goto done;
    if (!current(job->ticket) || esp_http_client_open(client, strlen(body)) != ESP_OK) goto done;
    LOCK();
    s_socket = esp_http_client_get_socket(client);
    bool active = job->ticket == s_generation;
    if (!active && s_socket >= 0) shutdown(s_socket, SHUT_RDWR);
    UNLOCK();
    if (!active) goto done;
    size_t written = 0, body_len = strlen(body);
    while (written < body_len && current(job->ticket)) {
        int n = esp_http_client_write(client, body + written, body_len - written);
        if (n <= 0) goto done;
        written += n;
    }
    if (written != body_len || esp_http_client_fetch_headers(client) < 0) goto done;
    int status = esp_http_client_get_status_code(client);
    if (status != 200) { ESP_LOGW(TAG, "speech HTTP %d", status); goto done; }
    esp_http_client_set_timeout_ms(client, 1000);
    while (current(job->ticket) && esp_timer_get_time() - job->started < FETCH_MAX_US) {
        int n = esp_http_client_read(client, (char *)buf, 2048);
        if (n == -ESP_ERR_HTTP_EAGAIN) continue;
        if (n < 0) goto done;
        if (!n) {
            if (used) {
                line[used] = '\0';
                if (!parse_frame(line, job, &finished)) goto done;
            }
            ok = finished && job->audio_bytes && esp_http_client_is_complete_data_received(client);
            break;
        }
        for (int i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                line[used] = '\0';
                if (!parse_frame(line, job, &finished)) goto done;
                used = 0;
            } else {
                if (!buf[i] || used == TTS_LINE_BYTES) goto done;
                line[used++] = buf[i];
            }
        }
        if (finished) { ok = job->audio_bytes > 0; break; }
    }
done:
    LOCK();
    s_socket = -1;   /* cancellation may no longer touch this socket */
    UNLOCK();
    if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); }
    cJSON_free(body);
    free(line);
    free(buf);
    return ok;
}

/* The server's original HTTPS variant is fetched without credentials. A
 * relative variant stays on the existing authenticated Noise connection. */
static bool fetch_media(job_t *job)
{
    esp_http_client_config_t cfg = {
        .url = job->media,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .disable_auto_redirect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    uint8_t *buf = heap_caps_malloc(2048, MALLOC_CAP_SPIRAM);
    bool ok = false;
    if (!client || !buf) goto done;
    esp_http_client_set_header(client, "Accept", "audio/mpeg");
    if (!current(job->ticket) || esp_http_client_open(client, 0) != ESP_OK) goto done;
    LOCK();
    s_socket = esp_http_client_get_socket(client);
    bool active = job->ticket == s_generation;
    if (!active && s_socket >= 0) shutdown(s_socket, SHUT_RDWR);
    UNLOCK();
    if (!active || esp_http_client_fetch_headers(client) < 0) goto done;
    int status = esp_http_client_get_status_code(client);
    if (status != 200) { ESP_LOGW(TAG, "native audio HTTP %d", status); goto done; }
    esp_http_client_set_timeout_ms(client, 1000);
    while (current(job->ticket) && esp_timer_get_time() - job->started < FETCH_MAX_US) {
        int n = esp_http_client_read(client, (char *)buf, 2048);
        if (n == -ESP_ERR_HTTP_EAGAIN) continue;
        if (n < 0) goto done;
        if (!n) { ok = job->audio_bytes && esp_http_client_is_complete_data_received(client); break; }
        if (job->audio_bytes + n > 8 * 1024 * 1024 || !enqueue_audio(buf, n, job)) goto done;
    }
done:
    LOCK();
    s_socket = -1;
    UNLOCK();
    if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); }
    free(buf);
    return ok;
}

static void worker(void *arg)
{
    (void)arg;
    job_t *job;
    for (;;) {
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE) continue;
        bool ok = job->media[0] ? fetch_media(job) : fetch(job);
        ESP_LOGI(TAG, "%s fetch %s: %u bytes", job->media[0] ? "native audio" : "speech",
                 ok ? "complete" : "stopped", (unsigned)job->audio_bytes);
        LOCK();
        if (job->ticket == s_generation) s_state = ok ? MUSE_TTS_DONE : MUSE_TTS_FAILED;
        memset(job->key, 0, sizeof(job->key));
        free(job);
        s_busy = false;
        UNLOCK();
    }
}

static bool start_worker(void)
{
    if (s_jobs) return true;
    s_rx_storage = heap_caps_malloc(RX_BYTES + 1, MALLOC_CAP_SPIRAM);
    if (!s_rx_storage) return false;
    /* IDF 6.0.1's dynamic-with-caps stream-buffer deletion can free twice. */
    s_rx = xStreamBufferCreateStatic(RX_BYTES + 1, 1, s_rx_storage, &s_rx_control);
    s_jobs = xQueueCreate(1, sizeof(job_t *));
    if (s_rx && s_jobs && xTaskCreatePinnedToCoreWithCaps(worker, "muse_tts", 8192,
                                                       NULL, 4, NULL, 1, MALLOC_CAP_SPIRAM) == pdPASS) return true;
    if (s_jobs) vQueueDelete(s_jobs);
    if (s_rx) vStreamBufferDelete(s_rx);
    free(s_rx_storage);
    s_jobs = NULL;
    s_rx = NULL;
    s_rx_storage = NULL;
    return false;
}

static uint32_t begin(const char *text, const char *media)
{
    if (!s_lock || !start_worker()) return 0;
    job_t *job = heap_caps_calloc(1, sizeof(*job), MALLOC_CAP_SPIRAM);
    if (!job) return 0;
    LOCK();
    if (s_busy || (!media && !s_key[0])) { UNLOCK(); free(job); return 0; }
    if (media) {
        strcpy(job->media, media);
    } else {
        size_t n = muse_tts_utf8_prefix(text, sizeof(job->text) - 1);
        memcpy(job->text, text, n);
        strcpy(job->key, s_key);
        strcpy(job->speaker, s_speaker);
    }
    if (++s_generation == 0) ++s_generation;
    job->ticket = s_generation;
    job->started = esp_timer_get_time();
    xStreamBufferReset(s_rx);   /* the previous producer has finished */
    s_state = MUSE_TTS_WAITING;
    s_busy = true;
    uint32_t ticket = job->ticket;
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
        s_busy = false;
        s_state = MUSE_TTS_FAILED;
        memset(job->key, 0, sizeof(job->key));
        free(job);
        ticket = 0;
    }
    UNLOCK();
    return ticket;
}

uint32_t muse_tts_begin(const char *text)
{
    return text && text[0] ? begin(text, NULL) : 0;
}

uint32_t muse_reply_fetch_begin(const char *url)
{
    bool https;
    return muse_reply_media_source(url, &https) && https ? begin(NULL, url) : 0;
}

size_t muse_tts_read(uint32_t ticket, uint8_t *out, size_t cap)
{
    if (!ticket || !s_rx || !current(ticket)) return 0;
    return xStreamBufferReceive(s_rx, out, cap, 0);
}

muse_tts_state_t muse_tts_state(uint32_t ticket)
{
    if (!s_lock) return MUSE_TTS_FAILED;
    LOCK();
    muse_tts_state_t state = ticket == s_generation ? s_state : MUSE_TTS_FAILED;
    UNLOCK();
    return state;
}
