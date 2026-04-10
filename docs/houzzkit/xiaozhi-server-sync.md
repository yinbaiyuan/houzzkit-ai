# xiaozhi server 联合对比文档

本文档用于统一对比三套基线：

- 当前版本 `ESP32 固件 + ai-server` 的现状协议
- 官方最新 `xiaozhi-esp32-server v0.9.2` 的标准协议
- 如果采用`双轨并存`方案，固件侧和 server 侧分别需要补哪些改动

本文只做差异梳理，不直接进入代码实现细节，目标是让后续实现者能从一份文档里回答四个问题：

- 当前版本怎么做
- 官方怎么做
- 要不要改
- 谁来改

## 1. 当前基线说明

### 1.1 固件侧基线

本仓库中，和 server 协议耦合最深的入口主要在以下几处：

- [`main/application.cc`](houzzkit-ai/main/application.cc)
- [`main/protocols/protocol.cc`](houzzkit-ai/main/protocols/protocol.cc)
- [`main/protocols/websocket_protocol.cc`](houzzkit-ai/main/protocols/websocket_protocol.cc)
- [`main/protocols/mqtt_protocol.cc`](houzzkit-ai/main/protocols/mqtt_protocol.cc)
- [`main/ota.cc`](houzzkit-ai/main/ota.cc)

当前固件的关键事实如下：

- 启动时由 `Application::Start()` 选择 `WebSocket` 或 `MQTT + UDP` 协议。
- 设备会主动发送 `hello`、`listen`、`abort`、`mcp`，以及 Houzzkit 私有消息 `speaker_tts`、`speaker_order`、`front_speaker_order`。
- 设备当前能消费的服务端主消息包括 `tts`、`stt`、`llm`、`mcp`、`system`、`alert`、`custom`。
- 设备当前对 `tts.state` 的本地依赖不止标准状态，还包含 `play_start`、`play_stop`、`listen_start` 这三类 Houzzkit 扩展状态。
- `main/ota.cc` 仍是关键薄弱点，当前 OTA 下发的 `websocket` / `mqtt` 配置没有完整写回本地 `Settings`。

### 1.2 二开 server 基线

本次对比默认以 `ai-server` 为目标二开 server 仓库。

它在 README 里明确标注基于：

- [`ai-server/README.md`](ai-server/README.md)
- `xiaozhi-esp32-server@53479f`

也就是说，当前版本 `ESP32 + ai-server` 更接近“老上游基线 + Houzzkit 私有扩展”，而不是官方最新 `v0.9.2` 的原样实现。

当前 `ai-server` 里和协议直接相关的关键入口主要是：

- [`core/handle/helloHandle.py`](ai-server/core/handle/helloHandle.py)
- [`core/handle/textHandle.py`](ai-server/core/handle/textHandle.py)
- [`core/handle/sendAudioHandle.py`](ai-server/core/handle/sendAudioHandle.py)
- [`core/websocket_server.py`](ai-server/core/websocket_server.py)
- [`core/connection.py`](ai-server/core/connection.py)
- [`config/config_default.yaml`](ai-server/config/config_default.yaml)

当前 `ai-server` 的关键事实如下：

- WebSocket 主入口是 `/ws`，不是官方默认 OTA 常下发的 `/xiaozhi/v1/` 风格路径。
- `hello` 处理目前主要是读取 `features.daec`，然后直接返回 `conn.welcome_msg`。
- 文本消息处理除了标准 `hello`、`listen`、`abort`、`mcp`、`iot` 之外，还直接处理 `speaker_tts`、`speaker_order`、`front_speaker_order`。
- TTS 发送侧会输出 `play_start`、`play_stop`、`listen_start`、`sentence_start`、`sentence_end` 等 Houzzkit 依赖状态。
- 配置层仍以本地 `server.auth.tokens` 和业务侧配置为主，不是官方最新 manager-api 驱动的完整配置模型。

### 1.3 官方基线

本次对齐目标按官方仓库 tag 取最新可见标签：

- 仓库：`https://github.com/xinnan-tech/xiaozhi-esp32-server.git`
- 核对时间：`2026-04-09`
- 只读核对命令：`git ls-remote --tags https://github.com/xinnan-tech/xiaozhi-esp32-server.git`
- 最新 tag：`v0.9.2`
- 对应提交：`2ac7e5be9ce91addb5b9cac0f176908b57a7ed64`

本次文档对官方方案的只读核对主要基于以下路径：

- `/tmp/xiaozhi-server-v092/main/xiaozhi-server/core/websocket_server.py`
- `/tmp/xiaozhi-server-v092/main/xiaozhi-server/core/handle/helloHandle.py`
- `/tmp/xiaozhi-server-v092/main/xiaozhi-server/core/handle/textMessageHandlerRegistry.py`
- `/tmp/xiaozhi-server-v092/main/xiaozhi-server/core/api/ota_handler.py`
- `/tmp/xiaozhi-server-v092/docs/mqtt-gateway-integration.md`
- `/tmp/xiaozhi-server-v092/main/manager-api/src/main/java/xiaozhi/modules/device/service/impl/DeviceServiceImpl.java`

## 2. 官方最新基线说明

官方 `v0.9.2` 标准面，可以归纳为下面几条：

- WebSocket 认证使用 `Device-Id`、`Client-Id`、`Authorization: Bearer <token>` 三元组。
- OTA 会下发 `websocket` 或 `mqtt` 接入信息，设备应按 OTA 响应切换协议。
- WebSocket 文本消息采用注册表机制，标准类型至少包括：
  - `hello`
  - `abort`
  - `listen`
  - `iot`
  - `mcp`
  - `server`
  - `ping`
- `hello` 会读取客户端的 `audio_params` 和 `features`，若启用 `features.mcp` 会初始化设备侧 MCP。
- MQTT 模式依赖 OTA 下发 `endpoint`、`client_id`、`username`、`password`、`publish_topic`、`subscribe_topic`。
- MQTT 音频面走 UDP，服务端和网关之间使用固定 16 字节头的 Opus 包格式。
- manager-api 已经纳入设备在线状态、远程命令下发、远程 MCP 工具调用等管理面能力。

## 3. 固件侧差异矩阵

| 对照维度 | 当前版本固件 | 官方 `v0.9.2` | 要不要改 | 谁改 | 说明 |
| --- | --- | --- | --- | --- | --- |
| 握手头 | 已发送 `Authorization`、`Protocol-Version`、`Device-Id`、`Client-Id` | 标准要求 `Authorization`、`Device-Id`、`Client-Id` | 暂不必改 | 固件 | 当前多发 `Protocol-Version` 不构成阻塞 |
| `hello` 请求字段 | 已带 `version`、`features`、`transport`、`audio_params` | 标准 `hello` 也读取这些字段 | 暂不必改 | 固件 | WebSocket 标准面基本兼容 |
| `hello` 响应消费 | 读取 `session_id`、`audio_params` | 标准响应会回这些字段 | 暂不必改 | 固件 | 消费面基本兼容 |
| WebSocket 消息类型 | 主动发送 `hello`、`listen`、`abort`、`mcp`、`speaker_tts`、`speaker_order`、`front_speaker_order` | 标准只认 `hello`、`listen`、`abort`、`iot`、`mcp`、`server`、`ping` | 必改 | 固件 + server | 私有三类消息不是官方标准面 |
| `tts.state` 消费 | 依赖 `start`、`stop`、`sentence_start`、`play_start`、`play_stop`、`listen_start` | 标准面核心可确认的是 `start`、`sentence_start`、`stop` | 必改 | 固件 + server | 双轨并存时必须把 legacy 扩展状态隔离出来 |
| WebSocket 二进制音频 | 支持版本 `1/2/3` | 官方主链路可兼容标准音频流 | 暂不必改 | 固件 | 联调时先以默认版本验证 |
| OTA 下发 `websocket` | 当前 `main/ota.cc` 未完整写回 `ws_url/ws_token` | 官方 OTA 明确会下发 `websocket.url/token` | 必改 | 固件 | 这是当前接入最新 server 的首要阻塞点 |
| OTA 下发 `mqtt` | 当前 `main/ota.cc` 未完整写回 `mqtt` 段 | 官方 OTA 明确会下发 MQTT 连接参数 | 必改 | 固件 | 没有这步就无法跟随 OTA 切 MQTT |
| MQTT `hello` 响应消费 | 已按 `udp.server/port/key/nonce` 等字段解析 | 官方 MQTT 网关就是这套结构 | 暂不必改 | 固件 | 消费面基本兼容，问题在配置注入 |
| MCP 封装 | 外层 `type: "mcp"` 包裹 JSON-RPC `payload` | 官方也是同一路径 | 暂不必改 | 固件 | 基础封装兼容 |
| 远程设备工具调用 | 设备侧已有 `mcp` 与工具注册基础 | 官方 manager-api 可通过 MQTT 网关触发 `tools/list` / `tools/call` | 可后补 | 固件 + server | 先跑通基础会话后再补联调 |
| 在线状态 | 当前固件没有额外差异处理 | 官方已有关联 MQTT 在线状态与管理 API | 可后补 | server 为主 | 主要受 server 和网关配置影响 |
| `server` / `iot` / `ping` 入站 | 当前 `Application` 未完整对齐这几类 | 官方已作为标准消息类型注册 | 可后补 | 固件 | 不是首阶段会话阻塞项 |

## 4. `ai-server` 侧差异矩阵

| 对照维度 | 当前 `ai-server` | 官方 `v0.9.2` | 要不要改 | 谁改 | 说明 |
| --- | --- | --- | --- | --- | --- |
| WebSocket 接入路径 | 主入口是 `/ws` | OTA 默认常下发 `/xiaozhi/v1/` 风格地址 | 必改 | server | 双轨并存时至少要新增官方协议入口，不能只保留 `/ws` |
| WebSocket 接入框架 | `aiohttp` 路由式多入口，含 `/ws`、`/mcp/ws`、`/hos-mcp/ws`、`/asr/ws`、`/tts/ws` | 官方主链路是标准 websocket server，结合 OTA / manager-api 配置 | 必改 | server | 不要求迁框架，但要补标准协议入口 |
| 鉴权模型 | 以 `server.auth.tokens` 和业务态鉴权为主 | 官方使用 `auth_key` 生成/校验 `client_id + device_id + token` | 必改 | server | 需要新增官方兼容鉴权，而不是替换当前版本鉴权 |
| `helloHandle.py` | 只处理 `features.daec`，随后回 `welcome_msg` | 官方 `hello` 会读取 `audio_params`、`features.mcp`，必要时初始化 MCP | 必改 | server | 官方协议设备接入时必须补齐标准 `hello` 能力 |
| `textHandle.py` | 直接处理 `speaker_tts`、`speaker_order`、`front_speaker_order` | 官方标准处理器只注册 `hello`、`abort`、`listen`、`iot`、`mcp`、`server`、`ping` | 必改 | server | 需要拆成 legacy 与官方两套入口，而不是混写在同一标准面 |
| `sendAudioHandle.py` 状态输出 | 会发 `play_start`、`play_stop`、`listen_start`、`sentence_end` | 官方以标准 `start`、`sentence_start`、`stop` 为核心 | 必改 | server | 标准链路必须停止依赖 Houzzkit 扩展状态 |
| MQTT 音频头 | 已实现 16 字节头部并兼容 MQTT 网关 | 官方也采用 16 字节头部 | 暂不必改 | server | 数据面兼容性较好 |
| OTA 配置模型 | 当前更偏本地配置和业务定制 | 官方强依赖 OTA 下发 `websocket` / `mqtt` 参数 | 必改 | server | 若继续双轨并存，server 要能明确下发 legacy 或官方配置 |
| `connection.py` 会话上下文 | 绑定了 Houzzkit 业务对象、设备控制、前置播报状态机等 | 官方连接态更聚焦标准会话、认证、工具和音频控制 | 必改 | server | 需要增加“协议模式识别 + 上下文隔离”，避免新旧逻辑串线 |
| MCP 调用 | 当前版本已有自定义业务整合和 `tools/list` 使用 | 官方已标准化 `mcp`、设备工具和远程工具调用 | 可后补 | server | 基础可复用，但需要标准链路映射 |
| 在线状态与远程控制 | 当前版本未按官方管理面完全展开 | 官方 manager-api 已支持在线状态、远程命令、远程工具调用 | 可后补 | server | 适合放到标准链路接通后补齐 |
| `server` / `ping` 消息 | 当前主链路未见完整官方对齐 | 官方已注册并可处理配置更新与心跳 | 可后补 | server | 不影响第一轮建链和会话，但影响运维能力 |

## 5. Server 必改项与可后补项

### 5.1 必改项

这些项不补，设备要么连不上，要么连上后无法完成标准会话：

- 新增官方协议入口，至少支持 OTA 下发可达的标准 WebSocket 地址。
- 新增官方兼容鉴权，能正确校验 `Device-Id`、`Client-Id`、`Authorization`。
- 补齐 `helloHandle.py` 的标准处理逻辑，至少正确处理 `audio_params` 与 `features.mcp`。
- 在 `textHandle.py` 或新的协议适配层中，把 legacy 私有消息和官方标准消息拆开。
- 在 `sendAudioHandle.py` 中把 legacy `tts` 状态和官方标准状态拆开输出。
- 配置层补齐官方所需的 `websocket` / `mqtt` / 鉴权参数下发契约。
- 为 `connection.py` 增加协议模式识别，避免 legacy 会话状态机污染官方连接。

### 5.2 可后补项

这些项不影响第一轮建链和基础会话，但会影响联调深度、运营或远程管理：

- 在线状态和 MQTT 管理面联调
- 远程 `tools/list` / `tools/call`
- `server` 配置更新与重启控制
- `ping` / `pong` 心跳
- `iot` 全量状态同步
- 基于官方标准面重做 Houzzkit 业务映射后的灰度管理能力

## 6. Houzzkit 私有能力映射表

| Houzzkit 私有协议/状态 | 当前作用 | 官方标准链路里的建议表达 | 能否直接映射 | 备注 |
| --- | --- | --- | --- | --- |
| `speaker_tts` | 直接让设备播报指定文本 | server 侧改为创建一轮标准 `tts` 输出任务 | 部分可直映 | 消息名不应继续暴露给官方协议设备 |
| `speaker_order` | 直接提交文字指令并执行 | 走标准会话入口，相当于一次文本版 `listen.detect` 或标准请求入口 | 部分可直映 | 需要 server 业务层接管，而不是设备继续发私有类型 |
| `front_speaker_order` | 先播提示，再继续监听等待回答 | 拆成标准 `tts` 播报 + 播完后切回标准 `listen.start` | 不能直接映射 | 需要 server 侧重构现有前置播报状态机 |
| `play_start` | 通知设备开始进入“主动播报播放态” | 官方模式下尽量收敛为标准 `tts.start` | 可替代 | 仅 legacy 保留原状态名 |
| `play_stop` | 通知设备主动播报播放结束 | 官方模式下收敛为标准 `tts.stop` | 可替代 | 若还要区分“普通 stop / 主动播报 stop”，应由业务上下文处理 |
| `listen_start` | 前置播报结束后，通知设备立即继续收听 | 官方模式下改为标准 `listen.start` 控制 | 不能直接映射 | 需要标准链路的“播完继续听”编排能力 |

结论很明确：

- `speaker_tts`
- `speaker_order`
- `front_speaker_order`
- `play_start`
- `play_stop`
- `listen_start`

这六项都不应被视为官方标准协议的一部分。双轨并存时，它们继续留在 legacy 轨道，官方轨道只保留等价能力，不保留原消息名。

## 7. 双轨并存方案

推荐把合并策略固定为两条轨道：

- `legacy` 轨道
- `官方` 轨道

### 7.1 `legacy` 轨道

保留当前版本协议和业务行为，不改变以下事实：

- 固件仍可发送 `speaker_tts`、`speaker_order`、`front_speaker_order`
- `ai-server` 仍可发送 `play_start`、`play_stop`、`listen_start`
- 当前版本设备不需要立即升级
- 当前版本业务入口、HA 文本实体能力、定制前置播报逻辑全部不回归

### 7.2 `官方` 轨道

新增一套标准链路，只面向灰度设备或后续新固件：

- 固件只发标准 `hello`、`listen`、`abort`、`mcp`
- server 只回标准 `tts`、`stt`、`llm`、`mcp`、`system`、`server`、`ping`
- `hello`、鉴权、OTA、MQTT 配置、管理面行为尽量向官方 `v0.9.2` 靠拢
- Houzzkit 私有业务能力通过 server 业务层映射到标准会话和标准控制动作，不再复用旧消息名

### 7.3 双轨判断点

文档先固定实现思路，不留实现者继续拍脑袋：

- OTA 下发里显式带协议模式，或由下发地址与鉴权模型隐式判定。
- server 连接入口在建链时就识别协议模式，不要等消息进来再猜。
- 一条连接从建链开始到断开，只属于一种协议模式，不允许混跑。

## 8. 分阶段实施建议

### 阶段 1：打通配置与标准接入

- 固件补齐 OTA `websocket` / `mqtt` 写回。
- `ai-server` 新增官方协议入口和官方兼容鉴权。
- 先只跑通 `hello`、`listen`、`abort`、`tts`、`stt`、`mcp` 标准链路。

### 阶段 2：补齐标准音频与 MQTT

- 跑通官方 WebSocket 标准会话。
- 跑通官方 MQTT `hello`、UDP 音频与结束会话。
- 确认 legacy 与官方可同时连接同一套 `ai-server`。

### 阶段 3：迁移 Houzzkit 业务能力

- 将主动播报、执行命令、询问后执行映射到官方标准能力。
- 只在 legacy 轨道继续保留私有消息名。
- 验证标准轨道下这三类能力至少具备等价体验。

### 阶段 4：补运维与管理面

- 在线状态
- 远程命令
- 远程 MCP
- `server` 配置更新
- `ping` 心跳

## 9. 风险与回滚点

### 9.1 主要风险

- OTA 配置一旦改错，设备可能直接切到错误协议。
- 如果 server 只做“统一处理”而不做双轨隔离，legacy 当前版本能力最容易回归。
- `front_speaker_order` 这类带状态机的能力，最容易在标准化过程中丢失细节。
- MQTT 管理面一旦半接通，可能出现“设备能通话但管理台看不到状态”的割裂问题。

### 9.2 回滚点

- 固件侧保留 legacy 为默认协议模式。
- server 侧在新增官方协议入口前，不替换现有 `/ws` 主路径。
- 所有灰度先从 WebSocket 开始，不先切 MQTT。
- Houzzkit 私有能力在 官方等价实现稳定前，不从 legacy 轨道删除。

## 10. 联调检查表

### 10.1 文档验收标准

- 你能从本文件同时看清固件侧和 server 侧的全部差异。
- 每一项差异都能回答“当前版本怎么做、官方怎么做、要不要改、谁改”。
- 后续可以直接据此写实现 ExecPlan，而不需要重新做关键决策。

### 10.2 联调场景

- `legacy 固件 + legacy server`
- `官方协议固件 + 官方协议入口`
- `同一套 ai-server 同时服务 legacy 与官方两类设备`
- `WebSocket` 链路
- `MQTT + UDP` 链路
- `MCP tools/list` 与 `tools/call`
- Houzzkit 三类私有能力不回归

### 10.3 联调顺序

建议严格按下面顺序推进：

1. legacy 当前版本链路回归确认
2. 官方 WebSocket 建链成功
3. 官方 `hello` 往返成功
4. 官方 `stt`、`tts`、`mcp` 正常
5. 官方 OTA 下发 MQTT 配置成功
6. 官方 MQTT `hello` 返回 `udp.server`、`port`、`key`、`nonce`
7. UDP 音频正常收发
8. 同一套 server 双轨共存稳定
9. 远程工具调用与在线状态联调
10. Houzzkit 三类私有能力在 legacy 零回归，在官方轨道至少有等价实现

## 11. 结论

这次“合并最新官方 xiaozhi 方案”如果只看固件，会低估工作量；如果只看 server，也会误判风险点。

正确的理解方式是：

- 这不是单纯的“把固件接到新 server”
- 也不是单纯的“把 server 升级到新 tag”
- 而是一次`当前版本 legacy 协议`与`官方 v0.9.2 标准协议`的双端对齐工作

对当前项目来说，最先该做的不是删掉 Houzzkit 私有协议，而是先建立一条可灰度、可回滚、可并存的官方标准链路。
