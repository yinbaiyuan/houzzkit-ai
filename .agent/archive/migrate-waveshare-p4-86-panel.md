# 迁移 Waveshare ESP32-P4 86 Panel 固件

本 ExecPlan 是活文档。随着工作推进，`Progress`、`Surprises & Discoveries`、`Decision Log`、`Outcomes & Retrospective` 四个章节必须保持最新。

本文件必须按照仓库根目录 `.agent/PLANS.md` 的规则持续维护；完成全部验收后移动到 `.agent/archive/`。

## Purpose / Big Picture

本次工作把已经在 2.1.3 分支实机测通的 Waveshare ESP32-P4-86-Panel-ETH-2RO 板型迁移到当前 GitHub 仓库。完成后，发布脚本能够生成该板固件，设备能够使用 MIPI DSI 屏幕、GT911 触摸、ES8311/ES7210 音频，以及通过板载 ESP32-C6 提供的 ESP-Hosted Wi-Fi 和蓝牙完成微信小程序配网。此次迁移只恢复已有能力，不实现以太网、RS485 和双继电器。

## Progress

- [x] (2026-08-07 07:18Z) 确认目标仓库位于 `main@6b7744e`，工作树干净，并创建分支 `codex/migrate-waveshare-p4-86-panel`。
- [x] (2026-08-07 07:27Z) 新增板级目录、板型注册、发布配置和 ESP32-P4 公共编译兼容。
- [x] (2026-08-07 07:27Z) 迁移 16 kHz 无参考音频路径及准确的 AEC 能力上报。
- [x] (2026-08-07 07:27Z) 核对 CMD 10/CMD 12 配网时序、正常启动后的 BLE 生命周期和 P4 ESPHome API 策略。
- [x] (2026-08-07 07:32Z) 运行 `git diff --check`、板型枚举和 P4 发布构建，生成合并固件、发布 ZIP 与 OTA BIN。
- [x] (2026-08-07 07:36Z) 完成 `zhengchen-1.54tft-wifi` ESP32-S3 发布回归构建，复核差异范围和排除项。
- [x] (2026-08-07 07:36Z) 记录无法在本机完成的实机验收项，完成回顾并归档本计划。

## Surprises & Discoveries

- Observation: 目标仓库和已测通基线都使用项目版本 2.1.3，但 P4 开发分支最初基于更早提交，之后才合入目标主干。
  Evidence: `git merge-base main e3af6ab` 返回 `6b7744e`，而 P4 首个提交 `ef3c748` 的父提交是 `f2b7029`。因此不能整体 cherry-pick，必须按行为选择性适配。
- Observation: 目标仓库已有 P4 所需的 ESP-Hosted、ST7703、GT911 和 NimBLE 依赖，且本板固定启用蓝牙，因此不需要迁移旧分支的无蓝牙 stub 和依赖重排。
  Evidence: `main/idf_component.yml` 已对 `esp_hosted` 和 ST7703 设置 P4 规则，`config.json` 明确设置 `CONFIG_BT_ENABLED=y`；静态差异因此保持在 23 个文件。
- Observation: 干净构建解析到 `78/esp-ml307 3.3.7`，但目标主干 MQTT 代码使用了只存在于后续仓库内置组件的 `MqttConnectError`、`LastConnectError()` 和 `LastConnectErrorMessage()`，导致所有板型在业务代码阶段编译失败。
  Evidence: P4 构建在 `main/protocols/mqtt_protocol.cc` 报告这些类型和成员不存在；目标 `main/idf_component.yml` 仍声明 `78/esp-ml307: ~3.3.6`，仓库没有本地 `components/esp-ml307`。
- Observation: 目标主干的 `WifiBoard::getDeviceName()` 已包含 P4 的远端 STA MAC 分支，但缺少 `esp_wifi_remote.h`，因此首次 P4 构建在最后阶段找不到 `esp_wifi_get_mac()` 声明。
  Evidence: 首次构建在 `main/boards/common/wifi_board.cc:294` 失败；补回已测通分支使用的 P4 条件头文件后，增量构建和完整发布构建均通过。

## Decision Log

- Decision: 以 `feature/esp32-p4-support@e3af6ab` 的用户可见行为为基线，不复制该提交合入的智能音箱、蜂窝网络和诊断代码。
  Rationale: 目标仓库已有更新的 UDP 音频稳定性实现；整体合并会触及一百多个无关文件并覆盖稳定逻辑。
  Date/Author: 2026-08-07 / Codex
- Decision: 保持板型发布身份 `waveshare-p4-86-panel-eth-2ro`，只接入屏幕、触摸、音频、ESP-Hosted Wi-Fi 和 BLE。
  Rationale: 这是已测通固件的实际范围，额外外设属于新功能开发。
  Date/Author: 2026-08-07 / Codex
- Decision: 不迁移旧分支的 BLE stub、其他 Waveshare P4 板型解禁及 `main/idf_component.yml` 依赖重排。
  Rationale: 这些改动不影响固定启用 Hosted NimBLE 的 86 Panel，排除它们能减少对其他板型的影响。
  Date/Author: 2026-08-07 / Codex
- Decision: 对目标主干的 MQTT 连接失败处理恢复通用错误日志与 `SERVICE_CONNECT_FAILED`，不为三个诊断接口迁入完整的本地 esp-ml307 组件。
  Rationale: 这是解除干净构建阻塞的最小兼容修复；失败时仍向用户报告连接失败，不改变成功连接、握手或音频传输路径，也避免新增约六十个与 P4 无关的组件文件。
  Date/Author: 2026-08-07 / Codex

## Outcomes & Retrospective

迁移已按选择性方案完成，共涉及 25 个源码、板级和计划文件，没有导入智能音箱共享架构、堆诊断、硬编码烧录端口或外设驱动。新板型的四个板级文件与 `e3af6ab` 对应文件逐项一致，共享层只补入 P4 编译、真实 AEC 能力、ESPHome 暂停和远端 MAC 等必要适配。

P4 与现有 ESP32-S3 板型均完成全量编译、链接、合并和发布打包。P4 `houzzkit.bin` 为 `0x2ac6b0` 字节，最小应用分区剩余 32%；S3 回归固件为 `0x2c27c0` 字节，剩余 30%。构建仅保留上游 NimBLE `NimBLEService::start()` 的弃用警告，没有新增编译警告或错误。

本机没有连接目标硬件和微信小程序，因此屏幕、触摸、音频、CMD 12 重启前 BLE 链路、重启后上线、30 分钟稳定性仍需用户实机验证。代码检查确认 CMD 12 仍是成功回包后延迟 500 ms 直接 `esp_restart()`，没有新增主动 BLE terminate；正常启动路径仍启动 BLE。

## Context and Orientation

仓库根目录是 `/Users/resmo/projectGithub/houzzkit-ai`。`main/boards/` 保存板级实现；每个板型通过 `main/Kconfig.projbuild` 声明配置选项，通过 `main/CMakeLists.txt` 映射为发布身份。`scripts/release.py` 读取板目录里的 `config.json` 并生成固件。ESP32-P4 本身没有 Wi-Fi 和蓝牙射频，本板通过 ESP-Hosted 让板载 ESP32-C6 作为无线从机；固件中的 NimBLE 只运行主机协议栈，通过 Hosted VHCI 与 C6 通信。

板级音频没有独立回采参考通道，因此 `AUDIO_INPUT_REFERENCE` 必须为 false。设备端 AEC 是 acoustic echo cancellation（声学回声消除）；如果没有参考通道仍向服务器声明 `daec`，服务端会按错误的全双工能力工作，因此必须让音频服务根据真实硬件能力决定是否上报。

配网使用现有 BLE 协议。CMD 10 验证并保存 Wi-Fi，CMD 12 保存服务器配置、成功回包并在约 500 ms 后直接重启。直接重启前不能主动终止 BLE，否则微信小程序会把 terminate 事件视为失败。正常启动后现有 `Application::Start()` 会继续启动 BLE，满足小程序等待设备重新出现的要求。

## Plan of Work

首先迁移 `main/boards/waveshare-p4-86-panel-eth-2ro/` 的四个板级文件，并在 Kconfig、CMake 和发布配置中注册。随后只补齐本板编译所需的 P4 公共兼容，包括 ESPHome variant、Hosted NimBLE 依赖范围、BLE MAC 获取方式以及 ESP-IDF 5.4.1 的 FreeRTOS TLSP 规避项。

然后在不覆盖目标仓库 UDP 改善的前提下，迁移无参考音频的 AEC 能力判断。音频处理器公开是否真正支持设备端 AEC，应用选择半双工监听模式，MQTT 和 WebSocket hello 仅在能力成立时上报 `daec`。P4 的 ESPHome API Server 保持已测通基线中的关闭策略，其余板型不变。

最后核对 BLE CMD 12 仍是“回包、等待、直接重启”，并确认启动路径没有针对 P4 释放 BLE。静态检查通过后执行 P4 发布构建和一个现有 ESP32-S3 板型回归构建。硬件相关验收以串口日志和小程序行为记录，若本机无法完成则明确列为待用户实机验证，不伪造结果。

## Concrete Steps

所有命令都在 `/Users/resmo/projectGithub/houzzkit-ai` 执行。

先检查差异和板型枚举：

    git diff --check
    python scripts/release.py --list-boards

P4 构建命令：

    python scripts/release.py waveshare-p4-86-panel-eth-2ro --name waveshare-p4-86-panel-eth-2ro

代表性 ESP32-S3 回归板型从 `scripts/release.py --list-boards` 的现有列表选择，执行同样的单板发布命令。成功标准是命令退出码为 0，并生成包含 `merged-binary.bin` 的发布包。

## Validation and Acceptance

静态验收要求 `git diff --check` 无输出，新板型出现在发布脚本列表中，差异中没有 `houzzkit-smart-speaker`、PSRAM 最大连续块定时诊断、硬编码串口 Makefile、Ethernet、RS485 或继电器实现。

构建验收要求 P4 和代表性 S3 板型都成功链接和打包。实机验收要求屏幕、背光、触摸、按键和音频正常；BLE 广播可见；CMD 12 成功回包后约 500 ms 出现软件重启，重启前没有主动 BLE terminate；重启后 Wi-Fi、MQTT 或 WebSocket、BLE 正常，小程序完成配网；hello 不包含无效 `daec`；至少运行 30 分钟不出现周期性重启。

## Idempotence and Recovery

代码迁移都通过小范围补丁完成，可重复运行静态检查和构建。构建产物位于忽略目录，不提交。若某项共享改动导致 S3 回归失败，保留板级新增文件，撤销对应共享补丁并把行为限制到 ESP32-P4 条件编译，不使用破坏性的 `git reset --hard`。

## Artifacts and Notes

初始证据：

    target HEAD: 6b7744e
    known-good: e3af6ab
    target PROJECT_VER: 2.1.3
    known-good PROJECT_VER: 2.1.3

完成证据：

    git diff --check: 通过
    release board list: waveshare-p4-86-panel-eth-2ro
    P4 package: releases/v2.1.3/v2.1.3_waveshare-p4-86-panel-eth-2ro.zip
    P4 OTA: releases/v2.1.3-ota/waveshare-p4-86-panel-eth-2ro_v2.1.3.bin
    S3 package: releases/v2.1.3/v2.1.3_zhengchen-1.54tft-wifi.zip
    P4 app partition free: 0x143950 bytes (32%)
    S3 app partition free: 0x12d840 bytes (30%)

## Interfaces and Dependencies

迁移后必须存在 Kconfig 标识 `CONFIG_BOARD_TYPE_ESP32P4_86_PANEL_ETH_2RO` 和发布名 `waveshare-p4-86-panel-eth-2ro`。板型继续依赖 `espressif/esp_hosted`、`h2zero/esp-nimble-cpp`、`espressif/esp_lcd_st7703`、`espressif/esp_lcd_touch_gt911` 以及现有 `BoxAudioCodec`。不新增网络协议字段，不修改 OTA 身份，不增加公共外部 API。

本次创建 ExecPlan，用于记录从已测通 2.1.3 P4 行为向目标主干选择性迁移的实施、验证和取舍。
