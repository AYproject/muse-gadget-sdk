/* SPDX-License-Identifier: Apache-2.0 */
/* Exercise the board's real callbacks through esp_codec_dev's real lifecycle. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_codec_dev.h"
#include "audio_codec_sw_vol.h"

typedef void *i2s_chan_handle_t;
static i2s_chan_handle_t s_spk_tx;
static bool s_spk_on, driver_on, partial_write;
static unsigned enabled, disabled, writes;
static const char *TAG = "xiao147";
static const char *esp_err_to_name(int error) { (void)error; return "error"; }
static int i2s_channel_enable(i2s_chan_handle_t channel)
{
    assert(channel && !driver_on);
    driver_on = true; ++enabled;
    return ESP_OK;
}
static int i2s_channel_disable(i2s_chan_handle_t channel)
{
    assert(channel && driver_on);
    driver_on = false; ++disabled;
    return ESP_OK;
}
static int i2s_channel_write(i2s_chan_handle_t channel, const void *data,
                             size_t size, size_t *done, unsigned timeout_ms)
{
    assert(channel && driver_on && data && timeout_ms == 1000);
    ++writes;
    *done = partial_write ? size - 4 : size;
    return ESP_OK;
}

#include "esp_log.h"
#include "board_speaker_callbacks.inc"

/* Volume DSP is outside this lifecycle check; keep the codec's normal path. */
static int vol_open(const audio_codec_vol_if_t *h, esp_codec_dev_sample_info_t *fs, int fade)
{ (void)h; (void)fs; (void)fade; return ESP_CODEC_DEV_OK; }
static int vol_set(const audio_codec_vol_if_t *h, float db)
{ (void)h; (void)db; return ESP_CODEC_DEV_OK; }
static int vol_process(const audio_codec_vol_if_t *h, uint8_t *in, int len, uint8_t *out, int out_len)
{ (void)h; assert(len == out_len); memmove(out, in, len); return ESP_CODEC_DEV_OK; }
static int vol_close(const audio_codec_vol_if_t *h)
{ (void)h; return ESP_CODEC_DEV_OK; }
static const audio_codec_vol_if_t volume = {
    .open = vol_open, .set_vol = vol_set, .process = vol_process, .close = vol_close,
};
const audio_codec_vol_if_t *audio_codec_new_sw_vol(void) { return &volume; }
int audio_codec_delete_vol_if(const audio_codec_vol_if_t *h)
{ assert(h == &volume); return ESP_CODEC_DEV_OK; }

int main(void)
{
    const audio_codec_data_if_t data = {
        .enable = spk_enable, .is_open = spk_is_open, .write = spk_write,
    };
    esp_codec_dev_cfg_t config = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .data_if = &data };
    esp_codec_dev_handle_t codec = esp_codec_dev_new(&config);
    assert(codec);
    esp_codec_dev_sample_info_t format = {.sample_rate=16000, .bits_per_sample=16, .channel=2};
    int16_t samples[] = {123, 123, -456, -456};
    /* Not initialized: reject opening without touching the driver. */
    assert(esp_codec_dev_open(codec, &format) == ESP_CODEC_DEV_NOT_SUPPORT);
    assert(enabled == 0);
    int channel;
    s_spk_tx = &channel;
    /* Ready but not yet enabled: open must succeed. This caught the regression. */
    assert(!s_spk_on && spk_is_open(&data));
    assert(esp_codec_dev_open(codec, &format) == ESP_CODEC_DEV_OK);
    assert(s_spk_on && driver_on && enabled == 1);
    assert(esp_codec_dev_open(codec, &format) == ESP_CODEC_DEV_OK && enabled == 1);
    assert(spk_enable(&data, ESP_CODEC_DEV_TYPE_OUT, true) == ESP_CODEC_DEV_OK && enabled == 1);
    assert(esp_codec_dev_write(codec, samples, sizeof(samples)) == ESP_CODEC_DEV_OK && writes == 1);
    partial_write = true;
    assert(esp_codec_dev_write(codec, samples, sizeof(samples)) == ESP_CODEC_DEV_WRITE_FAIL);
    partial_write = false;
    assert(spk_write(&data, (uint8_t *)samples, 3) == ESP_CODEC_DEV_WRITE_FAIL);
    assert(spk_write(&data, (uint8_t *)samples, -4) == ESP_CODEC_DEV_WRITE_FAIL);
    assert(esp_codec_dev_close(codec) == ESP_CODEC_DEV_OK && !s_spk_on && disabled == 1);
    assert(esp_codec_dev_write(codec, samples, sizeof(samples)) == ESP_CODEC_DEV_WRONG_STATE);
    /* Closing stops clocks; initialized hardware remains ready for waking. */
    assert(spk_is_open(&data));
    assert(esp_codec_dev_open(codec, &format) == ESP_CODEC_DEV_OK && enabled == 2);
    assert(esp_codec_dev_write(codec, samples, sizeof(samples)) == ESP_CODEC_DEV_OK);
    esp_codec_dev_delete(codec);
    assert(!driver_on && disabled == 2);
    puts("XIAO MAX98357A codec open/write/close/reopen and partial-write checks passed");
    return 0;
}
