/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Seeed XIAO 1.47-inch IPS Touch Display with XIAO ESP32-S3 Plus:
 * 16 MB flash, 8 MB octal PSRAM, native USB Serial/JTAG (303a:1001).
 * Pins: https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/
 * https://wiki.seeedstudio.com/getting_started_1.47_inch_touch_display_esp32s3/
 * Panel, touch and PDM reference: https://github.com/Seeed-Studio/Seeed_GFX2
 * src/driver/tft/Driver_JD9853A.cpp, Driver_ST7789.cpp,
 * src/touch/Touch_AXS5106L.cpp and examples/.../0519_DashBoard_147_ESP32.
 * The panel register sequence is adapted from Seeed_GFX2; its MIT notice is
 * retained in devices/licenses/Seeed_GFX2.txt.
 *
 * External MAX98357A: DIN=D11/GPIO38, BCLK=D12/GPIO39, LRCLK=D13/GPIO40.
 * The PDM microphone uses I2S0; standard I2S playback uses I2S1.
 * This profile does not use SD/IMU.
 */
#include <math.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "driver/rtc_io.h"
#include "driver/spi_master.h"
#include "driver/usb_serial_jtag.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "muse_audio.h"
#include "muse_board.h"
#include "muse_mem.h"
#include "xiao_147_touch.h"

#define LCD_W 172
#define LCD_H 320
#define DRAW_LINES 32
#define LCD_HOST SPI2_HOST
#define LCD_CS GPIO_NUM_3       /* D2 */
#define LCD_DC GPIO_NUM_4       /* D3 */
#define LCD_SCLK GPIO_NUM_7     /* D8 */
#define LCD_MOSI GPIO_NUM_9     /* D10 */
#define LCD_RST GPIO_NUM_13     /* D17, shared with touch reset */
#define LCD_BL GPIO_NUM_12      /* D18, active high */
#define SD_CS GPIO_NUM_43       /* D6: keep the unused shared-SPI device deselected */
#define I2C_SDA GPIO_NUM_5      /* D4 */
#define I2C_SCL GPIO_NUM_6      /* D5 */
#define TOUCH_IRQ GPIO_NUM_44   /* D7 */
#define MIC_CLK GPIO_NUM_1      /* D0 */
#define MIC_DATA GPIO_NUM_2     /* D1 */
#define SPK_DATA GPIO_NUM_38    /* D11 -> MAX98357A DIN */
#define SPK_BCLK GPIO_NUM_39    /* D12 -> MAX98357A BCLK */
#define SPK_WS GPIO_NUM_40      /* D13 -> MAX98357A LRC/LRCLK */
#define TALK_GPIO GPIO_NUM_11   /* D19 / USR1, active low */
#define AUX_GPIO GPIO_NUM_42    /* D15 / USR2, active low */

static const char *TAG = "xiao147";
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static i2c_master_dev_handle_t s_touch;
static i2s_chan_handle_t s_mic;
static i2s_chan_handle_t s_spk_tx;
static muse_gpio_button_t s_talk, s_aux;
static bool s_mic_on;
static bool s_spk_on;
static bool s_mic_settled;
static int32_t s_mic_dc;
static int s_gain_q8 = 256;
static lv_point_t s_touch_point;

static esp_err_t init(void)
{
    const gpio_config_t outputs = {
        .pin_bit_mask = BIT64(LCD_RST) | BIT64(LCD_BL) | BIT64(SD_CS),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), TAG, "display pins");
    gpio_set_level(LCD_BL, 0);
    gpio_set_level(SD_CS, 1);
    /* Reset both controllers once. Resetting touch later would also reset LCD. */
    gpio_set_level(LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(300));

    ESP_RETURN_ON_ERROR(muse_gpio_button_init(&s_talk, TALK_GPIO), TAG, "USR1");
    ESP_RETURN_ON_ERROR(muse_gpio_button_init(&s_aux, AUX_GPIO), TAG, "USR2");
    s_talk.pressed = gpio_get_level(TALK_GPIO) == 0;
    s_aux.pressed = gpio_get_level(AUX_GPIO) == 0;
    const gpio_config_t irq = {
        .pin_bit_mask = BIT64(TOUCH_IRQ), .mode = GPIO_MODE_INPUT, .pull_up_en = true,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&irq), TAG, "touch interrupt");
    i2c_master_bus_handle_t bus;
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0, .sda_io_num = I2C_SDA, .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "touch bus");
    const i2c_device_config_t touch_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x63,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(bus, &touch_cfg, &s_touch);
}

static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint8_t reg = 0x01, report[14];
    uint16_t x, y;
    /* Match the vendor's STOP between selecting the register and reading it.
     * Poll even when INT is high, so a missed edge cannot leave touch stuck. */
    bool pressed = i2c_master_transmit(s_touch, &reg, 1, 20) == ESP_OK &&
                   i2c_master_receive(s_touch, report, sizeof(report), 20) == ESP_OK &&
                   xiao_147_touch_decode(report, sizeof(report), &x, &y);
    if (pressed) {
        s_touch_point.x = x;
        s_touch_point.y = y;
    }
    data->point = s_touch_point;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static esp_err_t panel_configure(void)
{
    /* Seeed's verified ST7789-compatible JD9853A sequence. */
    static const struct {
        uint8_t cmd, length, data[14];
    } commands[] = {
        {0x13, 0, {0}},
        {0xb6, 2, {0x0a, 0x82}}, {0xb0, 2, {0x00, 0xe0}},
        {0x3a, 1, {0x55}}, {0xb2, 5, {0x0c, 0x0c, 0x00, 0x33, 0x33}},
        {0xb7, 1, {0x35}}, {0xbb, 1, {0x28}}, {0xc0, 1, {0x0c}},
        {0xc2, 2, {0x01, 0xff}}, {0xc3, 1, {0x10}}, {0xc4, 1, {0x20}},
        {0xc6, 1, {0x0f}}, {0xd0, 2, {0xa4, 0xa1}},
        {0xe0, 14, {0xd0,0x00,0x02,0x07,0x0a,0x28,0x32,0x44,0x42,0x06,0x0e,0x12,0x14,0x17}},
        {0xe1, 14, {0xd0,0x00,0x02,0x07,0x0a,0x28,0x31,0x54,0x47,0x0e,0x1c,0x17,0x1b,0x1e}},
    };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "LCD init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_panel_io, 0x11, NULL, 0), TAG, "sleep out");
    vTaskDelay(pdMS_TO_TICKS(120));
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_panel_io, commands[i].cmd,
                            commands[i].data, commands[i].length), TAG, "LCD register");
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, false), TAG, "LCD portrait");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, 34, 0), TAG, "LCD offset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, false), TAG, "LCD colours");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "LCD on");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

static lv_display_t *display_start(lv_indev_t **touch)
{
    *touch = NULL;
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0, .freq_hz = 20000, .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t backlight = {
        .gpio_num = LCD_BL, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 0,
    };
    if (ledc_timer_config(&timer) != ESP_OK || ledc_channel_config(&backlight) != ESP_OK) {
        return NULL;
    }
    const spi_bus_config_t bus = {
        .sclk_io_num = LCD_SCLK, .mosi_io_num = LCD_MOSI, .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC, .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_W * DRAW_LINES * 2,
    };
    if (spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        return NULL;
    }
    const esp_lcd_panel_io_spi_config_t io = {
        .cs_gpio_num = LCD_CS, .dc_gpio_num = LCD_DC, .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000, .trans_queue_depth = 10,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    const esp_lcd_panel_dev_config_t panel = {
        .reset_gpio_num = -1, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    if (esp_lcd_new_panel_io_spi(LCD_HOST, &io, &s_panel_io) != ESP_OK ||
        esp_lcd_new_panel_st7789(s_panel_io, &panel, &s_panel) != ESP_OK ||
        panel_configure() != ESP_OK) {
        return NULL;
    }
    esp_lv_adapter_config_t adapter = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter.task_core_id = MUSE_UI_CORE;
    adapter.task_priority = MUSE_UI_PRIORITY;
    if (esp_lv_adapter_init(&adapter) != ESP_OK) {
        return NULL;
    }
    const esp_lv_adapter_display_config_t display = {
        .panel = s_panel, .panel_io = s_panel_io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = ESP_LV_ADAPTER_ROTATE_0, .hor_res = LCD_W, .ver_res = LCD_H,
            .buffer_height = DRAW_LINES, .use_psram = false, .require_double_buffer = true,
        },
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
    };
    lv_display_t *disp = esp_lv_adapter_register_display(&display);
    if (!disp || esp_lv_adapter_start() != ESP_OK || esp_lv_adapter_lock(-1) != ESP_OK) {
        return NULL;
    }
    *touch = lv_indev_create();
    if (*touch) {
        lv_indev_set_type(*touch, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(*touch, disp);
        lv_indev_set_read_cb(*touch, touch_read);
    }
    esp_lv_adapter_unlock();
    return *touch ? disp : NULL;
}

static bool display_lock(int timeout_ms) { return esp_lv_adapter_lock(timeout_ms) == ESP_OK; }
static void set_brightness(int pct)
{
    pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, pct * 1023 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
static void panel_sleep(bool sleep) { esp_lcd_panel_disp_sleep(s_panel, sleep); }

static int mic_enable(const audio_codec_data_if_t *h, esp_codec_dev_type_t type, bool on)
{
    (void)h; (void)type;
    if (on != s_mic_on) {
        if ((on ? i2s_channel_enable(s_mic) : i2s_channel_disable(s_mic)) != ESP_OK) {
            return ESP_CODEC_DEV_DRV_ERR;
        }
        s_mic_on = on;
        s_mic_settled = false;
    }
    return ESP_CODEC_DEV_OK;
}
static int mic_read(const audio_codec_data_if_t *h, uint8_t *data, int size)
{
    (void)h;
    if (size <= 0 || size % 4 != 0) { return ESP_CODEC_DEV_READ_FAIL; }
    int16_t mono[MUSE_AUDIO_CHUNK];
    int16_t *stereo = (int16_t *)data;
    for (int left = size / 4; left > 0;) {
        int frames = left > MUSE_AUDIO_CHUNK ? MUSE_AUDIO_CHUNK : left;
        size_t got = 0;
        size_t bytes = frames * sizeof(int16_t);
        if (i2s_channel_read(s_mic, mono, bytes, &got, 1000) != ESP_OK || got != bytes) {
            return ESP_CODEC_DEV_READ_FAIL;
        }
        if (!s_mic_settled) {
            int32_t sum = 0;
            for (int i = 0; i < frames; i++) { sum += mono[i]; }
            s_mic_dc = sum / frames * 256;
            s_mic_settled = true;
        }
        for (int i = 0; i < frames; i++) {
            /* Remove PDM filter DC before gain, so it cannot cause clipping.
             * Muse's shared 80 Hz high-pass then removes residual rumble. */
            s_mic_dc += (mono[i] * 256 - s_mic_dc) >> 8;
            int value = (mono[i] - (s_mic_dc >> 8)) * s_gain_q8 / 256;
            int16_t sample = value < INT16_MIN ? INT16_MIN : (value > INT16_MAX ? INT16_MAX : value);
            stereo[2*i] = stereo[2*i+1] = sample;
        }
        stereo += frames * 2;
        left -= frames;
    }
    return ESP_CODEC_DEV_OK;
}
static void set_mic_gain(esp_codec_dev_handle_t mic, int db)
{
    (void)mic;
    /* Unity at the UI's default 30 dB; analogue codec gain does not apply to PDM. */
    s_gain_q8 = (int)(256.0f * powf(10.0f, (db - 30) / 20.0f));
}
static int spk_enable(const audio_codec_data_if_t *h, esp_codec_dev_type_t type, bool on)
{
    (void)h; (void)type;
    if (on != s_spk_on) {
        esp_err_t err = on ? i2s_channel_enable(s_spk_tx) : i2s_channel_disable(s_spk_tx);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "speaker %s failed: %s", on ? "enable" : "disable", esp_err_to_name(err));
            return ESP_CODEC_DEV_DRV_ERR;
        }
        s_spk_on = on;
    }
    return ESP_CODEC_DEV_OK;
}
static bool spk_is_open(const audio_codec_data_if_t *h)
{
    (void)h;
    /* Codec-dev probes the data interface before calling enable(true), and
     * again on reopening after sleep. Report initialized hardware here;
     * spk_write separately checks whether transmission has been enabled. */
    return s_spk_tx != NULL;
}
static int spk_write(const audio_codec_data_if_t *h, uint8_t *data, int size)
{
    (void)h;
    if (!s_spk_on || size < 0 || size % 4 != 0) { return ESP_CODEC_DEV_WRITE_FAIL; }
    if (size == 0) { return ESP_CODEC_DEV_OK; }
    size_t written = 0;
    /* Both slots carry Muse's mono sample, so the amplifier can select either
     * channel or their average. Codec-dev applies the software volume first.
     * The I2S driver takes milliseconds, not FreeRTOS ticks, for this timeout. */
    if (i2s_channel_write(s_spk_tx, data, size, &written, 1000) != ESP_OK || written != (size_t)size) {
        return ESP_CODEC_DEV_WRITE_FAIL;
    }
    return ESP_CODEC_DEV_OK;
}
static esp_err_t audio_init(esp_codec_dev_handle_t *spk, esp_codec_dev_handle_t *mic)
{
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = 4;
    channel.dma_frame_num = MUSE_AUDIO_CHUNK;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel, NULL, &s_mic), TAG, "PDM channel");
    const i2s_pdm_rx_config_t pdm = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .clk = MIC_CLK, .din = MIC_DATA },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_pdm_rx_mode(s_mic, &pdm), TAG, "PDM mode");
    gpio_set_drive_capability(MIC_CLK, GPIO_DRIVE_CAP_0);

    /* PDM RX conversion exists only on I2S0 on the S3. Use the independent
     * I2S1 controller for the amplifier so playback cannot reconfigure the mic. */
    i2s_chan_config_t speaker = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    /* 30 ms of TX DMA (1920 bytes) leaves internal RAM for Muse's TLS/Link
     * sessions. muse_audio_write supplies 20 ms blocks with backpressure. */
    speaker.dma_desc_num = 3;
    speaker.dma_frame_num = MUSE_AUDIO_RATE / 100;
    speaker.auto_clear = true;  /* Idle DMA sends silence instead of stale speech. */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&speaker, &s_spk_tx, NULL), TAG, "speaker channel");
    const i2s_std_config_t standard = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, .bclk = SPK_BCLK, .ws = SPK_WS,
            .dout = SPK_DATA, .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_spk_tx, &standard), TAG, "MAX98357A I2S mode");
    static const audio_codec_data_if_t mic_if = { .enable = mic_enable, .read = mic_read };
    static const audio_codec_data_if_t spk_if = {
        .enable = spk_enable, .is_open = spk_is_open, .write = spk_write,
    };
    esp_codec_dev_cfg_t in = { .dev_type = ESP_CODEC_DEV_TYPE_IN, .data_if = &mic_if };
    esp_codec_dev_cfg_t out = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .data_if = &spk_if };
    *mic = esp_codec_dev_new(&in);
    *spk = esp_codec_dev_new(&out);
    ESP_LOGI(TAG, "PDM mic on I2S0; MAX98357A on I2S1: DIN=%d BCLK=%d LRCLK=%d, %d Hz stereo 16-bit",
             SPK_DATA, SPK_BCLK, SPK_WS, MUSE_AUDIO_RATE);
    return *mic && *spk ? ESP_OK : ESP_ERR_NO_MEM;
}
static unsigned poll_buttons(void)
{
    return muse_gpio_button_poll(&s_talk) | (muse_gpio_button_poll(&s_aux) << 2);
}
static esp_err_t read_power(muse_power_t *out)
{
    /* No charger-status GPIO. Do not infer battery presence from a voltage that
     * can also appear on USB power. Voltage/IMU support is left for a later port. */
    *out = (muse_power_t){ .usb = usb_serial_jtag_is_connected(), .battery_pct = -1 };
    return ESP_OK;
}
static esp_err_t power_off(void)
{
    set_brightness(0);
    esp_lcd_panel_disp_on_off(s_panel, false);
    while (gpio_get_level(TALK_GPIO) == 0 || gpio_get_level(AUX_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    rtc_gpio_init(TALK_GPIO);
    rtc_gpio_set_direction(TALK_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(TALK_GPIO);
    rtc_gpio_pulldown_dis(TALK_GPIO);
    ESP_RETURN_ON_ERROR(esp_sleep_enable_ext1_wakeup_io(BIT64(TALK_GPIO), ESP_EXT1_WAKEUP_ANY_LOW), TAG, "USR1 wake");
    esp_deep_sleep_start();
    return ESP_FAIL;
}
static const muse_board_t s_board = {
    .name = "Seeed XIAO ESP32-S3 1.47 Touch Display",
    .width = LCD_W, .height = LCD_H, .touch = true, .diagonal_in = 1.47f,
    .talk_button = "USR1", .aux_button = "USR2",
    .talk_hint = { LV_ALIGN_BOTTOM_LEFT, 4, -4 },
    .aux_hint = { LV_ALIGN_BOTTOM_RIGHT, -4, -4 },
    .frame_ms = 40, .init = init, .display_start = display_start,
    .display_lock = display_lock, .display_unlock = esp_lv_adapter_unlock,
    .set_brightness = set_brightness, .panel_sleep = panel_sleep,
    .audio_init = audio_init, .mic_slot = 0, .set_mic_gain = set_mic_gain,
    .poll_buttons = poll_buttons, .read_power = read_power, .power_off = power_off,
};
const muse_board_t *muse_board_get(void) { return &s_board; }
