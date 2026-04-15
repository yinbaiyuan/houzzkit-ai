# P4 分支对比 `main` 的公共代码复盘与 Ubuntu 打包发布说明

## 1. 文档目的

本文只回答四个问题：

- 当前分支相对 `main` 改了哪些公共代码
- 这些公共代码是否会影响其他芯片
- 这些改动为什么要保留，哪些地方只是可维护性上还能继续收口
- 最终走 Ubuntu 打包机发布时，这些改动如何在 `release.py` 链路里生效

本文不替代实施过程文档。实施过程和原始证据仍以以下两份 ExecPlan 为准：

- [`docs/houzzkit/esp32-p4-minimal-support.execplan.md`](houzzkit-ai/docs/houzzkit/esp32-p4-minimal-support.execplan.md)
- [`docs/houzzkit/esp32-p4-86-panel-eth-2ro.execplan.md`](houzzkit-ai/docs/houzzkit/esp32-p4-86-panel-eth-2ro.execplan.md)

## 2. 先给结论

### 2.1 公共代码会不会影响其他芯片

会，但当前分支里的公共改动大多属于“能力感知”和“链路收口”，不是把 `ESP32-P4` 私有逻辑硬塞给所有芯片。

可以把本分支改动分成三类：

- `P4 only`
  - 只在 `CONFIG_IDF_TARGET_ESP32P4` 下生效，或者只作用于 `esp32p4` 的构建入口与默认配置。
- `公共但安全`
  - 会影响所有芯片或所有同类配置，但本质上是在修正原来不够严谨的公共链路。
- `公共且建议后续收口`
  - 当前功能上成立，但实现方式还有继续收窄的空间，主要是维护性问题，不是现阶段阻断问题。

### 2.2 当前判断

- 当前没有发现“为了支持 P4 而明显破坏其他芯片”的阻断性问题。
- 当前最值得后续继续收口的公共层问题，是 `BLEManager` 在无 BT 场景下复制了一套完整类声明，长期看容易和真实实现漂移。
- 对最终发布来说，最关键的事实不是本地开发机能否直接 `idf.py build`，而是 Ubuntu 打包机是否按 `scripts/release.py` 和板级 `config.json` 正确生成目标固件。

## 3. 公共代码复盘

### 3.1 公共构建与能力边界

涉及文件：

- `main/CMakeLists.txt`
- `main/ble/ble_manager.h`
- `main/ble/ble_manager_stub.cc`
- `main/boards/common/wifi_board.cc`
- `main/idf_component.yml`

改了什么：

- `main/CMakeLists.txt` 改成按 `CONFIG_BT_ENABLED` 选择真实 BLE 实现或 stub 实现。
- `main/ble/ble_manager.h` 在无 BT 场景下提供同名接口声明。
- `main/ble/ble_manager_stub.cc` 提供无 BT 的空实现，保持调用面不变。
- `main/boards/common/wifi_board.cc` 在无 BT 时改为只提示 AP/Web 配网，不再误提示用户使用 BLE。
- `main/idf_component.yml` 恢复 `esp32p4` 对 `h2zero/esp-nimble-cpp` 的依赖规则，使需要 BLE 的 P4 板型可以在构建期正确拉入依赖。

改动原因：

- 仓库里并不是所有板都具备蓝牙能力，`ESP32-P4` 尤其不能继续默认假定“本地 BT 一定存在”。
- 之前 BLE 相关代码是全局强依赖，导致“芯片可编译”和“板型实际具备 BT 能力”之间没有明确边界。

作用和意义：

- 把“是否启用 BLE”从业务层判断收回到编译能力层，减少调用点分叉。
- 让无 BT 板型仍能复用原有应用逻辑，而不是为了不支持 BLE 去拆动更多业务代码。
- 让配网提示和实际可用入口一致，避免用户界面继续给出错误引导。

是否影响其他芯片：

- 会。
- 所有关闭 `CONFIG_BT_ENABLED` 的板型都会走这条公共路径，而不只是 `ESP32-P4`。
- 这种影响是预期内的，方向是“按能力退化”，不是“按芯片强行分流”。

对 Ubuntu 打包机是否有额外要求：

- 有。
- 最终是否走真实 BLE 或 stub，不取决于开发机当前状态，而取决于打包时写入 `sdkconfig` 的能力开关。
- 对打包机来说，关键是板级 `config.json` 中的 `sdkconfig_append` 是否把所需 BT 配置写全。

当前判断：

- 归类为“公共但安全”。
- 其中 `main/ble/ble_manager.h` 的 stub 声明方式归类为“公共且建议后续收口”，因为同一份头文件里复制完整接口，后续新增方法时容易漏同步。

### 3.2 公共运行时链路

涉及文件：

- `main/application.cc`
- `main/application.h`
- `main/audio/audio_processor.h`
- `main/audio/audio_service.cc`
- `main/audio/audio_service.h`
- `main/audio/processors/afe_audio_processor.cc`
- `main/audio/processors/afe_audio_processor.h`
- `main/audio/processors/no_audio_processor.cc`
- `main/audio/processors/no_audio_processor.h`
- `main/protocols/mqtt_protocol.cc`
- `main/protocols/mqtt_protocol.h`
- `main/ota.cc`

改了什么：

- `application` 增加 `GetEffectiveAecMode()`、`SupportsRealtimeListening()`、`GetPreferredChatListeningMode()`，把实时对话能力和设备真实 AEC 能力绑定。
- `audio_service` 增加 `ResetUplink()` 与 `SupportsDeviceAec()`，在关通道、重新进 listening、MQTT 断线后清掉上行残留队列。
- `afe_audio_processor` 和 `no_audio_processor` 增加 `SupportsDeviceAec()` 接口，避免上层把“不支持设备 AEC 的音频路径”误当成可实时双工。
- `mqtt_protocol` 在 MQTT 断开时主动关闭音频通道，并在 `CloseAudioChannel()` 中重置会话状态。
- `ota.cc` 改为从 `websocket` 命名空间读取 `ws_url`，修正之前错误复用 `mqtt` 配置对象的问题。

改动原因：

- 原先公共状态机把“配置允许设备 AEC”和“当前音频路径实际支持设备 AEC”混在了一起。
- 原先 MQTT 音频链路在断线、切换状态、重新开通道时可能残留旧上行数据或旧会话状态。
- 原先 OTA 对 WebSocket 配置的读取位置不准确，会让配置判断和实际存储结构不一致。

作用和意义：

- 避免不支持输入参考的板型误进实时双工监听。
- 减少 MQTT 断线后的悬挂通道、残留音频包、重复会话状态。
- 让协议切换和 OTA 配置判断回到和实际设置结构一致的状态。

是否影响其他芯片：

- 会。
- 这些属于真正的公共运行时逻辑，任何使用相同状态机、音频服务和 MQTT 协议的板型都会受影响。
- 但影响方向是“纠正原有不严谨行为”，不是为 P4 特地开一条公共分叉。

对 Ubuntu 打包机是否有额外要求：

- 没有额外系统要求。
- 这部分效果体现在最终编译出的固件行为上，与 Ubuntu 和 macOS 的差别不大。
- 打包机只需要按目标板型产出正确固件即可。

当前判断：

- 归类为“公共但安全”。
- 这是当前分支最值得保留的公共改动部分，因为它们解决的是跨板型都可能遇到的链路收口问题。

### 3.3 代码卫生与后续可收口项

涉及文件：

- `main/ble/ble_manager.cc`
- `main/ble/ble_manager.h`

当前观察：

- `git diff --check main...HEAD` 报出 `main/ble/ble_manager.cc:480` 存在尾随空白。
- `BLEManager` 的无 BT 场景通过复制整套类声明来保持接口兼容，当前可用，但长期维护成本偏高。
- 实际编译 `esp32s3` 和 `esp32p4` 时，`main/ble/ble_manager.cc` 里的 `bleService->start()` 会触发 `NimBLEService::start()` 已废弃的告警。

意义：

- 这两项都不是当前分支的功能阻断点。
- 但如果后续还会继续扩展 BLE 属性、OTA 或配网指令，建议把无 BT stub 的接口声明进一步收敛，避免头文件两套签名漂移。
- 同时建议在下一轮顺手清理 BLE 初始化里的废弃接口调用，减少后续依赖升级时的噪音。

当前判断：

- 归类为“公共且建议后续收口”。

## 4. P4 最小接入复盘

涉及文件：

- `main/Kconfig.projbuild`
- `main/CMakeLists.txt`
- `components/esphome/CMakeLists.txt`
- `components/esphome/esphome/core/defines.h`
- `main/system_info.cc`
- `sdkconfig.defaults.esp32p4`
- `main/boards/waveshare-p4-nano/esp32-p4-nano.cc`
- `main/boards/waveshare-p4-wifi6-touch-lcd-4b/esp32-p4-wifi6-touch-lcd-4b.cc`
- `main/boards/waveshare-p4-wifi6-touch-lcd-xc/esp32-p4-wifi6-touch-lcd-xc.cc`

改了什么：

- 打开现有 `Waveshare P4` 板型的 `Kconfig` 入口和 `CMake` 映射。
- 调整 ESPhome 组件编译宏，让 `esp32p4` 不再误带 `ESP32S3` 变体宏。
- `system_info.cc` 在 `esp32p4` 下改为回退读取 STA MAC，而不是继续假定本地 BT MAC 接口可用。
- `sdkconfig.defaults.esp32p4` 默认关闭 `CONFIG_FREERTOS_TLSP_DELETION_CALLBACKS`，绕过 `ESP-IDF v5.4.1 + P4 + PSRAM XIP` 下的已知误判问题。
- 三块现有 `Waveshare P4` 板的 DSI 总线配置改为显式结构体写法，修复 C++ 下的枚举类型不兼容。

改动原因：

- 仓库原先已经有部分 P4 板目录和映射，但入口并未正式打通。
- P4 使用远端 Wi-Fi/BT 方案，很多在 S3 上默认成立的本地接口假设在 P4 上不成立。
- Waveshare 提供的宏在 C 环境可用，但当前 C++ 编译条件下会触发类型不兼容。

作用和意义：

- 把“仓库里半接入的 P4 路径”补成正式可选、可编译的最小闭环。
- 让 ESPhome、系统信息和板级驱动在 P4 下使用正确的芯片语义，而不是继续借 S3 路径硬跑。

是否影响其他芯片：

- 基本不会。
- 这部分要么是 `esp32p4` 专属配置，要么是只影响 P4 板目录。

对 Ubuntu 打包机是否有额外要求：

- 有。
- 打包机必须使用 `esp32p4` 目标进行构建，不能沿用旧板型或旧目标。
- 最终产物是否正确，依赖打包脚本是否真的把板型 `target` 和对应 `sdkconfig_append` 带入构建。

当前判断：

- 归类为 `P4 only`。
- 这部分不是过度设计，而是把“已有但未接通的 P4 路径”补成最小可交付状态。

## 5. `waveshare-p4-86-panel-eth-2ro` 适配复盘

涉及文件：

- `main/boards/waveshare-p4-86-panel-eth-2ro/config.h`
- `main/boards/waveshare-p4-86-panel-eth-2ro/config.json`
- `main/boards/waveshare-p4-86-panel-eth-2ro/waveshare-p4-86-panel-eth-2ro.cc`
- `main/boards/waveshare-p4-86-panel-eth-2ro/README.md`
- `main/Kconfig.projbuild`
- `main/CMakeLists.txt`

改了什么：

- 新增 `waveshare-p4-86-panel-eth-2ro` 板目录与入口。
- 为该板新增 `config.json`，通过 `sdkconfig_append` 打开 `BT + NimBLE host-only + esp_hosted VHCI` 路径。
- 板级实现复用 `4B` 的显示、触摸、音频和基础 UI 资源，但保留独立板型标识。
- `config.h` 将该板的音频输入/输出采样率收敛为 `16k/16k`，避免继续放大重采样负担。

改动原因：

- 不能继续用 `waveshare-p4-wifi6-touch-lcd-4b` 的固件去冒充 `86-panel-eth-2ro`，否则 OTA 通道和板型识别都会混乱。
- 该板要优先走蓝牙配网，而仓库默认 P4 配置并不会自动打开这条 hosted BLE 路径。

作用和意义：

- 让 86 Panel 拥有独立的板型标识、独立的发布包和独立 OTA 通道。
- 把“最小可用板型适配”和“额外外设补齐”拆开，先优先交付基础固件。

是否影响其他芯片：

- 不会直接影响其他芯片。
- 它主要影响新板型本身，以及 `release.py --list-boards` 结果中新增一个可打包目标。

对 Ubuntu 打包机是否有额外要求：

- 有，而且这是最关键的一组要求。
- 86 Panel 的蓝牙和 hosted BLE 不是来自 `sdkconfig.defaults.esp32p4`，而是来自该板 `config.json` 中的 `sdkconfig_append`：
  - `CONFIG_BT_ENABLED=y`
  - `CONFIG_BT_CONTROLLER_DISABLED=y`
  - `CONFIG_BT_NIMBLE_ENABLED=y`
  - `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE=y`
  - `CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI=y`
- 也就是说，Ubuntu 打包机若不走 `python3 scripts/release.py waveshare-p4-86-panel-eth-2ro --name waveshare-p4-86-panel-eth-2ro` 这类板级打包入口，而是直接套默认 P4 配置，最终固件能力会不完整。

当前判断：

- 归类为 `P4 only`。
- 这部分不是过度设计，而是为了保证板型独立发布和蓝牙配网路径在打包机上真正生效。

## 6. Ubuntu 打包机发布链路说明

这里以 [`scripts/release.py`](houzzkit-ai/scripts/release.py) 为准，而不是以开发机手动执行 `idf.py build` 为准。

### 6.1 `release.py` 实际会做什么

- 从 `main/boards/<board>/config.json` 读取：
  - `target`
  - `builds`
  - `sdkconfig_append`
- 通过 `main/CMakeLists.txt` 反查该板对应的 `CONFIG_BOARD_TYPE_*`
- 调用：
  - `idf.py set-target <target>`
  - 向 `sdkconfig` 追加 `CONFIG_BOARD_TYPE_*` 和 `sdkconfig_append`
  - `idf.py -DBOARD_NAME=<name> build`
  - `idf.py merge-bin`
- 最后生成：
  - `releases/<name>/v<version>/v<version>_<name>.zip`
  - `releases/v<version>/v<version>_<name>.zip`
  - 若存在 `build/houzzkit.bin`，则复制到 `releases/v<version>-ota/`

### 6.2 当前分支对 Ubuntu 打包机的直接影响

- `waveshare-p4-86-panel-eth-2ro` 已被 `release.py --list-boards` 识别。
- `build.name` 必须以板目录名开头，这也是为什么新板目录名和发布包名保持一致。
- 86 Panel 的 BLE/hosted BLE 能力依赖 `config.json`，因此打包机必须通过板级配置驱动构建，不能只靠默认 P4 `sdkconfig`。

### 6.3 为什么文档要把“开发机验证”和“Ubuntu 打包机验证”分开

- 开发机上的 `idf.py build` 主要用于辅助确认公共代码没有明显破坏已有路径。
- 最终发布是否正确，取决于 Ubuntu 打包机是否按 `release.py + config.json` 的组合运行。
- 这意味着“开发机能编译”不等于“发布配置完整”，反过来也一样。

## 7. 验证记录

### 7.1 静态核查

- `git diff --check main...HEAD`
  - 结果：`main/ble/ble_manager.cc:480` 存在尾随空白
- `python3 scripts/release.py --list-boards`
  - 结果：已识别
    - `waveshare-p4-wifi6-touch-lcd-4b`
    - `waveshare-p4-86-panel-eth-2ro`

### 7.2 开发机辅助验证

- 本地初始状态下 `idf.py` 不在 PATH，需先执行 `source ~/esp/esp-idf/export.sh`
- 直接执行 `idf.py -B build-review-s3 -DIDF_TARGET=esp32s3 ... build` 时，因仓库根目录现有 `sdkconfig` 锁定为 `esp32p4` 而失败
  - 这说明开发机验证会受当前工作区状态影响，不能替代打包机发布结论
- 改用临时 `sdkconfig` 后，以下辅助构建已成功：
  - `idf.py -B build-review-s3 -DSDKCONFIG=/tmp/... -DIDF_TARGET=esp32s3 -DBOARD_NAME=esp-box-3 build`
  - `idf.py -B build-review-p4 -DIDF_TARGET=esp32p4 -DBOARD_NAME=waveshare-p4-wifi6-touch-lcd-4b build`
- 两条构建都成功生成 `houzzkit.bin`，说明当前公共代码没有明显打断 `esp32s3` 和 `esp32p4` 的基本编译路径。
- 两条构建都出现同一条 BLE 废弃告警：
  - `main/ble/ble_manager.cc:159` 调用 `NimBLEService::start()`，当前不阻断构建，但建议后续清理。

### 7.3 Ubuntu 打包机口径下的结论

- 当前最应信任的发布入口是 `release.py`
- 86 Panel 的板型识别和发布配置入口已经存在
- 已在当前环境实际执行：
  - `python3 scripts/release.py waveshare-p4-86-panel-eth-2ro --name waveshare-p4-86-panel-eth-2ro`
- 结果：
  - 成功完成 `idf.py set-target esp32p4`
  - 成功追加 `CONFIG_BOARD_TYPE_ESP32P4_86_PANEL_ETH_2RO=y` 及 86 Panel 的 BLE/hosted BLE 配置
  - 成功生成 `build/houzzkit.bin`
  - 成功执行 `idf.py merge-bin`
  - 成功生成 `releases/v2.1.2/v2.1.2_waveshare-p4-86-panel-eth-2ro.zip`
  - 成功生成 `releases/v2.1.2-ota/waveshare-p4-86-panel-eth-2ro_v2.1.2.bin`
- 这说明 86 Panel 的发布入口、命名规则和 `config.json` 配置在实际打包链路里已经生效。
- 由于最终生产环境仍是 Ubuntu 打包机，正式发布时仍应以 Ubuntu 上同命令的结果作为最终验收。

## 8. 最终判断

### 8.1 公共代码改动是否有明显无效或过度设计

- 没有发现明显“无效改动”。
- 也没有发现明显“为了 P4 而把公共层改得过宽”的阻断性过度设计。
- 真正属于“可继续优化”的，主要是 `BLEManager` 无 BT stub 的实现方式，而不是本轮公共逻辑本身。

### 8.2 建议保留的部分

- `application` / `audio_service` / `mqtt_protocol` / `ota` 的公共收口改动
- `wifi_board` 的无 BT 配网提示切换
- `CONFIG_BT_ENABLED` 驱动的 BLE 真身 / stub 编译分流
- P4 变体宏、P4 板入口、P4 驱动兼容修正
- 86 Panel 的独立板型与 `config.json` 发布配置

### 8.3 建议后续补一轮小收口的部分

- 清理 `main/ble/ble_manager.cc` 的尾随空白
- 评估是否把 `BLEManager` 无 BT stub 的接口声明进一步收敛，减少双份接口漂移风险
- 清理 `main/ble/ble_manager.cc:159` 的 `NimBLEService::start()` 废弃调用
