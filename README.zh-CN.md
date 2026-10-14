<!--
Copyright (c) Meta Platforms, Inc. and affiliates.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# Muse Gadgets

> 本文是 [README.md](README.md) 的中文译本。

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset=".github/images/muse-gadgets-dark.png">
    <img src=".github/images/muse-gadgets-light.png" width="900" alt="Muse 设备：一块 Waveshare 圆形 AMOLED、一块 M5Stack StickS3、Muse Home Link、一台树莓派，以及一块 Seeed reTerminal 电子墨水屏">
  </picture>
</p>

Muse gadgets 是你可以自己动手做的开源设备。用我们的设备 SDK 给现成的 ESP32 开发板写程序，或者把树莓派（Raspberry Pi）配置好，再把 Muse 接到显示器、按键、传感器、执行器，以及工作台上任何你手头有的东西上。

我们在这里开源了 SDK 和固件。它由极客写给极客，纯粹为了好玩。折腾的副作用可能包括板子变砖、保修作废、电压跌落，甚至破产。风险自负！

| | |
|---|---|
| [**ESP32 设备 SDK**](esp32) | 通过我们的开源 SDK，把你的 ESP32 开发板接到 Muse。加一块屏幕用来显示图片，接上音频输入和输出，或者再接上其他传感器。 |
| [**Linux 设备 SDK**](linux) | 把闲置的树莓派或 Linux 主机变成一台 Muse 设备。写上你自己的命令，让 Muse 接手系统管理杂活，或者接入你的 Home Assistant。 |

烧录或配对设备之前，先获取一个
[SDK token](https://gadgets.muse.ai/settings/sdk-tokens)，并阅读
[Gadget SDK 条款](https://gadgets.muse.ai/sdk-terms)。每台设备配对都需要一个 token。

ESP32 和 Linux 设备通过 iOS 与 Android 上的 Muse 应用配对，入口是「设置 > 设备」。先在那里打开开发者模式，再查找名称以 "MuseGadget" 开头的设备。
每个目录都有一份 `README.md` 用来入门，以及一份 `AGENTS.md`，供 [Muse Code](https://developer.meta.com/ai/lp/muse-code/) 这类编程助手使用。

电池输入端的功耗分析，请看[移植步骤](esp32/docs/power/README.md)。
可选启用的诊断核心与具体板型无关；各板提供它的钩子和状态矩阵，由 [power_sweep.py](esp32/tools/power/power_sweep.py) 驱动这次不经过 USB 的扫描。这套独立诊断不会配对，也不需要 SDK token；测试夹具的安全，以及明确的硬件授权，仍然放在最前面，具体见 [PPK2 规则](esp32/docs/power/ppk2.md)。

## 社区

来我们的社区 [Discord](https://discord.gg/3bhjCkZdd6) 认识其他正在制作、改装 Muse 设备的人。找灵感、互相帮忙，也把你做出来的东西分享出去。

## 许可证

Muse Gadgets 采用 Apache License 2.0，全文见
[`LICENSE`](LICENSE)。下面这些第三方文件除外，它们继续使用各自上游的许可证：

| 路径 | 上游 | 许可证 |
|---|---|---|
| [`esp32/components/minimp3/include/minimp3.h`](esp32/components/minimp3) | [lieff/minimp3](https://github.com/lieff/minimp3) | CC0-1.0，见 [`LICENSE`](esp32/components/minimp3/LICENSE) |
| [`esp32/components/muse/boards/ostb_echoear_display_init.h`](esp32/components/muse/boards/ostb_echoear_display_init.h) | [78/xiaozhi-esp32 ESP-VoCat 面板时序](https://github.com/78/xiaozhi-esp32/blob/0d576d3d4c049c6f55eaf879725dc23e516511b4/main/boards/espressif/esp-vocat/esp_vocat.cc) | MIT，保留在文件头中 |
| [`esp32/main/pixel_font.c`](esp32/main/pixel_font.c) | Adafruit GFX `glcdfont.c` | BSD-2-Clause，写在文件头中 |

构建时拉取的依赖使用它们自己的许可证：ESP-IDF
组件（位于 `esp32/managed_components/`），以及模拟器使用的 LVGL 和
SDL（列在 [`esp32/simulator/THIRD_PARTY.md`](esp32/simulator/THIRD_PARTY.md)）。

Apache 许可证不覆盖 [Jollybot 头像](esp32/avatar)。
