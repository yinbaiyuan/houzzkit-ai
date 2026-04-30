# MQTT 请求失败细分错误执行计划

本 ExecPlan 是活文档。随着工作推进，`Progress`、`Surprises & Discoveries`、`Decision Log`、`Outcomes & Retrospective` 四个章节必须保持最新。本文件遵循仓库根目录 `.agent/PLANS.md` 的规则。

## Purpose / Big Picture

当前 MQTT 请求失败常常只显示“等待响应超时”或“无法连接服务”，现场用户和客服很难判断是网络、配置、鉴权、服务响应还是音频通道问题。本次改动要让设备屏幕显示通俗中文提示，并带稳定英文错误码，例如“服务拒绝连接（AUTH_FAILED）”。用户不需要理解 MQTT、UDP 或 hello，研发可以根据括号里的错误码和串口日志快速定位。

## Progress

- [x] (2026-04-30 00:00Z) 阅读 `.agent/PLANS.md`，确认复杂跨模块实现需要维护 ExecPlan。
- [x] (2026-04-30 00:00Z) 读取 MQTT 协议、板级网络抽象、语言资源与底层 MQTT 实现，确认需要修改主工程和 `managed_components/78__esp-ml307` 的 MQTT 抽象。
- [x] (2026-04-30 00:00Z) 增加 `Board::IsNetworkReady()` 并在 Wi-Fi、ML307、双网络公共板基类实现。
- [x] (2026-04-30 00:00Z) 扩展 `Mqtt` 接口，记录最近连接错误枚举和原始错误信息。
- [x] (2026-04-30 00:00Z) 在 ESP、ML307、EC801E MQTT 实现中设置连接错误原因。
- [x] (2026-04-30 00:00Z) 重构 `MqttProtocol` 的连接、订阅、会话等待、服务响应校验和音频通道打开错误分流。
- [x] (2026-04-30 00:00Z) 更新 zh-CN/en-US 语言源和生成后的 `lang_config.h`。
- [x] (2026-04-30 00:00Z) 运行语言生成脚本，确认 `main/assets/lang_config.h` 包含新增错误文案。
- [x] (2026-04-30 00:00Z) 尝试运行目标构建；本机 `idf.py` 未进入 PATH，ESP-IDF Python 环境也缺失，完整构建无法完成。
- [x] (2026-04-30 00:00Z) 使用 `build/compile_commands.json` 对本次受影响的 7 个 C++ 源文件执行交叉编译器语法检查，全部通过。
- [x] (2026-04-30 00:00Z) 修正服务响应校验失败前提前写入会话状态的问题，并重新对 `mqtt_protocol.cc` 执行语法检查通过。
- [x] (2026-04-30 00:00Z) 按产品判断将音频发送失败改回静默失败，移除对应用户可见文案和计数提示逻辑。

## Surprises & Discoveries

- Observation: 当前目录包含 `.git`，但 `git status` 在工具环境中返回“not a git repository”。实现过程不能依赖 git diff 作为唯一变更追踪。
  Evidence: `git status --short` 输出 `fatal: not a git repository (or any of the parent directories): .git`，但 `Get-ChildItem -Force` 能看到 `.git` 目录。
- Observation: 4G/ML307 的域名解析由模组内部 AT 命令完成，主控侧系统 DNS 预检查不一定可用。
  Evidence: `Ml307Udp::Connect` 和 `Ml307Mqtt::Connect` 直接向模组发送域名字符串，等待模组 URC 结果。
- Observation: `managed_components/` 和 `main/assets/lang_config.h` 被仓库 `.gitignore` 忽略，常规 `git status` 不会显示这两处改动。
  Evidence: `git check-ignore -v managed_components/78__esp-ml307/include/mqtt.h` 命中 `.gitignore:2:managed_components/`；`git check-ignore -v main/assets/lang_config.h` 命中 `.gitignore:11:main/assets/lang_config.h`。
- Observation: 本机无法完成计划里的完整 `idf.py -DBOARD_NAME=kevin-sp-v3-dev build`。
  Evidence: 直接运行 `idf.py` 返回命令不存在；加载 ESP-IDF 5.5 export 后又提示 `D:\esp\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe` 不存在。

## Decision Log

- Decision: Wi-Fi 路径用主控侧 DNS 预检查；ML307/EC801E 通过底层连接结果透传 DNS 失败，不在主控侧强行预解析。
  Rationale: 避免在蜂窝网络设备上因为主控没有联网 DNS 而误报 `DNS_RESOLVE_FAILED`。
  Date/Author: 2026-04-30 / Codex
- Decision: 用户提示保持通俗中文，错误码保持英文大写并稳定不翻译。
  Rationale: 屏幕提示面向用户，括号错误码面向客服和研发定位。
  Date/Author: 2026-04-30 / Codex

## Outcomes & Retrospective

已完成 MQTT 请求失败细分错误的主链路实现：板级网络可用性检查、底层 MQTT 连接错误透传、上层 MQTT 请求分阶段错误映射、服务响应严格校验、音频通道打开错误分流，以及 zh-CN/en-US 文案补齐。音频发送失败保持静默返回失败，不作为用户可见错误。

验证结果：完整 ESP-IDF 构建受本机环境限制未完成；已用交叉编译器对受影响的 7 个 C++ 源文件执行 `-fsyntax-only` 检查并通过，最后一次服务响应状态写入修正后也重新检查了 `mqtt_protocol.cc`。后续在已安装 ESP-IDF Python 环境的机器上仍需执行 `idf.py -DBOARD_NAME=kevin-sp-v3-dev build` 和真机链路回归。

## Context and Orientation

主工程 MQTT 协议位于 `main/protocols/mqtt_protocol.cc` 和 `main/protocols/mqtt_protocol.h`。它通过 `Board::GetInstance().GetNetwork()->CreateMqtt()` 创建底层 MQTT 客户端，通过 `SendText()` 发送 JSON，通过等待事件位接收服务端会话响应，然后创建 UDP 音频通道。

板级网络抽象在 `main/boards/common/board.h`，Wi-Fi 公共基类在 `main/boards/common/wifi_board.*`，4G/ML307 公共基类在 `main/boards/common/ml307_board.*`，双网络委托类在 `main/boards/common/dual_network_board.*`。

底层 MQTT 抽象在 `managed_components/78__esp-ml307/include/mqtt.h`，具体实现包括 `src/esp/esp_mqtt.*`、`src/ml307/ml307_mqtt.*` 和 `src/ec801e/ec801e_mqtt.*`。这些实现当前只返回 `Connect()` 的 bool，不能稳定把拒绝原因传给上层，因此要做最小错误原因透传。

语言资源源文件位于 `main/assets/locales/zh-CN/language.json` 和 `main/assets/locales/en-US/language.json`，生成头文件为 `main/assets/lang_config.h`。CMake 构建时会调用 `scripts/gen_lang.py` 生成头文件。

## Plan of Work

先增加网络状态和 MQTT 错误枚举接口，使上层能获取底层连接失败原因。然后重构 `MqttProtocol`，把请求链路拆成网络检查、配置校验、DNS 检查、连接服务、订阅下行、发送会话请求、等待响应、校验响应、打开音频通道和发送音频几个阶段。每个阶段只显示用户可理解的错误提示，同时在日志里保留 MQTT、UDP、字段名、底层错误码等技术信息。

语言资源要先改 JSON，再运行 `scripts/gen_lang.py --language zh-CN --output main/assets/lang_config.h`，保证生成头和源资源一致。

## Concrete Steps

在仓库根目录 `D:\houzzkit\houzzkit-ai-firmware` 执行实现。主要编辑文件为：

- `main/boards/common/board.h`
- `main/boards/common/wifi_board.h` 与 `.cc`
- `main/boards/common/ml307_board.h` 与 `.cc`
- `main/boards/common/dual_network_board.h` 与 `.cc`
- `managed_components/78__esp-ml307/include/mqtt.h`
- `managed_components/78__esp-ml307/src/*/*mqtt.*`
- `main/protocols/mqtt_protocol.h` 与 `.cc`
- `main/assets/locales/zh-CN/language.json`
- `main/assets/locales/en-US/language.json`
- `main/assets/lang_config.h`

构建验证命令：

    idf.py -DBOARD_NAME=kevin-sp-v3-dev build

若本地没有 ESP-IDF 环境，则记录无法运行的原因，并至少完成静态编译风险检查。

当前环境实际结果：`idf.py` 未进入 PATH；尝试加载 ESP-IDF 5.5 后，Python 虚拟环境缺失。已退而使用既有 `build/compile_commands.json` 对以下源文件做交叉编译器语法检查：

- `main/protocols/mqtt_protocol.cc`
- `main/boards/common/wifi_board.cc`
- `main/boards/common/ml307_board.cc`
- `main/boards/common/dual_network_board.cc`
- `managed_components/78__esp-ml307/src/esp/esp_mqtt.cc`
- `managed_components/78__esp-ml307/src/ml307/ml307_mqtt.cc`
- `managed_components/78__esp-ml307/src/ec801e/ec801e_mqtt.cc`

## Validation and Acceptance

成功行为应包括：网络断开时显示 `网络断开了（NETWORK_DISCONNECTED）`；鉴权错误可显示 `服务拒绝连接（AUTH_FAILED）`；协议拒绝可显示 `服务拒绝连接（PROTOCOL_REJECTED）`；client_id 被拒绝可显示 `服务拒绝连接（CLIENT_ID_REJECTED）`；订阅下行失败显示 `接收服务回复失败（REPLY_CHANNEL_FAILED）`；服务响应结构错误显示 `服务返回的数据不对（SERVICE_DATA_ERROR）`；正常 MQTT 请求仍能连接、订阅、发送会话建立请求、收到响应并打开音频通道。

## Idempotence and Recovery

所有代码修改都通过增量补丁完成，可以重复运行语言生成脚本。若构建失败，优先根据编译器报错修正接口签名、include 或枚举映射。若底层实现无法稳定识别细分原因，保留通用错误码并在日志输出原始原因。

## Artifacts and Notes

新增用户可见错误码覆盖：

- `NETWORK_DISCONNECTED`
- `SERVICE_CONFIG_ERROR`
- `DNS_RESOLVE_FAILED`
- `SERVICE_CONNECT_TIMEOUT`
- `SERVICE_CONNECT_FAILED`
- `AUTH_FAILED`
- `PROTOCOL_REJECTED`
- `CLIENT_ID_REJECTED`
- `SERVICE_REJECTED`
- `REPLY_CHANNEL_FAILED`
- `REQUEST_SEND_FAILED`
- `SERVICE_REPLY_TIMEOUT`
- `HANDSHAKE_BREAK`
- `SERVICE_CONNECT_ERROR`
- `SERVICE_DATA_ERROR`
- `AUDIO_CHANNEL_OPEN_FAILED`
注意：`managed_components/` 与 `main/assets/lang_config.h` 为忽略路径。若后续需要提交这部分改动，必须确认团队提交策略，必要时使用强制添加或把改动同步到组件来源。

## Interfaces and Dependencies

`Board` 新增：

    virtual bool IsNetworkReady() = 0;

`Mqtt` 新增错误枚举和读取接口，命名保持轻量稳定：

    enum class MqttConnectError { None, Timeout, Failed, DnsFailed, Rejected, AuthFailed, ProtocolRejected, ClientIdRejected };
    MqttConnectError LastConnectError() const;
    const std::string& LastConnectErrorMessage() const;

`MqttProtocol` 内部新增会话失败原因枚举，用固定枚举跨任务传递用户提示，避免共享裸字符串带来的数据竞争。
