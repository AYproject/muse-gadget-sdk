# Seeed XIAO ESP32-S3 1.47 触摸屏 — Muse Demo

[English README](seeed-xiao-s3-147.md)

## 产品介绍

[Seeed Studio XIAO 1.47 英寸 IPS 触摸屏（ESP32-S3）](https://www.seeedstudio.com/1-47-Inch-Touch-Display-Powered-by-XIAO-ESP32-S3-Plus-p-6996.html)
将 XIAO ESP32-S3 Plus 与小型显示扩展板集成在一起，提供 172×320 彩色 IPS 屏幕、电容触控、板载 PDM 麦克风和两个用户按键。
它适合制作小型 Muse 交互终端、桌面信息面板和便携式交互原型，无需另接显示屏和麦克风。

<img src="https://files.seeedstudio.com/wiki/Display_Gadgets/imgs/147_ESP32S3Plus_display_hardware_heroNEW.jpg" width="480" alt="Seeed Studio XIAO ESP32-S3 Plus 1.47 英寸 IPS 触摸屏">

| 硬件 | 规格 |
|---|---|
| 主控 | XIAO ESP32-S3 Plus，双核 ESP32-S3，最高 240 MHz |
| 存储 | 16 MB Flash、8 MB 八线 PSRAM |
| 显示 | 1.47 英寸、172×320 IPS LCD，JD9853A，SPI 接口 |
| 输入 | 电容触控、板载 PDM 麦克风、USR1 和 USR2 按键 |
| 无线 | 2.4 GHz Wi-Fi、Bluetooth LE |
| 音频扩展 | 引出的 I2S 引脚，可连接外置功放 |
| 尺寸 | 26.4 × 51.4 × 12.6 mm |

硬件还带有 LSM6DS3 六轴 IMU、MicroSD 卡槽，以及支持电池电压检测的电池接口。
本 Demo 尚未接入这些外设；硬件完整规格请参阅 [Seeed 官方入门指南](https://wiki.seeedstudio.com/getting_started_1.47_inch_touch_display_esp32s3/)。

## Demo 能做什么

本适配面向 **ESP32-S3 版本**，支持 Muse 头像、设置界面、BLE 配对、Wi-Fi、USR1 按住说话、中文回复字幕、电容触控，以及 USR2 息屏和唤醒。

开发板有麦克风，**没有内置扬声器**。连接 MAX98357A 和喇叭后，可播放提示音、测试音；配置可选的火山引擎 Agent Plan TTS 后，可将 Muse 的文字回复合成为语音并播出。
每位用户使用自己的 TTS Key，通过 USB 在本地配置；未配置 TTS Key 时，仍可使用文字回复。

针对 172×320 窄屏，设置页将名称和状态上下排列，过长状态用省略号显示，并为底部分页圆点预留空间。
声音页包含扬声器开关、音量、麦克风增益和亮度控件；麦克风电平名称与数值也分行显示，避免文字挤压。

**Muse 原生纯语音回复仍属于实验功能。** 测试中出现过手机能播放、开发板仍无声的情况，因此不将其列为已验证能力。
默认模式会请求 Muse 返回文字，再由已配置的 TTS 服务播报。这是“按住说话＋流式语音输出”，不是全双工语音助手。

IMU、MicroSD、电池遥测和经测量验证的低功耗功能尚未实现。本配置关闭 OTA，使用 USB 更新固件。

## 接线与参考资料

- [Seeed 官方入门指南](https://wiki.seeedstudio.com/getting_started_1.47_inch_touch_display_esp32s3/)
- [XIAO ESP32-S3 Plus 引脚说明](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
- [1.47 英寸扩展板功能示例](https://wiki.seeedstudio.com/function_1.47_inch_touch_display_esp32s3/)
- [Seeed_GFX2](https://github.com/Seeed-Studio/Seeed_GFX2)：屏幕初始化、AXS5106L 触控报告及 PDM 示例的参考来源。
  适配的屏幕初始化序列保留了 [MIT 许可声明](licenses/Seeed_GFX2.txt)。

| 功能 | XIAO 引脚 | GPIO |
|---|---|---|
| PDM 时钟／数据 | D0／D1 | 1／2 |
| MAX98357A DIN／BCLK／LRC | D11／D12／D13 | 38／39／40 |
| LCD CS／DC | D2／D3 | 3／4 |
| LCD SCK／MOSI | D8／D10 | 7／9 |
| LCD 与触控复位／背光 | D17／D18 | 13／12 |
| 触控 SDA／SCL／INT | D4／D5／D7 | 5／6／44 |
| USR1 说话／USR2 息屏 | D19／D15 | 11／42 |
| 暂未使用的 SD 片选，保持高电平 | D6 | 43 |

JD9853A 使用 ST7789 兼容驱动和厂商初始化序列，MADCTL 为 `0x48`（MX+BGR），X 偏移 34，Y 偏移 0，关闭反色。
触控 X 坐标做对应镜像。LCD 和触控共用复位信号，在两者启动前统一复位，避免触控驱动再次复位已运行的屏幕。

PDM 麦克风使用 I2S0；扬声器使用独立的 I2S1，以 16 kHz、16 位双声道 Philips I2S 输出，将单声道音频复制到左右声道。
TX DMA 缓冲约 30 ms 音频，空闲时输出静音；麦克风在应用增益前去除 PDM 直流偏置。

### 可选 MAX98357A 与喇叭

| XIAO | MAX98357A |
|---|---|
| D11／GPIO38 | DIN |
| D12／GPIO39 | BCLK |
| D13／GPIO40 | LRC／LRCLK |
| GND | GND |
| 3V3 | VIN |

喇叭连接在 SPK+ 与 SPK− 之间，**不要把 SPK− 接到地**。MAX98357A 不需要 MCLK 或 I2C。
请按具体模块的接线要求使 SD/EN 保持使能；参考 [MAX98357A 数据手册](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf)。

配置默认音量为 40%。设备 NVS 中已保存的音量和扬声器设置优先；已有设备可在设置中开启 Speaker 并调整音量。

## 账号、Token 与 Key 的配置入口

| 配置项 | 用户填写入口 | 保存位置与行为 |
|---|---|---|
| Muse SDK Token | [用户自己的 SDK Token](https://gadgets.muse.ai/settings/sdk-tokens)；通过 `menuconfig` 或 Windows `-Action Configure` 设置 | 编译进本地固件；不要分发带个人 Token 的固件 |
| Muse 账号与设备配对 | Muse App → Settings → Devices → Developer mode | 经 BLE 配对，下发后保存在设备中 |
| Wi-Fi | App 配对流程，或 `xiao147_wifi.py --config` | 保存在设备中，本地 JSON 被 Git 忽略 |
| TTS API Key | 用户自己的 Agent Plan Key，填写到本地 `.tts-config.json` 后通过 USB 配置 | 保存在设备 `muse_tts` NVS 命名空间，不编译进固件 |
| TTS 音色 | 同一本地配置文件的 `speaker` | 保存在 NVS，默认 `zh_female_vv_uranus_bigtts` |
| TTS 服务地址 | `muse_tts_protocol.h` 中的公开常量 `MUSE_TTS_PLAN_URL` | 固定为已实现的 Agent Plan 接口；不接受任意服务地址 |

仓库只提供 Key、Wi-Fi 名称和密码为空的 [TTS 配置示例](../../.tts-config.example.json) 与 [Wi-Fi 配置示例](../../.wifi-config.example.json)。
真实 `.sdk-token`、`.tts-config.json`、`.wifi-config.json`、构建目录、`.tools/` 和 `backups/` 均应留在本地。
真实凭据、本地编译固件、Flash 备份、配对信息和对话日志均不应进入 PR。

Muse SDK Token 与 TTS Key 是两套不同凭据：前者用于 Muse 设备接入，后者用于 Agent Plan 语音合成。
TTS Key 只发送到固定的火山引擎 HTTPS 接口，不发送到 Muse 音频附件地址；状态查询只返回是否已配置，不返回 Key。
本配置没有启用 NVS 加密，设备 Flash 备份应按敏感数据保管。截图和问题反馈中也不要包含真实凭据。

## 编译与刷写

使用 **ESP-IDF v6.0.1**。先取得自己的 Muse SDK Token，再在本地构建配置中填写。
不要将 Token 提交到 Git，也不要公开分享包含个人 Token 的固件。

### Linux／macOS

激活 ESP-IDF 环境，在 `esp32/` 目录执行：

```sh
idf.py -B build-muse-seeed-xiao-s3-147 -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG=build-muse-seeed-xiao-s3-147/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-seeed-xiao-s3-147" menuconfig
# 在 ESP32 Device SDK > Muse Gadgets SDK token 中填写自己的 Token

tools/muse/board.sh build xiao147
tools/muse/board.sh flash xiao147
```

连接多块原生 USB 开发板时，向刷写工具明确提供目标设备的串口或序列号。
此板使用原生 USB `303a:1001`，不要假定不同电脑上串口号相同。

### Windows

打开已激活的 **ESP-IDF v6.0.1 PowerShell 终端**，在仓库根目录执行：

```powershell
.\esp32\tools\muse\xiao147.ps1 -Action Configure
.\esp32\tools\muse\xiao147.ps1 -Action Build
.\esp32\tools\muse\xiao147.ps1 -Action Flash -Port COMx
```

将 `COMx` 换成当前电脑上的目标串口。`Configure` 通过隐藏输入读取自己的 Token，保存到 Git 忽略的 `.sdk-token`。
工具使用已经激活的 IDF 安装，不会安装工具链、配置 VPN，也不依赖贡献者电脑上的固定目录。
也可以把上面的 `idf.py` 参数写在同一行，分别执行配置、构建和刷写。

主控制台使用 USB Serial/JTAG，因为 UART0 默认 GPIO43／44 与 SD 片选和触控中断重叠。
刷写后若停留在下载模式，只短按 RESET，不要按 BOOT。首次刷写前备份原厂固件，备份和私人配对、网络配置不要放入 Git。

## 配置可选的云端语音播报

当前实现使用 Agent Plan 的 **HTTP 流式输出**接口：

```text
POST https://openspeech.bytedance.com/api/v3/plan/tts/unidirectional
X-Api-Key: <由用户在本地填写，不提交到仓库>
X-Api-Resource-Id: seed-tts-2.0
```

固件读取 NDJSON／SSE 音频帧，解码 MP3，再通过 MAX98357A 播放。
这里没有使用双向 WebSocket，也没有替换 Muse 的对话模型：回答仍由 Muse 生成。
只有为此设备播报而选中的回复文字会发送给 TTS 服务，使用用户自己的服务权益和额度。
服务配置参考 [Agent Plan 语音模型文档](https://docs.volcengine.com/docs/ark/agent-plan-personal-voice-model?lang=zh)。

1. 将仓库根目录 `.tts-config.example.json` 复制为 `.tts-config.json`。
2. 只编辑本地 `.tts-config.json`：把自己的 **Agent Plan 专属 API Key** 填入 `api_key`，按服务支持情况设置 `speaker` 音色。
   保留示例中的 `/plan/` 接口地址，示例 Key 有意留空。
3. 设备正常运行后，在仓库根目录执行：

   ```sh
   python esp32/tools/muse/xiao147_tts.py --port PORT --config .tts-config.json
   ```

   Windows 也可以使用：

   ```powershell
   .\esp32\tools\muse\xiao147.ps1 -Action TTS -Port COMx
   ```

4. 在 Sound 页开启 Speaker、调整音量，按住 USR1 提问后松开，等待屏幕回复和喇叭播报。
   播报时按住 USR1 可打断并开始下一轮提问。

USB 配置工具会先检查开发板名称、TTS 能力及每一步确认结果，再发送后续配置，保留已有 Wi-Fi 和 Muse 配对。
未配置 Key、关闭 Speaker 或合成失败时，仍以字幕为回退显示。
可在 `menuconfig` 中关闭 `CONFIG_MUSE_VOLC_TTS`，移除此后端。
更换 TTS Key 时修改本地 JSON 并重新执行配置；仅清空电脑上的文件不会清除设备 NVS 中的 Key。

开发者可通过 `muse_tts_begin/read/state/cancel` 对接语音传输与会话解码器。
`muse_tts_protocol.c` 负责流式帧、UTF-8 边界及配置值的校验。
增加其他 TTS 服务需要实现对应后端、认证和接口地址；本 Demo 没有宣称支持通用 OpenAI 兼容 TTS。
`muse_reply_media.c` 处理原生音频消息结构，但这条路径仍为实验功能，已验证的“文字回复 → TTS”流程不依赖它。

## 配对与日常操作

1. 在 Muse App 中开启 **Settings → Devices → Developer mode**，选择 Add Device，找到与屏幕名称对应的 `MuseGadget-XXXXXX`。
2. 按 **USR1** 确认配对，选择设备能够访问服务的 **2.4 GHz Wi-Fi**。
3. 按住 USR1 说话，松开后发送；屏幕显示识别文字与 Muse 回复。配置好 TTS 并开启 Speaker 后，喇叭同时播报。
4. 短按 **USR2** 息屏，再按一次唤醒。触控进入设置，纵向滑动滚动列表，向右滑动返回。
5. 更换 Muse 账号时，先配置新 SDK Token、重新编译刷写；如仍保留旧配对，再在 Settings → Muse → Reset pairing 重置并用新账号重新配对。
   无需擦除整片 Flash。TTS 使用独立凭据，单独配置。

## 常见问题

- **有文字、没有声音：** 先用提示音或测试音检查功放接线，再检查 Speaker、音量以及 TTS 是否已配置。
  同时检查服务权益、额度和 HTTP 状态；问题反馈中不要粘贴 Key 或完整本地配置。
- **手机收到语音，开发板不播放：** 原生纯语音回复仍属实验功能，优先使用默认的文字回复加 TTS 模式。
- **配对成功但消息发送失败：** 开发板所在网络需要能访问 Muse 和 TTS 接口；不能仅凭手机 App 能用就判断开发板出口正常。
  电脑热点、VPN 和代理配置取决于用户环境，本 Demo 不安装或修改这些设置。
- **串口打不开：** 关闭其他串口监视器，确认目标端口；如 Windows 驱动异常，可重新插拔 USB 后重试。
