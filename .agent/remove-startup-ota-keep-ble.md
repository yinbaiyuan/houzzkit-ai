# 移除启动 OTA 检查，保留 BLE 手动升级

本 ExecPlan 是活文档。随着工作推进，`Progress`、`Surprises & Discoveries`、`Decision Log`、`Outcomes & Retrospective` 四个章节必须保持最新。

本文件遵循仓库根目录 `.agent/PLANS.md` 的规则维护。当前工作目录是 `C:\Users\Administrator\.codex\worktrees\194d\houzzkit-ai-firmware`。

## Purpose / Big Picture

设备启动后不再访问旧的 OTA 检查接口，不再因为 `http/http_url` 缺失而进入配网，也不再走激活码或自动固件升级流程。用户仍然可以通过 BLE 主动下发固件 URL 来升级设备。MQTT 成功连接后，设备会额外发送一个轻量 `{"type":"time_sync"}` 请求，收到服务器时间后更新系统时钟；失败时只记录日志，不影响启动和协议连接。

完成后可以通过代码搜索和编译验证观察到：`main/ota.cc` 不再参与编译，`Ota` 类型无残留引用，`CONFIG_OTA_URL` 不再存在；启动流程直接在资源检查、网络连接后初始化协议；BLE 的 `CMD_OTA_UPDATE` 路径仍然调用 `Application::otaUpgrade()`；MQTT 连接成功后会 best-effort 发布 `time_sync`。

## Progress

- [x] (2026-04-30 04:21Z) 阅读 `.agent/PLANS.md` 并确认本任务需要 ExecPlan。
- [x] (2026-04-30 04:21Z) 搜索现有 `Ota`、`http_url`、`CONFIG_OTA_URL`、MQTT 和 BLE OTA 引用。
- [x] (2026-04-30 04:31Z) 移除启动自动 OTA 检查与 `Ota` 类残留引用。
- [x] (2026-04-30 04:31Z) 迁移原 `Ota::MarkCurrentVersionValid()` 的固件确认逻辑到启动早期。
- [x] (2026-04-30 04:31Z) 调整 WiFi 配网判断和 `Application::Start()` 协议选择。
- [x] (2026-04-30 04:31Z) 实现 MQTT 连接成功后的 best-effort `time_sync` 请求和响应解析。
- [x] (2026-04-30 04:31Z) 清理 `CONFIG_OTA_URL` 和 `main/ota.cc` 编译项。
- [x] (2026-04-30 04:50Z) 运行残留引用搜索，确认无 `Ota` 类型、`CheckNewVersion`、`UpgradeFirmware`、`CONFIG_OTA_URL` 等源码残留。
- [x] (2026-04-30 04:50Z) 尝试 `idf.py build`；构建启动但在既有 ESPHome/ESP32 目标兼容问题处失败，未到 main 链接阶段。
- [x] (2026-04-30 04:50Z) 使用 `compile_commands.json` 单独编译 `main/protocols/mqtt_protocol.cc` 和 `main/mcp_server.cc` 成功；`application.cc` 与 `wifi_board.cc` 被既有 NimBLE `syscfg/syscfg.h` 生成依赖阻塞。

## Surprises & Discoveries

- Observation: 当前仓库没有 `managed_components` 目录，本地源里找不到 `<mqtt.h>` 的实现。
  Evidence: `Get-ChildItem -Recurse -File -Filter mqtt.h` 未返回结果；`main/protocols/mqtt_protocol.cc` 只通过抽象 `Mqtt` 对象调用 `Publish`、`OnMessage`、`Connect`。

- Observation: `rg.exe` 在当前 Codex 桌面环境里被 WindowsApps 路径拒绝执行。
  Evidence: 第一次执行 `rg -n ...` 返回 `Program 'rg.exe' failed to run: Access is denied`，后续改用 PowerShell `Select-String`。

- Observation: `idf.py build` 已能启动并生成托管组件，但当前默认 `sdkconfig` 目标为 `esp32` 且没有板卡类型，构建在 ESPHome 组件处失败，不是本次修改的文件。
  Evidence: 构建日志报 `components/esphome/esphome/core/helpers.cpp:747:37: error: 'ESP_EFUSE_USER_DATA_MAC_CUSTOM' was not declared in this scope`。

- Observation: 底层 MQTT 封装不会自动订阅下行 topic。
  Evidence: `managed_components/78__esp-ml307/include/mqtt.h` 提供 `Subscribe()`；`managed_components/78__esp-ml307/src/esp/esp_mqtt.cc` 只在调用 `Subscribe()` 时执行 `esp_mqtt_client_subscribe_single()`。

## Decision Log

- Decision: 删除 MCP 的 `self.upgrade_firmware` 工具，只保留 BLE 手动 OTA。
  Rationale: 用户方案明确要求避免 AI/远程工具触发固件升级；BLE 下发 URL 是用户主动升级路径，继续保留。
  Date/Author: 2026-04-30 / Codex

- Decision: 时间同步不复用 `MqttProtocol::SendText()`，而是在 MQTT 协议内新增 best-effort 发布路径。
  Rationale: `SendText()` 发布失败会调用 `SetError()`，而时间同步失败不应该改变协议错误状态或阻塞启动。
  Date/Author: 2026-04-30 / Codex

- Decision: MQTT 连接成功后显式订阅下行 topic，优先使用 `mqtt/subscribe_topic`，缺失时用 `client_id` 作为兼容 fallback。
  Rationale: 构建下载出 `managed_components/78__esp-ml307/include/mqtt.h` 后确认底层 `Mqtt` 提供 `Subscribe()`，但不会自动订阅。当前 BLE 示例 JSON 没有 `subscribe_topic`，而 `client_id` 是唯一设备标识，用它作为 fallback 能保留旧配置接收下行消息的机会。订阅失败只 warning，不阻塞连接。
  Date/Author: 2026-04-30 / Codex

## Outcomes & Retrospective

本轮已完成启动自动 OTA 检查和激活逻辑移除，`Ota` 类文件删除，`ota.cc` 不再参与编译，MCP 固件升级工具删除，BLE 手动升级链路保留。启动流程现在会先确认当前 OTA 分区固件有效，再做资源检查、网络连接和协议初始化。配网判断不再读取 `http/http_url`，而是要求已有 WiFi 且至少有 MQTT endpoint 或 WebSocket ws_url。MQTT 连接成功后会显式订阅下行 topic，并 best-effort 发布 `{"type":"time_sync"}`；收到合法 `server_time.timestamp` 后更新系统时钟。

完整 `idf.py build` 未通过，但失败点在既有 ESPHome/ESP32 目标配置，不在本次变更文件。已通过残留引用搜索确认旧 `Ota`/`CONFIG_OTA_URL` 入口清理完成，并用 `compile_commands.json` 单独验证了 `mqtt_protocol.cc` 与 `mcp_server.cc` 可编译。`application.cc` 与 `wifi_board.cc` 的单文件编译被当前构建中尚未生成的 NimBLE `syscfg/syscfg.h` 依赖阻塞。

## Context and Orientation

`main/application.cc` 是设备启动主流程。当前 `Application::Start()` 在资源检查和网络连接后创建 `Ota ota`，调用 `CheckNewVersion(ota)`，随后根据 `ota.HasMqttConfig()` 或 `ota.HasWebsocketConfig()` 选择协议，并使用 `ota.GetCurrentVersion()` 展示版本。`Application::CheckNewVersion(Ota& ota)` 会调用旧 OTA 接口、处理自动固件升级、激活码和服务器时间。

`main/ota.cc` 和 `main/ota.h` 定义旧 `Ota` 类。它读取 `Settings("http").GetString("http_url")` 作为检查 URL，解析旧 OTA 接口返回值，并包含 `MarkCurrentVersionValid()`。本任务要删除旧启动 OTA 检查，因此 `Ota` 类不应再参与编译。

`main/ble/ble_manager.cc` 中 `CMD_OTA_UPDATE` 会读取 BLE payload 里的固件 URL 和版本，调用 `BLEManager::otaStart()`，再进入 `Application::startOtaUpgrade()` 和 `Application::otaUpgrade()`。这条路径是用户主动 BLE 升级，必须保留。

`main/boards/common/wifi_board.cc` 的 `WifiBoard::StartNetwork()` 当前在没有 WiFi SSID 或 `http/http_url` 为空时进入配网。本任务要求改为：没有 WiFi SSID 进入配网；有 WiFi 但 MQTT `endpoint` 与 WebSocket `ws_url` 都为空也进入配网；`http/http_url` 不再影响配网。

`main/protocols/mqtt_protocol.cc` 管理 MQTT 控制通道和 UDP 音频通道。`StartMqttClient()` 读取 MQTT NVS 配置，连接 broker，并设置 `OnConnected` 与 `OnMessage`。时间同步应在 MQTT 连接成功后发送轻量 JSON；收到 `type=time_sync` 且带有 `server_time.timestamp` 后调用 `settimeofday()` 更新系统时钟，并通知 `Application` 已有服务器时间。

## Plan of Work

先在 `main/application.h` 删除 `ota.h` 依赖、`UpgradeFirmware(Ota&)`、`CheckNewVersion(Ota&)` 和无用 task handle，并新增一个公开的小方法用于 MQTT 时间同步后标记服务器时间。随后在 `main/application.cc` 删除 `CheckNewVersion()` 和 `UpgradeFirmware()`，新增独立 `MarkCurrentFirmwareValid()` 静态函数，保留 `startOtaUpgrade()` 与 `otaUpgrade()`。`Application::Start()` 在资源检查后调用固件确认，然后联网，直接读取 NVS 决定协议，版本提示改用 `esp_app_get_description()->version`。

接着修改 `main/boards/common/wifi_board.cc` 的配网判断，读 `Settings("mqtt", false).GetString("endpoint")` 和 `Settings("websocket", false).GetString("ws_url")`，不再读取 `http/http_url`。`ble_manager.cc` 的历史 `http_url` 写入保留，因为旧 App 可能还会传这个字段。

然后修改 `main/protocols/mqtt_protocol.cc/.h`，在连接成功回调中先订阅下行 topic，再发送 `{"type":"time_sync"}`。订阅 topic 优先读 `mqtt/subscribe_topic`，旧配置缺失时使用 `client_id`。发送使用不触发 `SetError()` 的 helper 发布。`OnMessage` 中在分发给应用层前识别 `type=time_sync`，解析 `server_time.timestamp` 为 UTC Unix 毫秒，转换为 `timeval` 并调用 `settimeofday()`；解析失败只 warning。成功后调用 `Application::GetInstance().SetServerTimeSynced(true)`。

最后删除 MCP 固件升级工具、从 `main/CMakeLists.txt` 移除 `ota.cc`、从 `main/Kconfig.projbuild` 和板卡 sdkconfig 清理 `CONFIG_OTA_URL`，并搜索确认无 `Ota` 类型残留引用。

## Concrete Steps

在仓库根目录执行：

    git status --short
    Get-ChildItem -Recurse -File main,CMakeLists.txt,sdkconfig,sdkconfig.old | Select-String -Pattern 'Ota|CheckNewVersion|UpgradeFirmware|CONFIG_OTA_URL|OTA_URL|http_url|server_time|time_sync'

完成代码编辑后执行：

    Get-ChildItem -Recurse -File main,CMakeLists.txt,sdkconfig* | Select-String -Pattern '\bOta\b|CheckNewVersion|UpgradeFirmware|CONFIG_OTA_URL|OTA_URL'
    idf.py build

如果本地 ESP-IDF 环境不可用，则至少运行能覆盖残留引用的搜索，并在最终说明中明确未能编译的原因。

## Validation and Acceptance

编译通过是最小验收。代码层面还需要满足以下可观察条件：启动流程不再创建 `Ota` 或调用 `CheckNewVersion()`；`main/ota.cc` 不在 `main/CMakeLists.txt` 的 `SOURCES` 中；没有 `Ota` 类型残留引用；`CONFIG_OTA_URL` 不出现在 `main/Kconfig.projbuild` 或板卡 sdkconfig；`WifiBoard::StartNetwork()` 不读取 `http/http_url`；`CMD_OTA_UPDATE` 到 `Application::otaUpgrade()` 的 BLE 手动升级链路仍存在；`MqttProtocol` 连接成功后 best-effort 发布 `{"type":"time_sync"}`，收到合法时间后调用 `settimeofday()`。

在设备上可进一步验收：清空 `http/http_url` 但保留 WiFi 与 MQTT 配置时，设备不进入配网；清空 WiFi SSID 时进入配网；保留 WiFi 但清空 MQTT endpoint 和 WebSocket ws_url 时进入配网；BLE 下发 OTA URL 后设备继续显示进度并升级重启。

## Idempotence and Recovery

所有源码编辑都是可重复的普通文本修改。删除 `Ota` 后如果编译出现未声明类型，使用残留引用搜索定位并移除，不回滚用户无关改动。`main/ota.cc/.h` 可保留在文件系统中但不参与编译；本计划目标是删除类的使用和编译入口，而不是必须删除源文件本身。若构建失败来自缺失 ESP-IDF 或托管组件下载问题，记录环境问题，不修改无关配置。

## Artifacts and Notes

初始搜索确认的关键残留点：

    main/application.cc: Ota ota; CheckNewVersion(ota); ota.HasMqttConfig(); ota.GetCurrentVersion()
    main/application.h: #include "ota.h"; bool UpgradeFirmware(Ota&...); void CheckNewVersion(Ota&...)
    main/mcp_server.cc: auto ota = std::make_unique<Ota>(); app.UpgradeFirmware(*ota, url)
    main/CMakeLists.txt: "ota.cc"
    main/Kconfig.projbuild: config OTA_URL
    main/boards/m5stack-tab5/sdkconfig.tab5: CONFIG_OTA_URL="https://api.tenclass.net/xiaozhi/ota/"

## Interfaces and Dependencies

`Application` 需要提供：

    void SetServerTimeSynced(bool synced = true);

`MqttProtocol` 需要新增私有 helper：

    bool PublishBestEffort(const std::string& text, const char* context);
    bool SubscribeDownlinkTopic();
    void SendTimeSyncRequest();
    bool HandleTimeSyncMessage(const cJSON* root);

这些 helper 不改变 `Protocol` 基类接口，也不影响 WebSocket 兼容通道。`HandleTimeSyncMessage()` 只处理 `type=time_sync`，返回 true 表示消息已消费，不再交给应用层普通 JSON 分发。

最后修订记录：新增显式 MQTT 下行订阅设计，因为构建下载出的 MQTT 封装证明底层不会自动订阅；这保证 `time_sync` 响应和后续普通下行 JSON 有明确接收路径。
