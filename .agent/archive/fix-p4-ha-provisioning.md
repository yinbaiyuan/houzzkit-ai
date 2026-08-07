# 修复 ESP32-P4 Home Assistant 蓝牙配置链路

本 ExecPlan 是活文档。随着工作推进，`Progress`、`Surprises & Discoveries`、`Decision Log`、`Outcomes & Retrospective` 四个章节必须保持最新。

本文件必须按照仓库根目录 `.agent/PLANS.md` 的规则持续维护；完成本轮代码与静态验收后移动到 `.agent/archive/`。

## Purpose / Big Picture

Waveshare P4 86 Panel 已能通过微信小程序配置 Wi-Fi 和云端协议，但设备重启上线后处理 Home Assistant 配置命令时会破坏内部堆并重启。本次修改让 P4 与现有 ESP32-S3 Wi-Fi 板型保持一致：正常启动 ESPHome API Server，接收 Noise PSK，并向 Home Assistant 注册；同时把耗时 HTTP 请求移出 NimBLE 写回调，避免蓝牙协议栈任务同步承担 DNS、TCP 和 HTTP。完成后，小程序发送 CMD 20 时能够收到真实注册结果，BLE 任务不会被网络请求阻塞。

## Progress

- [x] (2026-08-07 08:30Z) 从已提交的 P4 迁移分支创建 `codex/fix-p4-ha-provisioning`。
- [x] (2026-08-07 09:12Z) 恢复 P4 ESPHome API Server，并对齐 P4 NimBLE 资源配置。
- [x] (2026-08-07 09:18Z) 将 CMD 20 改为独立工作任务并增加协议边界校验。
- [x] (2026-08-07 09:21Z) 运行 `git diff --check` 并用当前 P4 构建配置单独编译三个受影响核心对象，复核差异并归档本计划。
- [x] (2026-08-07 09:23Z) 提交修复分支；完整构建与实机验证按用户后续指令执行。

## Surprises & Discoveries

- Observation: ESP32-S3 与 P4 使用同一份 CMD 20 业务代码，但运行配置不同。
  Evidence: `sdkconfig.defaults.esp32s3` 为 NimBLE Host 配置 8192 字节栈、单连接、单 GATT 过程并使用外部内存；当前 P4 实际配置为 4096 字节栈、三连接、四 GATT 过程并使用内部 SRAM。
- Observation: 崩溃不是内存耗尽，而是内部堆元数据被破坏后在新建 TCP 连接时被发现。
  Evidence: 实机日志在尚有约 82 KB SRAM 和 28 MB PSRAM 时触发 `insert_free_block` 断言；匹配固件 ELF 后，栈地址解析为 `heap_caps_malloc_prefer -> mem_malloc -> tcp_create_segment -> tcp_connect`。
- Observation: 受影响的 BLE、协议解析和 ESPHome 源文件可以在现有 P4 构建目录独立编译通过。
  Evidence: `ninja -C build` 编译 `ble_manager.cc.obj`、`proto_parse.cc.obj` 和 `esphome_device.cc.obj` 退出码为 0；仅出现已有的 `NimBLEService::start()` 弃用警告。

## Decision Log

- Decision: 不新增 `CONFIG_ENABLE_HOME_ASSISTANT_API`，恢复新项目原有的统一 ESPHome API 生命周期。
  Rationale: 新项目已有的 ESP32-S3 `WifiBoard` 都使用同一 BLE 与 ESPHome 链路；P4 条件关闭是迁移旧实测基线时临时引入的差异，不适合作为长期维护策略。
  Date/Author: 2026-08-07 / Codex
- Decision: CMD 20 保留真实 HA 注册语义，但网络请求在独立任务中执行。
  Rationale: 直接成功应答会隐藏未完成的 HA 接入；继续在 NimBLE 回调中同步请求又会阻塞 Hosted BLE。独立任务同时保留协议行为并隔离任务栈。
  Date/Author: 2026-08-07 / Codex
- Decision: P4 NimBLE 使用与 S3 相同的 8192 字节 Host 栈、单连接和单 GATT 过程，但继续使用内部内存。
  Rationale: Hosted HCI 的缓冲区能力未经验证，不直接切换到 PSRAM；先收敛并发和栈余量，减少内部堆压力与不必要并行。
  Date/Author: 2026-08-07 / Codex

## Outcomes & Retrospective

代码实现和静态验收已完成。P4 不再跳过 ESPHome API Server；CMD 20 在 NimBLE 回调中只校验并复制七个字段，网络请求由独立 8192 字节任务执行。异步响应使用局部 `ProtoParse`，避免与共享编码缓冲区并发；原子状态防止重复创建任务。ESPHome API 初始化增加就绪状态，CMD 20 最多等待五秒并只在 Noise PSK 保存成功后继续注册。

协议解析增加 4096 字节帧上限、短帧检查和认证帧长度检查，消除了 Token 长度计算的无符号下溢。P4 发布配置与 S3 对齐为 8192 字节 NimBLE Host 栈、单连接、单 GATT 过程，同时保留 Hosted NimBLE 的内部内存模式。

本轮按用户要求先创建分支并提交，因此没有运行完整 `release.py` 构建，也没有实机验证。后续必须验证 P4 ESPHome API 6053 端口、CMD 20 HTTP 状态回包、小程序 HA 接入和至少 30 分钟稳定性。

## Context and Orientation

仓库根目录是 `/Users/resmo/projectGithub/houzzkit-ai`。`main/ble/ble_manager.cc` 注册 BLE 协议命令；CMD 20 会解析 Noise PSK、HA URL、令牌和设备 ID，然后通过 `BLEManager::request()` 发起同步 HTTP POST。NimBLE 的特征写回调运行在 NimBLE Host 任务中，因此当前实现会让蓝牙任务直接阻塞在网络调用上。

`main/esphome/esphome_device.cc` 创建 ESPHome 实体和 API Server。迁移 P4 时加入了按芯片关闭 API Server 的条件，但 CMD 20 没有同步关闭，形成“API 不存在却继续注册”的不一致。`main/boards/waveshare-p4-86-panel-eth-2ro/config.json` 是该板发布配置，可用来收敛 Hosted NimBLE 的任务栈和并发参数。

## Plan of Work

先移除 `ESPHomeDevice::setup()` 中针对 P4 跳过 API Server 的条件，使 P4 与 S3 都创建端口 6053 的 API Server。保留 `api_apiserver_id` 空指针保护，作为初始化失败或调用过早时的防御。

然后在 P4 86 Panel 的发布配置中把 NimBLE Host 和兼容任务栈设为 8192 字节，把最大连接数和同时执行的 GATT 过程数设为 1。Hosted NimBLE 仍从内部 SRAM 分配，不在没有实机证据时切换内存能力。

最后重构 CMD 20。NimBLE 回调只解析字段、检查边界、复制成独立请求对象并创建工作任务。工作任务设置 Noise PSK、执行 HTTP POST、保存 HA URL，并用独立的响应编码器回传 CMD 20，避免与共享 `_protoParse` 编码缓冲区并发。用原子状态拒绝重复 CMD 20；任务创建失败立即返回错误并恢复状态。

同时对 `ProtoParse::parse()` 补齐帧最小长度和最大缓存限制，防止短帧索引越界及无符号长度下溢。正常命令和字段格式保持不变。

## Concrete Steps

所有命令在 `/Users/resmo/projectGithub/houzzkit-ai` 执行。

静态验收命令：

    git diff --check
    git status --short
    git diff --stat

用户后续确认完整构建时执行：

    . /Users/resmo/esp/esp-idf/export.sh
    python3 scripts/release.py waveshare-p4-86-panel-eth-2ro --name waveshare-p4-86-panel-eth-2ro

## Validation and Acceptance

静态验收要求差异只包含本计划、ESPHome API 恢复、P4 BLE 配置、CMD 20 异步处理和协议边界检查。代码中 CMD 20 回调不得直接调用 `request()`；P4 配置必须生成 8192 字节 NimBLE Host 栈、单连接和单 GATT 过程。

实机验收要求重启后日志出现 ESPHome API 正常 setup，不再出现 `ESPHome API server is unavailable`；CMD 20 到达时先记录任务调度，再记录 HTTP 状态和 BLE 回包；小程序完成 HA 接入。连续执行两轮对话和多次 CMD 20，至少运行 30 分钟，不再出现 TLSF `insert_free_block` 断言。

## Idempotence and Recovery

代码补丁和静态检查可重复执行。CMD 20 原子状态在任务退出和创建失败时都必须复位，避免一次网络失败永久锁死配置。若 P4 API Server 实机启动失败，可回退本分支而不影响已经提交的 P4 迁移分支；不要通过删除迁移提交恢复。

## Artifacts and Notes

本轮起点：

    branch: codex/fix-p4-ha-provisioning
    base commit: 0cd532b
    source board: waveshare-p4-86-panel-eth-2ro

本轮提交：

    branch: codex/fix-p4-ha-provisioning
    message: 修复 P4 Home Assistant 配置崩溃

## Interfaces and Dependencies

不新增 Kconfig 公共开关，不修改 BLE 命令号和响应格式。`BLEManager` 仅增加私有 HA 请求结构、原子进行状态、异步处理与响应方法。`ESPHomeDevice::setNoisePsk()` 改为返回保存结果，并增加 `isApiServerReady()` 就绪查询。继续使用现有 `NetworkInterface::CreateHttp()`、FreeRTOS 任务和 `ProtoParse`。

本次创建 ExecPlan，用于记录从同步 CMD 20 到统一、异步 HA 配置链路的实现与验证；原因是该修改同时影响 BLE、HTTP、ESPHome 和 P4 资源配置。
