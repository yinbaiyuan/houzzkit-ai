# ESP32-P4 最小接入执行计划

本 ExecPlan 是活文档。随着工作推进，`Progress`、`Surprises & Discoveries`、`Decision Log`、`Outcomes & Retrospective` 四个章节必须保持最新。

仓库规则见 [`houzzkit-ai/.agent/PLANS.md`](houzzkit-ai/.agent/PLANS.md)。本文件必须按该规范持续维护。

配套复盘文档见 [`docs/houzzkit/esp32-p4-support-review-vs-main.md`](houzzkit-ai/docs/houzzkit/esp32-p4-support-review-vs-main.md)。该文档从 `main` 对比角度补充回答“公共代码是否影响其他芯片”“Ubuntu 打包机如何实际生效”“哪些地方值得后续继续收口”；本 ExecPlan 继续只负责最小接入实施过程和原始证据。

## Purpose / Big Picture

目标是在不改现有业务流程的前提下，把仓库里已存在但尚未正式接通的 `esp32p4` 路径补齐，让现有 `Waveshare P4` 板型可以从 `Kconfig` 正常选中并完成编译，同时不影响现有 `esp32s3` 板型。完成后，`P4` 在 `BT` 关闭时仍然可以通过现有 AP/网页路径进入配网流程，而不会继续误提示用户使用 BLE 小程序。

## Progress

- [x] 2026-04-14 11:02 +08:00 读取 `main/Kconfig.projbuild`、`main/CMakeLists.txt`、`main/idf_component.yml`、`components/esphome/*`，确认仓库已存在多块 `Waveshare P4` 板目录，但 `Kconfig` 入口仍被注释
- [x] 2026-04-14 11:08 +08:00 读取 `main/boards/common/wifi_board.cc`、`main/application.cc`、`main/esphome/esphome_device.cc`、`main/ble/ble_manager.*`，确认 BLE 当前是全局强依赖
- [x] 2026-04-14 11:18 +08:00 实施最小接入改动：开放 `Waveshare P4` 板型、修正 ESPhome 目标宏、将 BLE 改为按能力编译、为无 BT 场景切换 AP/网页配网提示
- [x] 2026-04-14 11:18 +08:00 运行 `esp32p4` 与 `esp32s3` 编译验证并记录结果
- [x] 2026-04-14 11:31 +08:00 修复 `system_info.cc`、`wifi_board.cc` 在 `esp32p4` 下对 `esp_wifi_get_mac()` 的兼容问题，避免继续假定本地 Wi-Fi/BT 接口头文件与类型行为与 `esp32s3` 一致
- [x] 2026-04-14 11:42 +08:00 修复 `Waveshare P4` 板文件里 `MIPI DSI` 总线配置宏在 C++ 下的枚举类型不兼容问题，保持原参数不变并完成 `waveshare-p4-wifi6-touch-lcd-4b`、`waveshare-p4-nano-10.1-a` 和 `esp-box-3` 编译验证

## Surprises & Discoveries

- Observation: 仓库里的 `main/CMakeLists.txt` 已经有 `ESP32P4` 板型到目录的映射，但 `main/Kconfig.projbuild` 中对应板型仍是注释状态。
  Evidence: `main/CMakeLists.txt` 存在 `CONFIG_BOARD_TYPE_ESP32P4_*` 分支，而 `main/Kconfig.projbuild` 中同名配置均被 `#` 注释。

- Observation: `components/esphome` 当前无论目标芯片是什么，都会注入 `USE_ESP32_VARIANT_ESP32S3`。
  Evidence: `components/esphome/CMakeLists.txt` 原先固定包含 `-DUSE_ESP32_VARIANT_ESP32S3`，`components/esphome/esphome/core/defines.h` 也无条件兜底到 `ESP32S3`。

- Observation: `m5stack-tab5` 的现有 `sdkconfig` 已经明确关闭 `CONFIG_BT_ENABLED`，这说明 P4 路径确实不能继续把 BLE 当成必然存在。
  Evidence: `main/boards/m5stack-tab5/sdkconfig.tab5` 中存在 `# CONFIG_BT_ENABLED is not set`。

- Observation: `esp32p4` 的远端 Wi-Fi 适配路径下，`esp_wifi_get_mac()` 需要显式包含 `esp_wifi_remote.h`，且不能再沿用原来假设本地 BT MAC 的读法。
  Evidence: 首次 `waveshare-p4-wifi6-touch-lcd-4b` 构建失败在 `main/system_info.cc` 与 `main/boards/common/wifi_board.cc`，错误分别指向 `esp_wifi_get_mac(ESP_MAC_BT, mac)` 的参数类型不匹配，以及缺少 `esp_wifi_get_mac` 声明。

- Observation: Waveshare 提供的 `MIPI DSI` 面板配置宏在 C 里可用，但在当前 C++ 编译参数下会因为 `.phy_clk_src = 0` 触发枚举类型不兼容。
  Evidence: `waveshare-p4-wifi6-touch-lcd-4b` 构建失败在 `ST7703_PANEL_BUS_DSI_2CH_CONFIG()`，编译器报错 `invalid conversion from 'int' to 'mipi_dsi_phy_clock_source_t'`；`JD9365_PANEL_BUS_DSI_2CH_CONFIG()` 存在相同写法。

## Decision Log

- Decision: 本轮只接通现有 `Waveshare P4` 板型，不扩展到 `LilyGO T-Display-P4` 或 `M5Stack Tab5`。
  Rationale: 这是最小改动闭环；仓库里 `Waveshare P4` 目录、`config.json` 与 `CMake` 映射已经齐全，风险最低。
  Date/Author: 2026-04-14 / Codex

- Decision: BLE 采用“同名接口 + 无 BT stub 实现”而不是在业务调用点大面积加条件编译。
  Rationale: 这样可以保持 `application`、`esphome_device`、`wifi_board` 现有调用面稳定，把变更集中在 `BLEManager` 和 `main/CMakeLists.txt`。
  Date/Author: 2026-04-14 / Codex

- Decision: 无 BT 场景不新增整套国际化字段，只在 `WifiBoard::EnterWifiConfigMode()` 本地切换为 AP/网页提示。
  Rationale: 目标是最小接入，当前现有 AP/网页入口已经存在，没必要为这一轮再扩展多语言资源。
  Date/Author: 2026-04-14 / Codex

## Outcomes & Retrospective

当前已完成：

- 打开现有 `Waveshare P4` 板型的 `Kconfig` 入口
- 让 `components/esphome` 能对 `esp32p4` 注入正确的目标变体宏
- 将 BLE 依赖改成按 `CONFIG_BT_ENABLED` 编译，避免 `P4`/无 BT 时继续强拉 NimBLE
- 让无 BT 场景在配网模式下改用现有 AP/网页提示
- 修复 `system_info.cc` 与 `wifi_board.cc` 在 `esp32p4` 下的远端 Wi-Fi MAC 读取兼容问题
- 修复 `Waveshare P4` 板文件中 `MIPI DSI` 总线配置的 C++ 枚举兼容问题

编译验证结果：

- `waveshare-p4-wifi6-touch-lcd-4b`：`idf.py -B build-p4-4b -DIDF_TARGET=esp32p4 ... -DBOARD_NAME=waveshare-p4-wifi6-touch-lcd-4b build` 成功
- `waveshare-p4-nano-10.1-a`：`idf.py -B build-p4-nano -DIDF_TARGET=esp32p4 ... -DBOARD_NAME=waveshare-p4-nano-10.1-a build` 成功
- `esp-box-3`：`idf.py -B build-s3-esp-box-3 -DIDF_TARGET=esp32s3 ... -DBOARD_NAME=esp-box-3 build` 成功，用于回归验证现有 `esp32s3` BLE 路径未受影响

经验教训：

- 这类“芯片已半接入”的仓库，最常见的问题不是缺目录，而是入口被注释、组件宏错误和能力依赖写死
- 对 `esp32p4` 这类使用远端 Wi-Fi/BT 方案的目标，很多“原本在 S3 上理所当然成立”的本地接口假设都需要重新校正

补充摘要：

- 从 `main` 对比角度看，本轮最关键的公共层变更集中在 BLE 按能力编译、音频上行链路收口和实时监听能力判断。
- 这些公共改动会影响其他芯片，但当前判断属于“公共但安全”的修正，而不是把 `P4` 私有逻辑强塞到所有板型。

## Context and Orientation

这次改动涉及四个最关键的入口：

- [`main/Kconfig.projbuild`](houzzkit-ai/main/Kconfig.projbuild)：定义板型选择入口，决定 `esp32p4` 板型能否在菜单中被选中
- [`components/esphome/CMakeLists.txt`](houzzkit-ai/components/esphome/CMakeLists.txt) 与 [`components/esphome/esphome/core/defines.h`](houzzkit-ai/components/esphome/esphome/core/defines.h)：决定 ESPhome 组件看到的芯片变体宏
- [`main/ble/ble_manager.h`](houzzkit-ai/main/ble/ble_manager.h) 与 [`main/ble/ble_manager_stub.cc`](houzzkit-ai/main/ble/ble_manager_stub.cc)：为 `BT` 关闭场景提供同名空实现
- [`main/boards/common/wifi_board.cc`](houzzkit-ai/main/boards/common/wifi_board.cc)：控制配网模式提示与 BLE/AP 配网路径

这里的“按能力编译”指是否启用 BLE，不再由业务代码假设，而是由 `CONFIG_BT_ENABLED` 控制底层是否编译真实 BLE 实现。这里的“AP/网页配网”指已有的 `WifiConfigurationAp` 组件，它已经负责启动配置网络并提供 `http://192.168.4.1` 的网页入口。

## Plan of Work

先在 `main/Kconfig.projbuild` 解开现有 `Waveshare P4` 板型，让 `esp32p4` 目标能在菜单中选到这些板。随后调整 `components/esphome/CMakeLists.txt` 与 `defines.h`，让 `esp32p4` 不再带着错误的 `ESP32S3` 变体宏进入组件编译。

接着在 `main/CMakeLists.txt` 中把 BLE 源码选择改成条件编译：`BT` 打开时编译真实 `ble_manager.cc + proto_parse.cc`，`BT` 关闭时只编译 `ble_manager_stub.cc`。为了避免 `esp32p4` 仍被 NimBLE 依赖拖住，再同步调整 `main/idf_component.yml` 中 `h2zero/esp-nimble-cpp` 的目标规则。

最后收口到 `main/boards/common/wifi_board.cc`，在进入配网模式时，如果 `BT` 关闭，就不再启动 BLE 配网服务，而是继续使用已有 AP/网页配网流程，并展示网页入口提示。

## Concrete Steps

工作目录统一使用仓库根目录：

    cd houzzkit-ai

本次实施前用于确认现状的命令：

    sed -n '180,320p' main/Kconfig.projbuild
    sed -n '1,120p' main/CMakeLists.txt
    sed -n '1,180p' components/esphome/CMakeLists.txt
    sed -n '1,120p' components/esphome/esphome/core/defines.h
    sed -n '1,120p' main/boards/common/wifi_board.cc
    sed -n '1,220p' main/ble/ble_manager.h

本次实施后要运行的验证命令：

    idf.py set-target esp32p4
    idf.py -DBOARD_NAME=waveshare-p4-wifi6-touch-lcd-4b build

    idf.py set-target esp32p4
    idf.py -DBOARD_NAME=waveshare-p4-nano-10.1-a build

    idf.py set-target esp32s3
    idf.py -DBOARD_NAME=kevin-sp-v3-dev build

## Validation and Acceptance

验收标准如下：

1. `esp32p4` 目标下可以在 `Kconfig` 选择现有 `Waveshare P4` 板型。
2. `waveshare-p4-wifi6-touch-lcd-4b` 可以完成编译。
3. `waveshare-p4-nano-10.1-a` 可以完成编译。
4. `kevin-sp-v3-dev` 在 `esp32s3` 目标下继续可编译。
5. `BT` 关闭时，`WifiBoard::EnterWifiConfigMode()` 不再提示 BLE 小程序，而是提示用户通过 `http://192.168.4.1` 配网。

## Idempotence and Recovery

本次改动全部是增量源码修改，可安全重复执行。若 `esp32p4` 编译中发现额外的板级驱动问题，可先保留本次“入口接通 + 能力解耦”提交，再单独处理板级兼容问题，避免把“芯片接通”与“板驱动补丁”混在一轮里。

如果要临时回退到改动前状态，只需要撤销以下文件上的变更即可：

- `main/Kconfig.projbuild`
- `components/esphome/CMakeLists.txt`
- `components/esphome/esphome/core/defines.h`
- `main/CMakeLists.txt`
- `main/idf_component.yml`
- `main/boards/common/wifi_board.cc`
- `main/ble/ble_manager.h`
- `main/ble/ble_manager_stub.cc`

## Artifacts and Notes

本次实现的关键设计证据：

    现状 1：
    main/CMakeLists.txt 已存在 CONFIG_BOARD_TYPE_ESP32P4_* -> boards/<dir> 映射

    现状 2：
    main/Kconfig.projbuild 里相同板型入口仍被注释，导致无法从菜单正式选到

    现状 3：
    components/esphome 当前固定注入 USE_ESP32_VARIANT_ESP32S3

    现状 4：
    main/boards/m5stack-tab5/sdkconfig.tab5 已明确关闭 CONFIG_BT_ENABLED

## Interfaces and Dependencies

本次必须保持以下接口关系不变：

- [`main/ble/ble_manager.h`](houzzkit-ai/main/ble/ble_manager.h) 继续提供 `BLEManager::GetInstance()` 与各类 `notify*` / `start` / `otaProgress` 方法，业务调用点不改签名
- [`main/boards/common/wifi_board.cc`](houzzkit-ai/main/boards/common/wifi_board.cc) 继续使用 `WifiConfigurationAp` 作为配网主流程
- [`components/esphome/CMakeLists.txt`](houzzkit-ai/components/esphome/CMakeLists.txt) 继续只向组件层暴露编译宏，不改 ESPhome 业务代码

本次改动内容：

- 打通 `Waveshare P4` 的板型入口
- 修复 ESPhome 芯片变体宏
- 引入无 BT 的 BLE stub 实现
- 将配网提示切换为能力感知

本次改动原因：

- 用户要求“在不影响当前业务逻辑的情况下，用最小代码支持 esp32-p4”，当前最小闭环就是把半接入状态补成可编译状态
