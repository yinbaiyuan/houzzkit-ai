# ESP32-P4-86-Panel-ETH-2RO 最小适配执行计划

本 ExecPlan 是活文档。随着工作推进，`Progress`、`Surprises & Discoveries`、`Decision Log`、`Outcomes & Retrospective` 四个章节必须保持最新。

仓库规则见 [`houzzkit-ai/.agent/PLANS.md`](houzzkit-ai/.agent/PLANS.md)。本文件必须按该规范持续维护。

配套复盘文档见 [`docs/houzzkit/esp32-p4-support-review-vs-main.md`](houzzkit-ai/docs/houzzkit/esp32-p4-support-review-vs-main.md)。该文档从 `main` 对比角度补充回答“公共代码是否影响其他芯片”“Ubuntu 打包机应如何依赖 `release.py + config.json` 生效”“哪些公共实现只是后续可收口项”；本 ExecPlan 继续只负责 86 Panel 最小适配的实施过程和证据链。

## Purpose / Big Picture

目标是在不继续混用 `4B` 固件的前提下，为 `Waveshare ESP32-P4-86-Panel-ETH-2RO` 补一个仓库内可选、可打包、可编译的最小板型，并优先让它走蓝牙配网。完成后，用户可以直接执行 `python3 scripts/release.py waveshare-p4-86-panel-eth-2ro --name waveshare-p4-86-panel-eth-2ro` 产出专用包，而不是再拿 `waveshare-p4-wifi6-touch-lcd-4b` 的固件冒充。

## Progress

- [x] 2026-04-14 15:52 +08:00 读取现有 `Waveshare P4` 板目录、`main/Kconfig.projbuild`、`main/CMakeLists.txt`、`main/idf_component.yml` 与 `esp_hosted`/`NimBLE` 相关配置，确认 `P4` 上存在 hosted BLE host-only 路径
- [x] 2026-04-14 16:08 +08:00 新增 `waveshare-p4-86-panel-eth-2ro` 板目录与 `Kconfig/CMake` 入口，复用 `4B` 的最小显示、触摸、音频实现
- [x] 2026-04-14 16:08 +08:00 为新板型在 `config.json` 中开启 `BT + NimBLE host-only + esp_hosted` 配置，并恢复 `esp32p4` 的 `esp-nimble-cpp` 依赖
- [x] 2026-04-14 16:31 +08:00 使用独立 `sdkconfig` 与 `build-p4-86` 完成 `esp32p4 + waveshare-p4-86-panel-eth-2ro` 编译验证，确认 hosted BLE 能完整通过到最终链接
- [x] 2026-04-14 16:33 +08:00 验证 `python3 scripts/release.py --list-boards` 已识别 `waveshare-p4-86-panel-eth-2ro`，并清理临时 `build-p4-86/`
- [x] 2026-04-14 21:24 +08:00 根据真机串口日志定位 `Got IP -> activating -> Certificate validated` 后的崩溃为 `ESP32-P4 + .text XIP from PSRAM + FreeRTOS TLSP deletion callback` 误判问题，并在 `sdkconfig.defaults.esp32p4` 中关闭 `CONFIG_FREERTOS_TLSP_DELETION_CALLBACKS`

## Surprises & Discoveries

- Observation: 当前仓库中的 `P4` 默认并未开启 `BT`，所以“最好走蓝牙配网”不能只靠补板目录，必须同步补 `sdkconfig_append`。
  Evidence: `sdkconfig.defaults.esp32p4` 没有 `CONFIG_BT_ENABLED=y`，而当前工作区 `sdkconfig` 中也显示 `# CONFIG_BT_ENABLED is not set`。

- Observation: `esp_hosted` 官方组件已经内置 `NimBLE host-only + VHCI` 方案，适合 `ESP32-P4` 作为 host、`ESP32-C6` 作为远端蓝牙控制器。
  Evidence: `managed_components/espressif__esp_hosted/Kconfig` 中存在 `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE` 与 `CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI`；示例 `examples/host_nimble_bleprph_host_only_vhci/sdkconfig.defaults` 同时要求 `CONFIG_BT_CONTROLLER_DISABLED=y` 与 `CONFIG_BT_NIMBLE_ENABLED=y`。

- Observation: `release.py` 要求 `build.name` 必须以前缀匹配 `board_type` 目录名，所以新板目录名和产物名需要统一。
  Evidence: `scripts/release.py` 中明确校验 `if not name.startswith(board_type): raise ValueError(...)`。

- Observation: `P4 + BT controller disabled + esp_hosted NimBLE VHCI` 这条路径在本仓库现有 `BLEManager` 上可以直接通过编译与最终链接，不需要再给 BLE 业务代码单独打补丁。
  Evidence: 独立验证命令 `idf.py -B build-p4-86 -DSDKCONFIG=/tmp/... -DIDF_TARGET=esp32p4 -DBOARD_NAME=waveshare-p4-86-panel-eth-2ro build` 成功，输出生成 `build-p4-86/houzzkit.bin`。

- Observation: 真机进入 `activating` 后的重启并不是业务层主动 `esp_restart()`，而是 `FreeRTOS` 在任务删除时把 `pthread_cleanup_thread_specific_data_callback` 误判为“非可执行指针”后 `abort()`。
  Evidence: 用户日志显示 `Certificate validated` 后立即报 `Fatal error: TLSP deletion callback at index 0 overwritten with non-excutable pointer 0x48006ec4`；对 `build/houzzkit.elf` 解符号后，`0x48006ec4` 正是 `pthread_cleanup_thread_specific_data_callback`，位于 `0x4800xxxx` 的 `PSRAM XIP` 文本区。

- Observation: `ESP-IDF v5.4.1` 在 `esp32p4` 上的 `esp_ptr_executable()` 未将 `SOC_EXTRAM_LOW..HIGH` 视为可执行区域，因此会错误拒绝 `.text xip on psram` 场景下的合法回调地址。
  Evidence: `components/esp_hw_support/include/esp_memory_utils.h` 中 `esp_ptr_executable()` 仅检查 `SOC_IROM_*`、`SOC_IRAM_*`、`SOC_IROM_MASK_*` 与可选 `SOC_CACHE_APP_*`；`components/soc/esp32p4/include/soc/soc.h` 中 `SOC_EXTRAM_LOW=0x48000000`，而日志里的回调地址正落在此区间。

## Decision Log

- Decision: 新板目录命名为 `waveshare-p4-86-panel-eth-2ro`，并让发布变体同名。
  Rationale: 这样可以直接兼容 `release.py` 的前缀校验，减少发布链路额外改动。
  Date/Author: 2026-04-14 / Codex

- Decision: 板级实现先直接复用 `waveshare-p4-wifi6-touch-lcd-4b` 的显示、触摸、音频与按键参数，不在本轮接以太网、RS485、继电器。
  Rationale: 用户要求“最小板级适配”，而当前已知基础资源接近；优先把专用板型与蓝牙配网跑通，再单独扩扩展外设更稳。
  Date/Author: 2026-04-14 / Codex

- Decision: `esp-nimble-cpp` 对 `esp32p4` 恢复依赖，不再整体排除。
  Rationale: 新板型要走蓝牙配网，仓库现有 `BLEManager` 依赖该组件；host-only 蓝牙的关键不在于去掉依赖，而在于 `sdkconfig` 切到 `BT controller disabled + esp_hosted NimBLE`。
  Date/Author: 2026-04-14 / Codex

- Decision: 对 `esp32p4` 默认关闭 `CONFIG_FREERTOS_TLSP_DELETION_CALLBACKS`，作为当前 `IDF v5.4.1 + PSRAM XIP` 组合下的最小稳定性绕过。
  Rationale: 崩溃发生在 IDF 的回调合法性检查而非业务代码；关闭该开关可绕过 `vPortTLSPointersDelCb()` 的误判，又不影响当前最关键的蓝牙配网、联网和激活主流程。
  Date/Author: 2026-04-14 / Codex

## Outcomes & Retrospective

当前已完成：

- 新增 `waveshare-p4-86-panel-eth-2ro` 板目录
- 新增 `Kconfig` 与 `CMake` 入口
- 为新板型增加 `release.py` 可识别的 `config.json`
- 在新板型中优先打开 hosted BLE 所需配置
- 独立完成 `esp32p4 + waveshare-p4-86-panel-eth-2ro` 编译验证
- 验证 `release.py` 已能识别该板型
- 根据真机日志定位并绕过 `ESP32-P4` 上 `TLSP deletion callback` 对 `PSRAM XIP` 文本地址的误判崩溃

经验教训：

- `P4` 的蓝牙配网问题关键不在业务代码，而在于构建阶段是否切到 `host-only` 思路
- `release.py` 的目录名前缀约束会直接决定新板命名，先顺着脚本约束设计可以少很多额外改动

补充摘要：

- 从 `main` 对比角度看，`waveshare-p4-86-panel-eth-2ro` 的核心意义不是多加一个板目录，而是把 86 Panel 从“借用 4B 固件”切换为“独立板型 + 独立 OTA/发布通道”。
- 对最终 Ubuntu 打包机发布来说，该板蓝牙和 hosted BLE 能力依赖 `config.json` 中追加的 `sdkconfig_append`，不能只靠默认 `sdkconfig.defaults.esp32p4`。

## Context and Orientation

本次改动集中在四个位置：

- [`main/Kconfig.projbuild`](houzzkit-ai/main/Kconfig.projbuild)：决定新板型能否在菜单中被选择
- [`main/CMakeLists.txt`](houzzkit-ai/main/CMakeLists.txt)：决定 `CONFIG_BOARD_TYPE_*` 如何映射到具体板目录
- [`main/idf_component.yml`](houzzkit-ai/main/idf_component.yml)：决定 `esp32p4` 下是否会拉入 `esp-nimble-cpp`
- [`main/boards/waveshare-p4-86-panel-eth-2ro/`](houzzkit-ai/main/boards/waveshare-p4-86-panel-eth-2ro)：新板目录，包含 `config.h`、`config.json`、板级 `.cc` 与 README

这里的“hosted BLE host-only”是指：`ESP32-P4` 只运行 NimBLE host，不启本地蓝牙控制器；真正的蓝牙控制器在板载 `ESP32-C6` 上，通过 `esp_hosted` 传输层与 `P4` 通信。

## Plan of Work

先增加新板型入口与目录，让仓库和发布脚本能识别 `waveshare-p4-86-panel-eth-2ro`。再把 `config.json` 写成最小可用的 `P4 + BLE host-only` 配置，确保构建时自动打开 `BT`、关闭本地控制器，并启用 `esp_hosted` 的 NimBLE VHCI 路径。最后运行 `idf.py` 编译，确认现有 `BLEManager` 可以在这条新配置下通过。

## Concrete Steps

工作目录：

    cd houzzkit-ai

实施步骤：

    1. 在 main/Kconfig.projbuild 新增 BOARD_TYPE_ESP32P4_86_PANEL_ETH_2RO
    2. 在 main/CMakeLists.txt 将其映射到 waveshare-p4-86-panel-eth-2ro
    3. 在 main/idf_component.yml 恢复 esp32p4 的 esp-nimble-cpp 依赖
    4. 创建 main/boards/waveshare-p4-86-panel-eth-2ro/{config.h,config.json,waveshare-p4-86-panel-eth-2ro.cc,README.md}
5. 运行：

           source ~/esp/esp-idf/export.sh
           TMP_SDKCONFIG=$(mktemp /tmp/sdkconfig.p4-86.XXXXXX)
           cat sdkconfig.defaults sdkconfig.defaults.esp32p4 > "$TMP_SDKCONFIG"
           cat <<'EOF' >> "$TMP_SDKCONFIG"
           CONFIG_BOARD_TYPE_ESP32P4_86_PANEL_ETH_2RO=y
           CONFIG_USE_WECHAT_MESSAGE_STYLE=n
           CONFIG_USE_DEVICE_AEC=y
           CONFIG_BT_ENABLED=y
           CONFIG_BT_CONTROLLER_DISABLED=y
           CONFIG_BT_BLUEDROID_ENABLED=n
           CONFIG_BT_NIMBLE_ENABLED=y
           CONFIG_BT_NIMBLE_TRANSPORT_UART=n
           CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE=y
           CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI=y
           EOF
           idf.py -B build-p4-86 -DSDKCONFIG="$TMP_SDKCONFIG" -DIDF_TARGET=esp32p4 -DBOARD_NAME=waveshare-p4-86-panel-eth-2ro build

## Validation and Acceptance

验收标准：

1. `menuconfig` 中可以选到 `Waveshare ESP32-P4-86-Panel-ETH-2RO`
2. `python3 scripts/release.py waveshare-p4-86-panel-eth-2ro --name waveshare-p4-86-panel-eth-2ro` 能识别新板型
3. `idf.py -DBOARD_NAME=waveshare-p4-86-panel-eth-2ro build` 成功
4. 构建生成的 `sdkconfig` 中存在 `CONFIG_BT_ENABLED=y`、`CONFIG_BT_CONTROLLER_DISABLED=y`、`CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE=y`
5. 构建最终能生成 `houzzkit.bin`，而不是停在 BLE 或链接错误

## Idempotence and Recovery

本次改动是增量增加新板型，可重复执行。若 hosted BLE 路径编译失败，可保留新板目录与板型入口不回滚，再单独把蓝牙相关项拆成后续补丁；这样至少不会再继续依赖 `4B` 板名。

## Artifacts and Notes

关键配置证据：

    managed_components/espressif__esp_hosted/examples/host_nimble_bleprph_host_only_vhci/sdkconfig.defaults
    需要同时打开：
    - CONFIG_BT_ENABLED=y
    - CONFIG_BT_CONTROLLER_DISABLED=y
    - CONFIG_BT_NIMBLE_ENABLED=y
    - CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE=y

## Interfaces and Dependencies

本次新增板型继续复用以下接口：

- `WifiBoard`
- `BoxAudioCodec`
- `MipiLcdDisplay`
- `BLEManager`

本次改动内容：

- 新增 `ESP32-P4-86-Panel-ETH-2RO` 板型入口与板目录
- 通过 `config.json` 为该板型启用 hosted BLE 配置
- 恢复 `esp32p4` 的 `esp-nimble-cpp` 依赖

本次改动原因：

- 用户明确要求不要再用 `4B` 固件测 `86-Panel-ETH-2RO`，而是补一个专用最小板型，并优先走蓝牙配网
