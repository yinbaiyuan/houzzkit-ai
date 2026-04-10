# 同步最新 xiaozhi server 的执行计划

本 ExecPlan 是活文档。随着工作推进，`Progress`、`Surprises & Discoveries`、`Decision Log`、`Outcomes & Retrospective` 四个章节必须保持最新。

仓库规则见 [`houzzkit-ai/.agent/PLANS.md`](houzzkit-ai/.agent/PLANS.md)。本文件必须按该规范持续维护。

## Purpose / Big Picture

目标是在不先动板级驱动和 `managed_components/` 的前提下，把本仓库与官方 `xiaozhi-esp32-server` 最新协议基线对齐，先修通“标准接入链路”，再处理 Houzzkit 自定义能力。完成后，设备至少要能正确接收 OTA 下发的 WebSocket 或 MQTT 配置，并和最新上游完成一次完整会话；随后再保证主动发声、执行命令、询问后执行三条已有能力不回归。

## Progress

- [x] 2026-04-09 11:40 +08:00 读取本仓库协议入口、状态机和文档，确认第一落点是 `main/application.cc`、`main/protocols/*`、`main/ota.cc`
- [x] 2026-04-09 11:56 +08:00 获取官方上游最新 tag，确认基线已从旧计划中的 `v0.8.11` 更新为 `v0.9.2`
- [x] 2026-04-09 12:12 +08:00 只读克隆上游 `v0.9.2` 到 `/tmp/xiaozhi-esp32-server-v0.9.2` 并完成关键协议文件核对
- [x] 2026-04-09 12:25 +08:00 产出协议差异文档 [`docs/houzzkit/xiaozhi-server-sync.md`](houzzkit-ai/docs/houzzkit/xiaozhi-server-sync.md)
- [ ] 下一步：按文档先修 `main/ota.cc` 的 OTA 配置解析与 `Settings` 写入逻辑
- [ ] 下一步：完成 WebSocket 基线联调，再开始 MQTT 基线联调
- [ ] 下一步：评估并实现 Houzzkit 自定义主动发声能力与上游标准消息之间的适配方案

## Surprises & Discoveries

- Observation: 官方上游最新 tag 已经是 `v0.9.2`，不是旧计划里写的 `v0.8.11`。
  Evidence: `git ls-remote --tags https://github.com/xinnan-tech/xiaozhi-esp32-server.git` 返回了 `refs/tags/v0.9.2`。

- Observation: 本地 [`main/ota.cc`](houzzkit-ai/main/ota.cc) 当前没有把 OTA 响应里的 `mqtt` / `websocket` 段写回 `Settings`，但协议选型又依赖这些本地配置。
  Evidence: `Ota::CheckVersion()` 里只读取 `Settings("mqtt", false)` 下的 `endpoint` 和 `ws_url`，没有对 OTA JSON 中同名对象做解析与写入。

- Observation: Houzzkit 的主动发声链路依赖 `speaker_tts`、`speaker_order`、`front_speaker_order` 三种自定义消息，而官方上游 `v0.9.2` 当前消息处理器枚举并不包含它们。
  Evidence: 上游 `core/handle/textMessageType.py` 与 `textMessageHandlerRegistry.py` 当前只注册 `hello`、`abort`、`listen`、`iot`、`mcp`、`server`、`ping`。

## Decision Log

- Decision: 以官方 `v0.9.2` 作为后续“最新 server”同步基线。
  Rationale: 这是 2026-04-09 实际查询到的最新官方 tag，继续以 `v0.8.11` 为基线会遗漏新增管理面和协议差异。
  Date/Author: 2026-04-09 / Codex

- Decision: 第一阶段只输出差异文档，不直接改协议代码。
  Rationale: 当前需要先把“兼容点”和“脱节点”钉死，避免在 WebSocket、MQTT、主动发声、远程 MCP 四条链路同时动手造成返工。
  Date/Author: 2026-04-09 / Codex

- Decision: 后续真正开始代码同步时，先修 `main/ota.cc`，再跑 WebSocket 基线，再跑 MQTT 基线。
  Rationale: `main/ota.cc` 是当前标准接入链路的总入口；如果这里不先修，后续协议联调的结论会被旧配置污染。
  Date/Author: 2026-04-09 / Codex

## Outcomes & Retrospective

当前已经完成：

- 明确了本仓库“熟悉项目并合并最新 server”的最佳入口不是 `esphome`，而是 `application + protocols + ota`
- 产出了一份仓库内可直接复用的协议差异文档
- 锁定了两个最关键阻塞项：OTA 配置注入脱节、Houzzkit 自定义主动发声消息未对齐官方上游

当前尚未完成：

- `main/ota.cc` 的修复
- WebSocket / MQTT 基线联调
- 主动发声链路适配

经验教训：

- 这类工作必须先核对“最新上游版本”再开始做方案，否则很容易围绕旧版本设计
- 对这个仓库来说，最重要的不是“协议类本身写了什么”，而是“配置从哪里进来、状态机怎样切协议、哪些消息是自定义扩展”

## Context and Orientation

本仓库是基于 ESP-IDF 的嵌入式固件项目。对“同步最新 server”这件事来说，最相关的本地模块有：

- [`main/application.cc`](houzzkit-ai/main/application.cc)：设备启动、协议选型、服务端消息分发、状态机
- [`main/protocols/websocket_protocol.cc`](houzzkit-ai/main/protocols/websocket_protocol.cc)：WebSocket 握手与音频链路
- [`main/protocols/mqtt_protocol.cc`](houzzkit-ai/main/protocols/mqtt_protocol.cc)：MQTT 控制面与 UDP 音频面
- [`main/protocols/protocol.cc`](houzzkit-ai/main/protocols/protocol.cc)：设备主动发送的标准消息与 Houzzkit 自定义扩展消息
- [`main/ota.cc`](houzzkit-ai/main/ota.cc)：OTA 响应解析与协议配置入口
- [`main/ble/ble_manager.cc`](houzzkit-ai/main/ble/ble_manager.cc)：BLE 配网后把服务地址和协议配置写入 `Settings`

这里的“协议接入面”指设备如何拿到服务地址、认证信息和主题配置，并据此建立 WebSocket 或 MQTT+UDP 连接；“控制面”指 JSON 文本消息，如 `hello`、`listen`、`abort`、`mcp`；“音频面”指二进制 Opus 音频流或 MQTT 下的 UDP 音频包。

## Plan of Work

第一步先改 [`main/ota.cc`](houzzkit-ai/main/ota.cc)。在 `Ota::CheckVersion()` 中增加对 OTA JSON 里 `websocket` 和 `mqtt` 对象的显式解析，并把字段同步写入 `Settings("websocket", true)` 与 `Settings("mqtt", true)`。这里至少要覆盖 `websocket.url/token` 和 `mqtt.endpoint/client_id/username/password/publish_topic/subscribe_topic`。同时修正 `has_mqtt_config_` 与 `has_websocket_config_` 的判定方式，使其基于刚刚解析后的结果，而不是仅读取旧配置。

第二步回到 [`main/application.cc`](houzzkit-ai/main/application.cc)，验证协议选型无需额外改动。如果 `Ota` 正确写入配置并设置标志位，那么现有逻辑应自动切到对应协议；如果发现仍存在旧值污染，再考虑在 `Application::Start()` 附近增加更明确的协议优先级日志。

第三步只做标准链路联调，不先碰 Houzzkit 的自定义主动发声。先验证 WebSocket 的 `hello`、`stt`、`tts`、`mcp`；确认没有问题后，再验证 MQTT 的 `hello`、UDP 音频、`goodbye`。在这一步里，若发现最新上游对消息字段有新增或改名，优先在协议类修正，不要先在业务层打补丁。

第四步单独处理 [`main/protocols/protocol.cc`](houzzkit-ai/main/protocols/protocol.cc) 里的自定义消息。这里要在“继续保留 `speaker_tts` / `speaker_order` / `front_speaker_order` 并修改 server”与“把这三条能力迁移到上游标准能力”之间做一次明确取舍，并把最终选择回写到本 ExecPlan。若选择设备侧迁移，则还要同步修正 [`main/application.cc`](houzzkit-ai/main/application.cc) 里对 `tts.play_start` / `play_stop` / `listen_start` 的依赖。

## Concrete Steps

工作目录统一使用仓库根目录：

    cd houzzkit-ai

本次已完成的核对命令如下，后续继续排查时可重复执行：

    sed -n '400,620p' main/application.cc
    sed -n '1,260p' main/protocols/websocket_protocol.cc
    sed -n '1,460p' main/protocols/mqtt_protocol.cc
    sed -n '1,220p' main/protocols/protocol.cc
    sed -n '1,220p' main/ota.cc
    sed -n '430,520p' main/ble/ble_manager.cc

后续正式改 `main/ota.cc` 后，至少要重复检查：

    rg -n 'GetString\\("endpoint"|GetString\\("ws_url"|SetString\\("endpoint"|SetString\\("ws_url"' main -S
    git diff -- main/ota.cc

如果要重新核对官方上游版本基线，可执行：

    git ls-remote --tags https://github.com/xinnan-tech/xiaozhi-esp32-server.git

如果要重新查看本次对照使用的上游只读副本，可执行：

    rg -n "hello|mcp|mqtt|websocket|device-id|client-id|authorization" /tmp/xiaozhi-esp32-server-v0.9.2/main/xiaozhi-server -S

## Validation and Acceptance

第一阶段文档验收标准已经完成：

- 仓库内存在 [`docs/houzzkit/xiaozhi-server-sync.md`](houzzkit-ai/docs/houzzkit/xiaozhi-server-sync.md)
- 文档明确写出最新上游基线、当前本地实现清单、协议差异矩阵和优先级
- 文档能直接指导下一阶段的代码改动顺序

第二阶段开始改代码后的验收标准如下：

1. OTA 返回 `websocket` 配置时，设备重启后能按下发的 `ws_url/ws_token` 建链。
2. OTA 返回 `mqtt` 配置时，设备重启后能按下发的 `endpoint/client_id/username/password` 建立 MQTT 连接，并完成一次 `hello` 交换。
3. WebSocket 下能完成一次完整会话：连接、监听、识别、播报、关闭。
4. MQTT+UDP 下能完成一次完整会话：连接、监听、识别、播报、关闭。
5. 在最终同步完成前，`播放语音`、`执行命令`、`询问后执行命令` 三条现有能力不能静默失效。

## Idempotence and Recovery

本次新增的对照文档和 ExecPlan 都是纯文本文件，可安全重复编辑。用于比对的上游仓库位于 `/tmp/xiaozhi-esp32-server-v0.9.2`，属于临时只读工作副本，后续不再需要时可手动删除，不影响本仓库状态。

后续改 `main/ota.cc` 时，应保持增量修改，先只引入 OTA 配置解析和 `Settings` 写入，不要在同一轮同时改 `Application` 状态机和主动发声协议，这样回归时更容易定位问题。如果联调中发现协议切换异常，可先通过 BLE 配网路径重新写入 `Settings`，用来区分“OTA 注入失败”和“协议实现失败”。

## Artifacts and Notes

本次最关键的证据包括：

    最新上游 tag:
    refs/tags/v0.9.2

    本地关键问题:
    main/ota.cc 当前只用 Settings("mqtt") 里的 endpoint / ws_url 判断是否有协议配置，
    没有把 OTA JSON 返回的 websocket / mqtt 段落盘

    上游标准消息枚举:
    hello / abort / listen / iot / mcp / server / ping

    本地 Houzzkit 自定义消息:
    speaker_tts / speaker_order / front_speaker_order

## Interfaces and Dependencies

后续真正开始同步时，至少要保证以下接口关系不被破坏：

- [`main/protocols/protocol.h`](houzzkit-ai/main/protocols/protocol.h) 中 `Protocol` 作为统一抽象不能被破坏，`Application` 必须继续只依赖该抽象
- [`main/protocols/websocket_protocol.cc`](houzzkit-ai/main/protocols/websocket_protocol.cc) 继续负责 WebSocket 请求头、`hello` 和二进制音频协议
- [`main/protocols/mqtt_protocol.cc`](houzzkit-ai/main/protocols/mqtt_protocol.cc) 继续负责 MQTT 控制面和 UDP 音频面
- [`main/ota.cc`](houzzkit-ai/main/ota.cc) 必须成为最新 server 协议配置的标准注入入口，而不是仅依赖旧本地设置

本次文档改动内容：

- 新增仓库内对照文档，沉淀“本地实现 vs 官方 `v0.9.2`”差异
- 新增活文档 ExecPlan，为下一轮真正代码同步提供接力材料

本次文档改动原因：

- 用户明确希望“熟悉项目并合并最新 xiaozhi server”，当前最先需要的是把最新协议差异钉死，避免围绕旧版本或错误入口开始改代码
