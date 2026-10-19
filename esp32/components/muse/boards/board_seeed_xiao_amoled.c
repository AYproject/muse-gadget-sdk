/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Seeed XIAO AMOLED Display: a XIAO ESP32-S3 Plus (16 MB flash, 8 MB octal
 * PSRAM, native USB) on a carrier that takes one of several CO5300 QSPI AMOLED
 * modules on the same 31-pin FPC, chosen at build time (Kconfig):
 * - the 2.05" 368x448 rounded rectangle (Huaxia RGB H0205S003AMT003A, CST820),
 * - the 1.75" 466x466 round one (Huaxia RGB H0175Y003AMT003, CST820B),
 * - the 2.16" 480x480 square one (Huaxia RGB H0216F002AM, CST9220).
 * Both CST820s answer at 0x15 with the same registers; the CST9220 at 0x5A
 * with 16-bit ones.
 *
 * The carrier (Seeed "XIAO 2.16 AMOLED Display" schematic v1.0, 2026-09-10):
 * - One I2C bus on D4/D5 (GPIO5/6) for everything: a PCA9535 expander (0x21),
 *   the touch controller, ES8311 (0x18), ES7243E (0x14), PCF8563 RTC (0x51),
 *   LSM6DSO IMU (0x6A) and MMC5603 magnetometer (0x30). Muse uses none of the
 *   last three; the IMU and magnetometer power up idle.
 * - The expander: P00 panel reset, P01 touch reset, P02 ENTER and P03 BACK
 *   (active low, 10K pull-ups), P04 the NS4150B amp, P05 SD detect, P06 SD
 *   rail, P07 audio LDO enable (R70 not fitted: the LDO is always on), P10 the
 *   module's VCI_EN, P11 the panel's VBAT switch, P12 the battery divider's
 *   switch, P13 IMU CS (high: I2C), P14/P15 IMU interrupts, P16 the panel's
 *   VCI switch, P17 EXT_IO1. Its INT is on D3 (GPIO4).
 * - QSPI: CS D6 (GPIO43), SCK D7 (GPIO44), D0-D3 on D16/D19/D18/D17 (GPIO10,
 *   11, 12, 13). TE isn't fitted (R90). Touch INT on D2 (GPIO3).
 * - I2S: MCLK D11 (GPIO38), BCLK D12 (39), LRCK D13 (40), the ES7243E's SDOUT
 *   to D14 (41), the ES8311's DSDIN from D15 (42). The ES7243E has the mic on
 *   its right input and the DAC's output on its left (a loopback Muse leaves
 *   unused). The ES8311's ADC isn't wired.
 * - The battery reaches GPIO1 (D0) through a 10K/10K divider that the
 *   expander's P12 switches in. Power comes on and off with the slide switch.
 * XIAO pin to GPIO: the esp32 Arduino core's variants/XIAO_ESP32S3_Plus.
 * Neither module's datasheet gives an init. The 2.05"'s, and its columns
 * starting 16 in, follow Waveshare's ESP32-S3-Touch-AMOLED-1.8 (V2) 368x448
 * CO5300 colour-bar example (examples/esp-idf/13_display_colorbar). The
 * 1.75"'s, and its 466 columns starting 6 in, are the StopWatch's
 * (board_m5stack_stopwatch.c), the same 466 px round CO5300 panel. The
 * 2.16"'s, and its CST9220 reads, follow Waveshare's ESP32-S3-Touch-AMOLED-2.16
 * BSP (waveshare/esp32_s3_touch_amoled_2_16 2.0.1, the same CO5300 + CST9220
 * module) and its waveshare/esp_lcd_touch_cst9217 2.0.0, less the BSP's
 * MADCTL, which turns the panel for Waveshare's own case.
 * Display and touch reads follow board_m5stack_stopwatch.c (CO5300 + CST820);
 * the ES8311 + ES7243E pair follows board_sensecap_watcher.c.
 * Each module sits its own way in the enclosure, and LCD_ROTATION turns the
 * picture clockwise to match it in the panel (MADCTL), with touch turned the
 * same way: 180 degrees for the 2.05", 90 for the 2.16", none for the 1.75".
 * Touch reports in the panel's own orientation on all three. Checked on
 * hardware.
 */
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/rtc_io.h"
#include "driver/spi_master.h"
#include "driver/usb_serial_jtag.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "muse_audio.h"
#include "muse_board.h"
#include "muse_lcd_bands.h"
#include "muse_mem.h"
#include "muse_state.h"

static const char *TAG = "board";

#if CONFIG_MUSE_BOARD_SEEED_XIAO_AMOLED_175
#define LCD_NAME "Seeed XIAO AMOLED 1.75"
#define LCD_H_RES 466
#define LCD_V_RES 466
#define LCD_GAP_X 6                /* the 466 columns in the controller's 480, as on the StopWatch */
#define LCD_ROUND true
#define LCD_ROTATION 0
#define LCD_DIAGONAL 1.75f
#define DRAW_BUF_LINES 118         /* four bands to the screen (muse_lcd_bands.h) */
#elif CONFIG_MUSE_BOARD_SEEED_XIAO_AMOLED_216
#define LCD_NAME "Seeed XIAO AMOLED 2.16"
#define LCD_H_RES 480
#define LCD_V_RES 480
#define LCD_GAP_X 0
#define LCD_ROUND false
#define LCD_ROTATION 90            /* clockwise, square: the resolution stays */
#define LCD_DIAGONAL 2.16f
#define DRAW_BUF_LINES 120
#define TP_CST9220 1
#else
#define LCD_NAME "Seeed XIAO AMOLED 2.05"
#define LCD_H_RES 368
#define LCD_V_RES 448
#define LCD_GAP_X 16               /* Waveshare's V2 module */
#define LCD_ROUND false
#define LCD_ROTATION 180           /* upside down in the enclosure */
#define LCD_DIAGONAL 2.05f
#define DRAW_BUF_LINES 112
#endif
_Static_assert(LCD_ROTATION != 90 || LCD_H_RES == LCD_V_RES, "a quarter turn needs a square panel");
#define LCD_HOST SPI2_HOST
#define LCD_CS GPIO_NUM_43         /* D6 */
#define LCD_SCLK GPIO_NUM_44       /* D7 */
#define LCD_D0 GPIO_NUM_10         /* D16 */
#define LCD_D1 GPIO_NUM_11         /* D19 */
#define LCD_D2 GPIO_NUM_12         /* D18 */
#define LCD_D3 GPIO_NUM_13         /* D17 */
#define LCD_CHUNK_BYTES (LCD_H_RES * 8 * 2)
#define LCD_LEVEL_BOOT 0xA0        /* until the UI sets its own */
#define LCD_POWER_UP_MS 20         /* VCI and VBAT up before the reset is let go */
#define LCD_RESET_MS 120           /* the panel's wake from reset */

#define I2C_SDA GPIO_NUM_5         /* D4 */
#define I2C_SCL GPIO_NUM_6         /* D5 */
#define TP_INT GPIO_NUM_3          /* D2 */
#define I2S_MCLK GPIO_NUM_38       /* D11 */
#define I2S_BCLK GPIO_NUM_39       /* D12 */
#define I2S_WS GPIO_NUM_40         /* D13 */
#define I2S_DIN GPIO_NUM_41        /* D14, from the ES7243E */
#define I2S_DOUT GPIO_NUM_42       /* D15, to the ES8311 */
#define ES7243E_ADDR 0x14
#define AMP_ON_MS 20               /* the amp starting up before the first sound */

#define BATT_ADC ADC_CHANNEL_0     /* GPIO1 (D0), half the battery */
#define BATT_SETTLE_MS 5           /* the divider switched in: 100 nF through 5K */

#if TP_CST9220
#define TP_ADDR 0x5A
#define TP_REG_DATA 0xD000         /* one point, then the point count and an ACK */
#define TP_REG_CMD_MODE 0xD101     /* D1 01: command mode, to read the chip's ID */
#define TP_REG_PROJECT_ID 0xD204   /* the chip type in the high half */
#define TP_ACK 0xAB
#define TP_REG_WAIT_MS 2           /* between a register's address and its read */
#else
#define TP_ADDR 0x15
#define TP_REG_POINTS 0x02         /* finger count, then X and Y, 12 bits each */
#define TP_REG_SLEEP 0xE5          /* 0x03: deep sleep until reset */
#define TP_REG_NO_AUTO_SLEEP 0xFE  /* else it stops answering I2C when idle */
#endif

/* PCA9535: port 0 in the low byte, port 1 in the high byte. */
#define EXP_ADDR 0x21
#define EXP_INT GPIO_NUM_4         /* D3: low from an input changing until the inputs are read */
#define EXP_REG_INPUT 0x00
#define EXP_REG_OUTPUT 0x02
#define EXP_REG_CONFIG 0x06        /* 1 = input */
#define EXP_LCD_RST BIT(0)
#define EXP_TP_RST BIT(1)
#define EXP_KEY_ENTER BIT(2)
#define EXP_KEY_BACK BIT(3)
#define EXP_PA_EN BIT(4)
#define EXP_SD_EN BIT(6)
#define EXP_AUDIO_EN BIT(7)
#define EXP_LCD_VCI_EN BIT(8)      /* the module's own VCI_EN */
#define EXP_LCD_VBAT BIT(9)        /* U8: the panel's VBAT */
#define EXP_ADC_EN BIT(10)         /* U6: the battery divider */
#define EXP_IMU_CS BIT(11)
#define EXP_LCD_VCI BIT(14)        /* U23: the panel's VCI */
#define EXP_OUTPUTS (EXP_LCD_RST | EXP_TP_RST | EXP_PA_EN | EXP_SD_EN | EXP_AUDIO_EN | EXP_LCD_VCI_EN | \
                     EXP_LCD_VBAT | EXP_ADC_EN | EXP_IMU_CS | EXP_LCD_VCI)
#define EXP_LCD_RAILS (EXP_LCD_VCI | EXP_LCD_VBAT | EXP_LCD_VCI_EN)

#define DEBOUNCE_SAMPLES 3
#define WAIT_DEBOUNCE_MS 10        /* wait_buttons() mid-debounce: the awake poll */
#define WAIT_BUSY_MS 50            /* ... and with EXP_INT already low */

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_exp, s_tp;
static SemaphoreHandle_t s_exp_lock;
static uint16_t s_exp_out;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
/* The panel's state, kept on the send task (muse_lcd_bands_run): the CO5300
 * forgets its setup, brightness and sleep with its rails (display_pause). */
static bool s_lcd_off;
static uint8_t s_lcd_level = LCD_LEVEL_BOOT;
static bool s_lcd_asleep;
static uint16_t s_keys;            /* debounced EXP_KEY_* down */
static uint8_t s_key_stable;       /* samples the keys have read changed */
static TaskHandle_t s_waiter;      /* the input task, woken out of wait_buttons() */

static esp_err_t exp_write(uint8_t reg, uint16_t v)
{
    const uint8_t buf[] = { reg, v & 0xff, v >> 8 };
    return i2c_master_transmit(s_exp, buf, sizeof(buf), 50);
}

static esp_err_t exp_read(uint16_t *v)
{
    const uint8_t reg = EXP_REG_INPUT;
    uint8_t buf[2];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_exp, &reg, 1, buf, sizeof(buf), 50), TAG, "expander read");
    *v = buf[0] | buf[1] << 8;
    return ESP_OK;
}

/* The amp (audio task), touch reset (input task) and the panel rails all change outputs. */
static esp_err_t exp_set(uint16_t mask, bool on)
{
    xSemaphoreTake(s_exp_lock, portMAX_DELAY);
    s_exp_out = on ? s_exp_out | mask : s_exp_out & ~mask;
    esp_err_t err = exp_write(EXP_REG_OUTPUT, s_exp_out);
    xSemaphoreGive(s_exp_lock);
    return err;
}

static esp_err_t add_device(uint8_t addr, uint32_t hz, i2c_master_dev_handle_t *dev)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = hz,
    };
    return i2c_master_bus_add_device(s_i2c, &cfg, dev);
}

static esp_err_t tp_write(uint8_t reg, uint8_t v)
{
    const uint8_t buf[] = { reg, v };
    return i2c_master_transmit(s_tp, buf, sizeof(buf), 50);
}

#if TP_CST9220
/* Its register reads are two transfers, the address and then the data, with
 * a pause between (Waveshare's cst9217_read_reg). */
static esp_err_t tp_read_reg(uint16_t reg, uint8_t *buf, size_t n)
{
    const uint8_t addr[] = { reg >> 8, reg & 0xff };
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_tp, addr, sizeof(addr), 50), TAG, "touch address");
    vTaskDelay(pdMS_TO_TICKS(TP_REG_WAIT_MS));
    return i2c_master_receive(s_tp, buf, n, 50);
}

/* Reset pulse, then the chip's type, read as Waveshare's driver does after
 * one: the command-mode register's address, then D1 01 written to it. */
static void tp_reset(void)
{
    exp_set(EXP_TP_RST, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    exp_set(EXP_TP_RST, true);
    vTaskDelay(pdMS_TO_TICKS(50));
    if (tp_write(TP_REG_CMD_MODE >> 8, TP_REG_CMD_MODE & 0xff) != ESP_OK) {
        ESP_LOGW(TAG, "touch not answering");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(TP_REG_WAIT_MS));
    if (tp_write(0xD1, 0x01) != ESP_OK) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t id[4];
    if (tp_read_reg(TP_REG_PROJECT_ID, id, sizeof(id)) == ESP_OK) {
        ESP_LOGI(TAG, "touch chip 0x%04x", id[3] << 8 | id[2]);
    }
}

/* Deep sleep until its reset line wakes it (D1 05, as board_waveshare_s3_216.c sends). */
static void tp_sleep(void)
{
    tp_write(0xD1, 0x05);
}

/* One point: a status nibble (6 while down), then 12-bit X and Y. */
static bool tp_point(int *x, int *y)
{
    uint8_t b[10];
    if (tp_read_reg(TP_REG_DATA, b, sizeof(b)) != ESP_OK || b[6] != TP_ACK || !(b[5] & 0x7F) ||
        (b[0] & 0x0F) != 0x06) {
        return false;
    }
    *x = b[1] << 4 | b[3] >> 4;
    *y = b[2] << 4 | (b[3] & 0x0F);
    return true;
}
#else
/* Reset pulse on the touch controller, then keep it answering while idle. */
static void tp_reset(void)
{
    exp_set(EXP_TP_RST, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    exp_set(EXP_TP_RST, true);
    vTaskDelay(pdMS_TO_TICKS(50));
    if (tp_write(TP_REG_NO_AUTO_SLEEP, 0x01) != ESP_OK) {
        ESP_LOGW(TAG, "touch not answering");
    }
}

static void tp_sleep(void)
{
    tp_write(TP_REG_SLEEP, 0x03);
}

/* The CST820 reports one point. */
static bool tp_point(int *x, int *y)
{
    const uint8_t reg = TP_REG_POINTS;
    uint8_t b[5];
    if (i2c_master_transmit_receive(s_tp, &reg, 1, b, sizeof(b), 50) != ESP_OK || (b[0] & 0x0F) == 0) {
        return false;
    }
    *x = (b[1] & 0x0F) << 8 | b[2];
    *y = (b[3] & 0x0F) << 8 | b[4];
    return true;
}
#endif

/* The expander's INT, armed by wait_buttons(). Level-triggered, so off until armed again. */
static void on_exp_int(void *arg)
{
    (void)arg;
    gpio_intr_disable(EXP_INT);
    BaseType_t woken = pdFALSE;
    if (s_waiter) {
        vTaskNotifyGiveFromISR(s_waiter, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

/* Wakes on ENTER: any expander input pulls INT low, so a stray one sleeps again. */
static void sleep_until_enter(void)
{
    uint16_t in;
    exp_read(&in);  /* clears the interrupt */
    rtc_gpio_pullup_en(EXP_INT);
    rtc_gpio_pulldown_dis(EXP_INT);
    esp_sleep_enable_ext0_wakeup(EXP_INT, 0);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_deep_sleep_start();
}

static esp_err_t init(void)
{
    s_exp_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_exp_lock, ESP_ERR_NO_MEM, TAG, "expander lock");
    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &s_i2c), TAG, "i2c");
    ESP_RETURN_ON_ERROR(add_device(EXP_ADDR, 400000, &s_exp), TAG, "expander");
    ESP_RETURN_ON_ERROR(add_device(TP_ADDR, 400000, &s_tp), TAG, "touch");

    /* Everything off and both resets held, the IMU on I2C; then the outputs. */
    s_exp_out = EXP_IMU_CS | EXP_AUDIO_EN;
    ESP_RETURN_ON_ERROR(exp_write(EXP_REG_OUTPUT, s_exp_out), TAG, "expander outputs");
    ESP_RETURN_ON_ERROR(exp_write(EXP_REG_CONFIG, (uint16_t)~EXP_OUTPUTS), TAG, "expander config");
    uint16_t in;
    ESP_RETURN_ON_ERROR(exp_read(&in), TAG, "expander inputs");
    if ((esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_EXT0)) && (in & EXP_KEY_ENTER)) {
        sleep_until_enter();   /* powered off, and another input woke us */
    }
    ESP_LOGI(TAG, "expander inputs 0x%04x", in);
    /* A key held through boot (the one that woke it) isn't a press. */
    s_keys = ~in & (EXP_KEY_ENTER | EXP_KEY_BACK);

    /* The panel's power-on order: IOVCC (always on), VCI, then VBAT, then the
     * reset let go (datasheet 9.2). Touch comes out of reset with it. */
    ESP_RETURN_ON_ERROR(exp_set(EXP_LCD_VCI, true), TAG, "panel vci");
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(exp_set(EXP_LCD_VBAT | EXP_LCD_VCI_EN, true), TAG, "panel vbat");
    vTaskDelay(pdMS_TO_TICKS(LCD_POWER_UP_MS));
    ESP_RETURN_ON_ERROR(exp_set(EXP_LCD_RST | EXP_TP_RST, true), TAG, "panel reset");
    vTaskDelay(pdMS_TO_TICKS(LCD_RESET_MS));

    esp_err_t err = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "isr service");
    const gpio_config_t int_cfg = {
        .pin_bit_mask = BIT64(EXP_INT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "expander int");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(EXP_INT, on_exp_int, NULL), TAG, "expander int isr");
    gpio_intr_disable(EXP_INT);

    const adc_oneshot_unit_init_cfg_t adc_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&adc_cfg, &s_adc), TAG, "adc");
    const adc_oneshot_chan_cfg_t ch_cfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, BATT_ADC, &ch_cfg), TAG, "adc channel");
    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = BATT_ADC,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "no ADC calibration: battery level disabled");
    }
    return ESP_OK;
}

/* The module's init, less what the driver and panel_setup() send: COLMOD and
 * MADCTL come from the driver, brightness and display on follow once the
 * panel's memory is black. */
static const co5300_lcd_init_cmd_t s_lcd_init[] = {
#if CONFIG_MUSE_BOARD_SEEED_XIAO_AMOLED_175
    /* The StopWatch's (M5GFX Panel_StopWatch), less the tearing line. */
    { 0x11, NULL, 0, 150 },                     /* sleep out */
    { 0xC4, (uint8_t[]){ 0x80 }, 1, 0 },
    { 0x53, (uint8_t[]){ 0x20 }, 1, 0 },        /* brightness control on */
#elif CONFIG_MUSE_BOARD_SEEED_XIAO_AMOLED_216
    /* Waveshare's 2.16 BSP, less its tearing line (not fitted here), window
     * and MADCTL (the driver's), brightness and display on (panel_setup). */
    { 0x11, NULL, 0, 120 },                     /* sleep out */
    { 0xFE, (uint8_t[]){ 0x20 }, 1, 0 },
    { 0x19, (uint8_t[]){ 0x10 }, 1, 0 },
    { 0x1C, (uint8_t[]){ 0xA0 }, 1, 0 },
    { 0xFE, (uint8_t[]){ 0x00 }, 1, 0 },
    { 0xC4, (uint8_t[]){ 0x80 }, 1, 0 },
    { 0x53, (uint8_t[]){ 0x20 }, 1, 0 },        /* brightness control on */
    { 0x63, (uint8_t[]){ 0xFF }, 1, 0 },
#else
    /* Waveshare's 368x448. */
    { 0xFE, (uint8_t[]){ 0x00 }, 1, 0 },
    { 0xC4, (uint8_t[]){ 0x80 }, 1, 0 },
    { 0x53, (uint8_t[]){ 0x20 }, 1, 0 },        /* brightness control on */
    { 0x63, (uint8_t[]){ 0xFF }, 1, 0 },
    { 0x11, NULL, 0, 120 },                     /* sleep out */
#endif
};

/* A CO5300 command over QSPI. */
static void lcd_cmd(uint8_t cmd, const void *param, size_t n)
{
    esp_lcd_panel_io_tx_param(s_io, (0x02 << 24) | (cmd << 8), param, n);
}

/*
 * At boot, and powered again after a pause: the panel has lost its setup and
 * what it showed. A software reset (its reset line is the expander's), the
 * init, then black over its random memory before the display goes on. Then
 * the last brightness and sleep state the UI set. After registration this
 * runs on the send task between bands, so the chunk buffers are idle.
 */
static void panel_setup(void *arg)
{
    (void)arg;
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    uint8_t *black = heap_caps_calloc(1, LCD_CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (black) {
        for (int y = 0; y < LCD_V_RES; y += 8) {
            esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + 8 < LCD_V_RES ? y + 8 : LCD_V_RES, black);
        }
    }
    lcd_cmd(0x51, &s_lcd_level, 1);             /* waits for the queued draws first */
    esp_lcd_panel_disp_on_off(s_panel, true);
    free(black);
    if (s_lcd_asleep) {
        lcd_cmd(0x10, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    s_lcd_off = false;
}

/* The CO5300 needs even-aligned update windows. */
static void round_area(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
}

/* LVGL polls this from its own task. */
static void tp_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    int x, y;
    if (!tp_point(&x, &y)) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    x = x < LCD_H_RES ? x : LCD_H_RES - 1;
    y = y < LCD_V_RES ? y : LCD_V_RES - 1;
    /* The panel is turned (display_start); the touch layer isn't. */
#if LCD_ROTATION == 90
    data->point.x = y;
    data->point.y = LCD_H_RES - 1 - x;
#elif LCD_ROTATION == 180
    data->point.x = LCD_H_RES - 1 - x;
    data->point.y = LCD_V_RES - 1 - y;
#else
    data->point.x = x;
    data->point.y = y;
#endif
    data->state = LV_INDEV_STATE_PRESSED;
}

static lv_display_t *display_start(lv_indev_t **touch)
{
    const spi_bus_config_t bus =
        CO5300_PANEL_BUS_QSPI_CONFIG(LCD_SCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3, LCD_CHUNK_BYTES);
    if (spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        return NULL;
    }
    const esp_lcd_panel_io_spi_config_t io_cfg = CO5300_PANEL_IO_QSPI_CONFIG(LCD_CS, NULL, NULL);
    if (esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &s_io) != ESP_OK) {
        return NULL;
    }
    co5300_vendor_config_t vendor_cfg = {
        .init_cmds = s_lcd_init,
        .init_cmds_size = sizeof(s_lcd_init) / sizeof(s_lcd_init[0]),
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_NC,      /* the expander's P00, let go in init() */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_cfg,
    };
    if (esp_lcd_new_panel_co5300(s_io, &panel_cfg, &s_panel) != ESP_OK) {
        return NULL;
    }
    esp_lcd_panel_set_gap(s_panel, LCD_GAP_X, 0);
    /* The driver keeps MADCTL and sends it on every init. A quarter turn
     * clockwise is rows and columns exchanged with X mirrored; Waveshare's
     * 2.16 BSP, a quarter turn the other way, has Y mirrored instead. */
#if LCD_ROTATION == 90
    esp_lcd_panel_swap_xy(s_panel, true);
    esp_lcd_panel_mirror(s_panel, true, false);
#elif LCD_ROTATION == 180
    esp_lcd_panel_mirror(s_panel, true, true);
#endif
    panel_setup(NULL);

    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_cfg.task_core_id = MUSE_UI_CORE;
    adapter_cfg.task_priority = MUSE_UI_PRIORITY;
    if (esp_lv_adapter_init(&adapter_cfg) != ESP_OK) {
        return NULL;
    }
    const esp_lv_adapter_display_config_t disp_cfg = {
        .panel = s_panel,
        .panel_io = s_io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = ESP_LV_ADAPTER_ROTATE_0,
            .hor_res = LCD_H_RES,
            .ver_res = LCD_V_RES,
        },
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
    };
    lv_display_t *disp = muse_lcd_bands_register(disp_cfg, DRAW_BUF_LINES, LCD_CHUNK_BYTES);
    if (!disp) {
        return NULL;
    }
    lv_display_add_event_cb(disp, round_area, LV_EVENT_INVALIDATE_AREA, NULL);

    tp_reset();
    *touch = lv_indev_create();
    if (!*touch) {
        return NULL;
    }
    lv_indev_set_type(*touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(*touch, disp);
    lv_indev_set_read_cb(*touch, tp_read);
    if (esp_lv_adapter_start() != ESP_OK) {
        return NULL;
    }
    return disp;
}

static bool display_lock(int timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms) == ESP_OK;
}

/* CO5300 "write display brightness" (0x51), kept for the next power-up. */
static void send_brightness(void *level)
{
    s_lcd_level = *(uint8_t *)level;
    if (!s_lcd_off) {
        lcd_cmd(0x51, &s_lcd_level, 1);
    }
}

static void set_brightness(int pct)
{
    uint8_t level = (uint8_t)(pct * 255 / 100);
    muse_lcd_bands_run(send_brightness, &level);
}

static void send_sleep(void *sleep)
{
    s_lcd_asleep = *(bool *)sleep;
    if (!s_lcd_off) {
        lcd_cmd(s_lcd_asleep ? 0x10 : 0x11, NULL, 0);
    }
}

static void panel_sleep(bool sleep)
{
    muse_lcd_bands_run(send_sleep, &sleep);
    vTaskDelay(pdMS_TO_TICKS(120));   /* settle before the next command */
}

/* On the send task: no command reaches the panel from here until panel_setup(). */
static void panel_off(void *arg)
{
    (void)arg;
    if (!s_lcd_asleep) {
        lcd_cmd(0x10, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    s_lcd_off = true;
}

/*
 * The panel's VBAT and VCI switch off while paused; IOVCC and the touch stay
 * on VDD_3V3, so the QSPI lines can stay as they are. Off: reset held, then
 * VBAT, then VCI (the power-on order reversed). On: as init() does, then the
 * panel set up again with the brightness the UI last set.
 */
static void lcd_power(bool on)
{
    if (!on) {
        muse_lcd_bands_run(panel_off, NULL);
        exp_set(EXP_LCD_RST, false);
        exp_set(EXP_LCD_VBAT | EXP_LCD_VCI_EN, false);
        exp_set(EXP_LCD_VCI, false);
        ESP_LOGI(TAG, "panel rails off");
        return;
    }
    exp_set(EXP_LCD_VCI, true);
    vTaskDelay(pdMS_TO_TICKS(5));
    exp_set(EXP_LCD_VBAT | EXP_LCD_VCI_EN, true);
    vTaskDelay(pdMS_TO_TICKS(LCD_POWER_UP_MS));
    exp_set(EXP_LCD_RST, true);
    vTaskDelay(pdMS_TO_TICKS(LCD_RESET_MS));
    muse_lcd_bands_run(panel_setup, NULL);
    ESP_LOGI(TAG, "panel rails on, panel set up");
}

/*
 * Screen off: LVGL stops, the CST820 sleeps until its reset line wakes it, and
 * the panel loses its rails. LVGL redraws the whole screen on resume.
 */
static void display_pause(bool pause)
{
    if (pause) {
        s_waiter = xTaskGetCurrentTaskHandle();   /* the input task, before INT can fire */
        esp_lv_adapter_pause(-1);
        tp_sleep();
        lcd_power(false);
    } else {
        lcd_power(true);
        tp_reset();
        esp_lv_adapter_resume();
        if (esp_lv_adapter_lock(-1) == ESP_OK) {
            lv_obj_invalidate(lv_screen_active());
            esp_lv_adapter_unlock();
        }
    }
}

/* The NS4150B runs from VSYS; off, it draws nothing while the codecs rest. */
static void audio_power(bool on)
{
    exp_set(EXP_PA_EN, on);
    if (on) {
        vTaskDelay(pdMS_TO_TICKS(AMP_ON_MS));
    }
}

/* ES8311 plays and a separate ES7243E records, on one duplex I2S bus with MCLK. */
static esp_err_t audio_init(esp_codec_dev_handle_t *spk, esp_codec_dev_handle_t *mic)
{
    i2s_chan_handle_t tx, rx;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx, &rx), TAG, "i2s channel");
    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws = I2S_WS,
            .dout = I2S_DOUT,
            .din = I2S_DIN,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &std_cfg), TAG, "i2s tx");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &std_cfg), TAG, "i2s rx");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx), TAG, "i2s tx on");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx), TAG, "i2s rx on");

    audio_codec_i2s_cfg_t i2s_cfg = { .port = I2S_NUM_0, .rx_handle = rx, .tx_handle = tx };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    audio_codec_i2c_cfg_t dac_i2c = { .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = s_i2c };
    const audio_codec_ctrl_if_t *dac_ctrl = audio_codec_new_i2c_ctrl(&dac_i2c);
    audio_codec_i2c_cfg_t adc_i2c = { .port = I2C_NUM_0, .addr = ES7243E_ADDR << 1, .bus_handle = s_i2c };
    const audio_codec_ctrl_if_t *adc_ctrl = audio_codec_new_i2c_ctrl(&adc_i2c);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(data_if && dac_ctrl && adc_ctrl && gpio_if, ESP_ERR_NO_MEM, TAG, "codec interfaces");

    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = dac_ctrl,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = GPIO_NUM_NC,     /* the amp is on the expander (audio_power) */
        .use_mclk = true,
        .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
    };
    const audio_codec_if_t *dac = es8311_codec_new(&es_cfg);
    ESP_RETURN_ON_FALSE(dac, ESP_FAIL, TAG, "ES8311 not responding");
    es7243e_codec_cfg_t adc_cfg = { .ctrl_if = adc_ctrl };
    const audio_codec_if_t *adc = es7243e_codec_new(&adc_cfg);
    ESP_RETURN_ON_FALSE(adc, ESP_FAIL, TAG, "ES7243E not responding");

    esp_codec_dev_cfg_t out_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = dac, .data_if = data_if };
    esp_codec_dev_cfg_t in_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = adc, .data_if = data_if };
    *spk = esp_codec_dev_new(&out_cfg);
    *mic = esp_codec_dev_new(&in_cfg);
    return *spk && *mic ? ESP_OK : ESP_FAIL;
}

/* ENTER talks and BACK is aux, both read through the expander and debounced
 * like muse_gpio_button_poll(). */
static unsigned poll_buttons(void)
{
    uint16_t in;
    if (exp_read(&in) != ESP_OK) {
        return 0;
    }
    uint16_t raw = ~in & (EXP_KEY_ENTER | EXP_KEY_BACK);
    if (raw == s_keys) {
        s_key_stable = 0;
        return 0;
    }
    /* Asleep, a press is what woke the input task: it counts on its first sample. */
    if (++s_key_stable < DEBOUNCE_SAMPLES && !(raw && muse_state_asleep())) {
        return 0;
    }
    s_key_stable = 0;
    uint16_t changed = raw ^ s_keys;
    s_keys = raw;
    unsigned ev = 0;
    if (changed & EXP_KEY_ENTER) {
        ev |= raw & EXP_KEY_ENTER ? MUSE_BTN_TALK_PRESS : MUSE_BTN_TALK_RELEASE;
    }
    if (changed & EXP_KEY_BACK) {
        ev |= raw & EXP_KEY_BACK ? MUSE_BTN_AUX_PRESS : MUSE_BTN_AUX_RELEASE;
    }
    return ev;
}

/*
 * Display paused: sleeps until an expander input changes (a key, SD detect,
 * the IMU's lines). poll_buttons() reads the inputs before each wait, which
 * lets INT go high again. If it's low already, an input changed since then:
 * a noisy one can't make this poll faster than it did without the wait.
 */
static void wait_buttons(int timeout_ms)
{
    if (s_key_stable) {
        vTaskDelay(pdMS_TO_TICKS(WAIT_DEBOUNCE_MS));
        return;
    }
    if (!gpio_get_level(EXP_INT)) {
        vTaskDelay(pdMS_TO_TICKS(WAIT_BUSY_MS));
        return;
    }
    s_waiter = xTaskGetCurrentTaskHandle();
    gpio_wakeup_enable(EXP_INT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    gpio_intr_enable(EXP_INT);
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms));
    gpio_intr_disable(EXP_INT);
    gpio_wakeup_disable(EXP_INT);
}

/*
 * The battery through its 10K/10K divider, switched in only to read it. The
 * XIAO charges the cell and tells nothing about it, so USB is only known when
 * a host enumerates the chip's own USB; on a plain charger the cell reads as
 * on battery, near full.
 */
static esp_err_t read_power(muse_power_t *out)
{
    out->usb = usb_serial_jtag_is_connected();
    out->charging = false;
    out->battery_pct = -1;
    out->battery_mv = 0;
    ESP_RETURN_ON_FALSE(s_cali, ESP_ERR_INVALID_STATE, TAG, "no ADC calibration");
    ESP_RETURN_ON_ERROR(exp_set(EXP_ADC_EN, true), TAG, "divider on");
    vTaskDelay(pdMS_TO_TICKS(BATT_SETTLE_MS));
    int sum = 0;
    esp_err_t err = ESP_OK;
    for (int i = 0; i < 8 && err == ESP_OK; i++) {
        int raw, mv = 0;
        err = adc_oneshot_read(s_adc, BATT_ADC, &raw);
        if (err == ESP_OK) {
            err = adc_cali_raw_to_voltage(s_cali, raw, &mv);
        }
        sum += mv;
    }
    exp_set(EXP_ADC_EN, false);
    ESP_RETURN_ON_ERROR(err, TAG, "adc read");
    int v = sum / 8 * 2;
    out->battery_mv = v;
    if (v < 2500) {
        return ESP_OK;   /* no cell */
    }
    /* The Watcher's LiPo curve. */
    int pct = (-v * v + 9016 * v - 19189000) / 10000;
    out->battery_pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    return ESP_OK;
}

/*
 * No power latch: the slide switch cuts the battery. Off here is the panel's
 * rails, the amp and the divider off, then deep sleep until ENTER is pressed
 * (the expander's INT, checked again in init()).
 */
static esp_err_t power_off(void)
{
    set_brightness(0);
    lcd_power(false);
    ESP_RETURN_ON_ERROR(exp_set(EXP_PA_EN | EXP_ADC_EN, false), TAG, "rails off");
    tp_sleep();
    uint16_t in;
    while (exp_read(&in) == ESP_OK && !(in & EXP_KEY_ENTER)) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    sleep_until_enter();
    return ESP_FAIL;
}

static const muse_board_t s_board = {
    .name = LCD_NAME,
    .width = LCD_H_RES,
    .height = LCD_V_RES,
    .round = LCD_ROUND,
    .touch = true,
    .diagonal_in = LCD_DIAGONAL,
    .talk_button = "enter",
    .aux_button = "back",
    .frame_ms = 40,
    .init = init,
    .display_start = display_start,
    .display_lock = display_lock,
    .display_unlock = esp_lv_adapter_unlock,
    .set_brightness = set_brightness,
    .panel_sleep = panel_sleep,
    .display_pause = display_pause,
    .audio_init = audio_init,
    .audio_power = audio_power,
    .mic_slot = 1,              /* the mic on the ES7243E's right input */
    .poll_buttons = poll_buttons,
    .wait_buttons = wait_buttons,
    .read_power = read_power,
    .power_off = power_off,
};

/* Home Link's app_main starts Muse with this board (main/main.c). */
const muse_board_t *muse_board_get(void)
{
    return &s_board;
}
