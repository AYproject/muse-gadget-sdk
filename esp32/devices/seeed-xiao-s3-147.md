# Seeed XIAO ESP32-S3 1.47 Touch Display — Muse Demo

[中文 README](seeed-xiao-s3-147.zh-CN.md)

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

The board has a PDM microphone but no built-in speaker. Connect an external
MAX98357A for sound effects and spoken text replies. The optional Volcengine
Agent Plan backend synthesizes completed Muse text replies and streams MP3
into the speaker. Each user supplies their own service key over USB.
Without a configured TTS key, Muse text replies remain available.

The 172×320 settings layout keeps names and values on separate lines, uses
single-line ellipses for long values, and reserves space for page indicators.
The Sound page includes speaker, volume, microphone gain and brightness
controls; microphone level and its numeric reading occupy separate lines.

Native Muse audio-only replies remain experimental: phone playback has been
observed while device playback still failed. They are not a verified feature
of this Demo. The default mode asks Muse for text so the configured TTS
backend can speak it. This is push-to-talk with streamed speech output,
not a full-duplex voice assistant.
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
| 3V3 | VIN |

Connect the speaker across SPK+ and SPK−; do not connect SPK− to ground. The
amplifier needs neither MCLK nor I2C. Keep its SD/EN pin enabled according to
the module's wiring. See the [MAX98357A datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf).

The profile defaults to 40% volume. Existing NVS volume and Speaker settings
take precedence: enable Speaker and adjust volume in Settings if needed.

## Credentials and configuration interfaces

| Setting | User configuration | Storage and behavior |
|---|---|---|
| Muse SDK token | [Your SDK token](https://gadgets.muse.ai/settings/sdk-tokens); `menuconfig`, or Windows `-Action Configure` | Compiled into your local firmware; never distribute that image with a personal token |
| Muse account / device pairing | Muse app → Settings → Devices → Developer mode | BLE provisioning, saved on the device |
| Wi-Fi | Muse app pairing, or `xiao147_wifi.py --config` | Saved on the device; local JSON is ignored by Git |
| TTS API key | Your Agent Plan key in local `.tts-config.json`, provisioned with `xiao147_tts.py` | Saved in the device's `muse_tts` NVS namespace; not compiled into firmware |
| TTS voice | `speaker` in the same local configuration | Saved in NVS; default `zh_female_vv_uranus_bigtts` |
| TTS endpoint | Public constant `MUSE_TTS_PLAN_URL` in `muse_tts_protocol.h` | Pinned to the supported Agent Plan endpoint; arbitrary service URLs are rejected |

The repository includes only empty [TTS](../../.tts-config.example.json) and
[Wi-Fi](../../.wifi-config.example.json) examples. `.sdk-token`,
`.tts-config.json`, `.wifi-config.json`, build directories, `.tools/` and
`backups/` must stay local. Neither real credentials nor local firmware,
flash backups, pairing data or dialogue logs belong in a PR.

SDK tokens and TTS keys are different credentials. An SDK token enables Muse
pairing; it does not authorize Agent Plan speech synthesis. The TTS key is
sent only to the pinned Volcengine HTTPS endpoint, never to Muse media URLs.
Status reports whether TTS is configured and does not return the key.
Device NVS is not encrypted by this profile: treat physical flash backups
as sensitive. Do not include real credentials in screenshots or issue reports.

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

On Windows, activate an **ESP-IDF v6.0.1 PowerShell terminal**, then run from
the repository root (replace `COMx` with the port on your computer):

```powershell
.\esp32\tools\muse\xiao147.ps1 -Action Configure
.\esp32\tools\muse\xiao147.ps1 -Action Build
.\esp32\tools\muse\xiao147.ps1 -Action Flash -Port COMx
```

`Configure` accepts a hidden SDK token and saves it to ignored `.sdk-token`.
The helper uses your activated IDF installation; it does not install tools,
set up a VPN or depend on this contributor's computer paths. Alternatively,
use the `idf.py` commands above on one line.

The primary console is USB Serial/JTAG: UART0 GPIO43/44 overlap SD CS and
touch IRQ. If flashing leaves the board in download mode, press RESET
without holding BOOT. Back up factory firmware before the first flash;
keep backups and private pairing/network configuration out of Git.

## Optional cloud speech output

This backend implements the Agent Plan **HTTP streaming output** interface:

```text
POST https://openspeech.bytedance.com/api/v3/plan/tts/unidirectional
X-Api-Key: <supplied locally; never committed>
X-Api-Resource-Id: seed-tts-2.0
```

It reads NDJSON/SSE audio frames, decodes the MP3 stream, and plays it on the
MAX98357A. It does not use the bidirectional WebSocket interface or a second
conversation model. Muse still generates the reply. Only the text selected
for this device's speech output is submitted to the TTS provider, using your
own service entitlement and quota. See the [Agent Plan voice configuration
guide](https://docs.volcengine.com/docs/ark/agent-plan-personal-voice-model?lang=zh).

1. Copy `.tts-config.example.json` at the repository root to `.tts-config.json`.
2. Edit only the local file: fill your **Agent Plan dedicated API key** into
   `api_key`, and choose a voice supported by your service in `speaker`.
   Keep the supplied `/plan/` endpoint. The example's key is intentionally empty.
3. With the board running, provision it from the repository root:

   ```sh
   python esp32/tools/muse/xiao147_tts.py --port PORT --config .tts-config.json
   ```

   On Windows, the equivalent is:

   ```powershell
   .\esp32\tools\muse\xiao147.ps1 -Action TTS -Port COMx
   ```

4. Enable **Speaker** under Sound, set the volume, hold USR1 to ask a question
   and release. A text reply should appear and be spoken. Press USR1 during
   playback to interrupt and start another turn.

Provisioning checks the board name, TTS capability and each acknowledgement
before sending subsequent commands. It keeps Wi-Fi and Muse pairing.
Without a configured key, with Speaker off, or if synthesis fails, captions
remain the fallback. Disabling `CONFIG_MUSE_VOLC_TTS` in `menuconfig` removes
this backend. TTS keys can be replaced by editing the local JSON and running
provisioning again; clearing the local file alone does not clear device NVS.

For developers, `muse_tts_begin/read/state/cancel` isolate speech transport
from the session decoder. `muse_tts_protocol.c` validates streaming frames,
UTF-8 boundaries and configuration values. Adding another provider requires
an explicit backend implementation with its own authentication and endpoint;
this Demo does not advertise generic OpenAI-compatible TTS support.
Native audio envelopes are parsed by `muse_reply_media.c`, but that path is
experimental and is not needed for the verified text-to-speech flow.

## Pair and use

1. In the Muse app enable **Settings > Devices > Developer mode** and use
   **Add Device** to select the `MuseGadget-XXXXXX` matching the screen.
2. Press **USR1** to confirm pairing, then configure a reachable 2.4 GHz Wi-Fi.
3. Hold USR1 while speaking and release to send. Check transcription and
   the text reply on the display. With TTS configured and Speaker enabled,
   the amplifier also plays the reply.
4. Press **USR2** to turn the screen off and press again to wake it. Touch
   opens the settings UI. Swipe vertically to scroll and right to return.
5. When changing Muse accounts, configure the new SDK token, rebuild and
   flash, then reset the old pairing in Settings → Muse → Reset pairing if
   required and pair through the new account. There is no need to erase all
   flash. TTS has a separate credential and is configured independently.

## Troubleshooting

- Text replies but no speech: first confirm the amplifier wiring and test
  sound, then check Speaker, volume and that TTS status says configured.
  Check the provider's entitlement/quota and its HTTP status; never paste a key
  or a complete local configuration into a bug report.
- A voice message plays only in the phone app: native Muse audio-only replies
  remain experimental; use the default text-reply/TTS mode.
- Pairing works but sending fails: the board's network must reach both Muse
  and the TTS endpoint. Phone app connectivity alone does not validate the
  board's route. Computer hotspot/VPN setup is environment-specific and is
  not installed or modified by this Demo.
- A serial tool cannot open the port: close other serial monitors, verify
  the selected port, and reconnect USB if the Windows driver needs recovery.
