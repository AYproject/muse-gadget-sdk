# Seeed XIAO ESP32-S3 1.47 Touch Display

## Product overview

The [Seeed Studio XIAO 1.47" IPS Touch Display (ESP32-S3)](https://www.seeedstudio.com/1-47-Inch-Touch-Display-Powered-by-XIAO-ESP32-S3-Plus-p-6996.html)
combines a XIAO ESP32-S3 Plus with a compact display expansion board. Its
172×320 color IPS screen, capacitive touch, onboard PDM microphone and two
user buttons provide a small interface for a Muse companion, desktop
dashboard or interactive prototype.

<img src="https://files.seeedstudio.com/wiki/Display_Gadgets/imgs/147_ESP32S3Plus_display_hardware_heroNEW.jpg" width="480" alt="Seeed Studio XIAO 1.47-inch IPS touch display powered by XIAO ESP32-S3 Plus">

| Hardware | Specification |
|---|---|
| Controller | Dual-core ESP32-S3, up to 240 MHz |
| Memory | 16 MB flash, 8 MB octal PSRAM |
| Display | 1.47-inch 172×320 IPS LCD, JD9853A, SPI |
| Input | Capacitive touch, PDM microphone, USR1 and USR2 |
| Wireless | 2.4 GHz Wi-Fi and Bluetooth LE |
| Expansion | Exposed I2S pins for an external amplifier |
| Board size | 26.4 × 51.4 × 12.6 mm |

The hardware also includes an LSM6DS3 IMU, MicroSD slot and battery connector
with voltage measurement. This firmware does not yet integrate those
peripherals. See the [official hardware guide](https://wiki.seeedstudio.com/getting_started_1.47_inch_touch_display_esp32s3/)
for the complete board specification.

## Muse support

This profile targets the ESP32-S3 version and provides the
Muse avatar and settings, BLE pairing, Wi-Fi, USR1 push-to-talk, Chinese
captions, touch input and USR2 screen sleep/wake.

The board has a PDM microphone but no built-in speaker. An external MAX98357A
can play the SDK's sound effects and test audio. Replies follow the upstream
SDK's text path; connecting an amplifier does not add a speech service.
SD, IMU, battery telemetry and measured low-power operation are not implemented.
OTA is disabled for this profile; update over USB.

## References and pins

- [Seeed board guide](https://wiki.seeedstudio.com/getting_started_1.47_inch_touch_display_esp32s3/)
- [XIAO ESP32-S3 Plus pin map](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
- [Seeed peripheral examples](https://wiki.seeedstudio.com/function_1.47_inch_touch_display_esp32s3/)
- [Seeed_GFX2](https://github.com/Seeed-Studio/Seeed_GFX2): JD9853A register
  sequence, AXS5106L reports and the `0519_DashBoard_147_ESP32` PDM example.
  The adapted panel sequence retains Seeed's [MIT notice](licenses/Seeed_GFX2.txt).

| Function | XIAO pin | GPIO |
|---|---|---|
| PDM clock / data | D0 / D1 | 1 / 2 |
| MAX98357A DIN / BCLK / LRC | D11 / D12 / D13 | 38 / 39 / 40 |
| LCD CS / DC | D2 / D3 | 3 / 4 |
| LCD SCK / MOSI | D8 / D10 | 7 / 9 |
| LCD and touch reset / backlight | D17 / D18 | 13 / 12 |
| Touch SDA / SCL / INT | D4 / D5 / D7 | 5 / 6 / 44 |
| USR1 talk / USR2 screen | D19 / D15 | 11 / 42 |
| Unused SD chip select, held high | D6 | 43 |

The JD9853A uses the ST7789-compatible driver with the vendor register
sequence, MADCTL `0x48` (MX+BGR), X offset 34, Y offset 0 and inversion off.
Touch X is mirrored to match. LCD and touch share reset, asserted once before
either starts. The touch driver must not reset the running panel.

The PDM mic uses I2S0. Independent I2S1 playback uses 16 kHz, 16-bit stereo
Philips I2S, with mono audio duplicated to both channels. TX DMA holds 30 ms
of audio and clears to silence while idle. The microphone removes PDM DC
offset before applying gain.

### Optional MAX98357A

| XIAO | MAX98357A |
|---|---|
| D11 / GPIO38 | DIN |
| D12 / GPIO39 | BCLK |
| D13 / GPIO40 | LRC / LRCLK |
| GND | GND |
| 5V | VIN |

Connect the speaker across SPK+ and SPK−; do not connect SPK− to ground. The
amplifier needs neither MCLK nor I2C. Keep its SD/EN pin enabled according to
the module's wiring. See the [MAX98357A datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf).

The profile defaults to 40% volume. Existing NVS volume and Speaker settings
take precedence: enable Speaker and adjust volume in Settings if needed.

## Build and flash

Use ESP-IDF **v6.0.1**. From `esp32/`, set your own SDK token in the build's
configuration before building; do not commit it or share a firmware image
containing it.

```sh
idf.py -B build-muse-seeed-xiao-s3-147 -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG=build-muse-seeed-xiao-s3-147/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-seeed-xiao-s3-147" menuconfig
# ESP32 Device SDK > Muse Gadgets SDK token
tools/muse/board.sh build xiao147
tools/muse/board.sh flash xiao147
```

With multiple native-USB boards connected, pass the target's serial number
or port to the flash helper. Native USB is `303a:1001`; select the connected
device rather than assuming a fixed COM number.

In an ESP-IDF PowerShell terminal, use the same `idf.py` arguments on one
line, replace `menuconfig` with `build`, then flash with
`idf.py -B build-muse-seeed-xiao-s3-147 -p PORT flash`.

The primary console is USB Serial/JTAG: UART0 GPIO43/44 overlap SD CS and
touch IRQ. If flashing leaves the board in download mode, press RESET
without holding BOOT. Back up factory firmware before the first flash;
keep backups and private pairing/network configuration out of Git.

## Pair and use

1. In the Muse app enable **Settings > Devices > Developer mode** and use
   **Add Device** to select the `MuseGadget-XXXXXX` matching the screen.
2. Press **USR1** to confirm pairing, then configure a reachable 2.4 GHz Wi-Fi.
3. Hold USR1 while speaking and release to send. Check transcription and
   the text reply on the display.
4. Press **USR2** to turn the screen off and press again to wake it. Touch
   opens the shared settings UI.

## Validation

On a real 16 MB / 8 MB XIAO board, the driver has been exercised for normal
boot, avatar/display, BLE pairing, Wi-Fi, PDM recordings and repeated Muse
transcription/text replies, touch settings and USR2 screen sleep/wake.
With the MAX98357A wired as above, startup effects and test tones were
audible. Boot and dialogue captures showed no panic or reboot loop.

Host checks exercise real AXS5106L report decoding (bounds, mirror, release
and malformed input), speaker callbacks through the actual `esp_codec_dev`
open/write/close/reopen lifecycle (including partial writes), and USB port
identity selection. Run them after fetching managed components:

```sh
python -m unittest discover -s tests -p 'test_xiao_147.py' -v
```

Four-corner touch calibration, deep-sleep wake, long-duration stability and
power consumption have not been measured. The software power-off callback
enters deep sleep with USR1/GPIO11 as wake; USR2/GPIO42 is not an RTC wake
source. Speaker playback does not establish native Muse voice-reply support.
