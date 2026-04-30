# UDP Downlink Adaptive Jitter Buffer

本 ExecPlan 是活文档。随着工作推进，`Progress`、`Surprises & Discoveries`、`Decision Log`、`Outcomes & Retrospective` 四个章节必须保持最新。本文件遵循仓库根目录 `.agent/PLANS.md`。

## Purpose / Big Picture

用户正在验证 UDP 下行音频在弱网和强乱序环境里的体验。当前固件已经能避免输出断流，但强乱序下会较早用 FEC/PLC 代替缺失包，随后原包迟到并被丢弃，造成 `late` 和 `plc` 偏高。本次改动要把 jitter buffer 的目标水位和最大容量拆清：200/240/280ms 是启动目标，500ms 只是防爆仓上限；同时在缺包但播放队列仍安全时短等一个音频帧，让可恢复的乱序包有机会赶上。用户可通过日志看到启播 target、下一轮 target、recovery waits，以及弱网下 `late/plc` 降低且 `output_gaps=0`。

## Progress

- [x] (2026-04-22 10:56+08:00) 阅读 `.agent/PLANS.md`、当前 `AudioService` 和 `MqttProtocol`，确认 MQTT 已经把解密成功的乱序包转交给 AudioService。
- [x] (2026-04-22 11:05+08:00) 更新 `AudioService` 常量、状态字段和 summary 指标。
- [x] (2026-04-22 11:05+08:00) 实现 deadline-aware recovery 等待逻辑。
- [x] (2026-04-22 11:05+08:00) 实现下一轮 target 自适应和 late 日志收敛。
- [x] (2026-04-22 11:09+08:00) 运行 `git diff --check` 和 `idf.py build`，准备归档本 ExecPlan。

## Surprises & Discoveries

- Observation: 当前 `MqttProtocol` 已经不再按旧 sequence 丢包，而是调用 `ObserveDownlinkSequence()` 后把有效包交给 AudioService。
  Evidence: `main/protocols/mqtt_protocol.cc` 中 UDP `OnMessage` 解密成功后调用 `on_incoming_audio_`，没有 `old sequence` return。
- Observation: 当前恢复逻辑在 `expected` 缺失且 `expected + 1` 到达时会立即 FEC；这会让稍后赶到的原包成为 late。
  Evidence: `AudioService::PrepareDownlinkDecodeActionLocked()` 中 FEC 分支没有检查播放队列水位或 deadline。
- Observation: `AudioOutputTask()` 在每次 pop 播放队列后已经 `notify_all()`，可用于唤醒 codec 线程重新评估是否还安全等待。
  Evidence: `main/audio/audio_service.cc` 中 pop 后紧接着设置 `downlink_output_active_` 并调用 `audio_queue_cv_.notify_all()`。

## Decision Log

- Decision: 保留 MQTT 侧转交逻辑，本次主要修改 `main/audio/audio_service.*`。
  Rationale: MQTT 已经提供到达压力诊断；最终是否 late/duplicate/FEC/PLC 应由 jitter buffer 决策。
  Date/Author: 2026-04-22 / Codex
- Decision: 采用 200/240/280ms 三档 target，500ms 只作为 max cap。
  Rationale: 这符合实时音频的延迟/稳定性取舍；500ms 作为目标会明显增加首包延迟。
  Date/Author: 2026-04-22 / Codex
- Decision: 缺包时只在播放队列至少 2 包时等待，单次等待最长一个 frame duration。
  Rationale: 这样减少提前 FEC/PLC，同时保留输出不断流的安全边界。
  Date/Author: 2026-04-22 / Codex

## Outcomes & Retrospective

已完成。`main/audio/audio_service.h` 拆清了 target buffer 和 max buffer 常量，`main/audio/audio_service.cc` 增加了 recovery deadline、三档下一轮 target 自适应、late 日志收敛和 summary 指标。`git diff --check` 通过；加载 ESP-IDF 5.4.1 后 `idf.py build` 通过并生成 `build/houzzkit.bin`。尚未做硬件刷机后的弱网主观体验复测，下一步应使用正常网络、`loss 1%`、`delay 100ms 50ms loss 1%`、`delay 100ms 50ms 75% loss 1%` 四档日志验证。

## Context and Orientation

固件仓库位于 `D:\houzzkit\houzzkit-ai-firmware`。下行 UDP 音频由 `main/protocols/mqtt_protocol.cc` 接收、解密后交给 `main/audio/audio_service.cc`。`AudioService` 用 `downlink_jitter_buffer_` 存放 Opus 压缩包，按 sequence 顺序解码到 `audio_playback_queue_`，再由输出任务写到 codec。jitter buffer 的 target 是“启播前希望攒多少音频”，max cap 是“最多允许缓存多少未来包以防内存增长”。FEC 是 Opus 的前向纠错，PLC 是丢包隐藏；两者都用于补缺包，但过多会降低音质。

## Plan of Work

先把 `audio_service.h` 中宏名改成明确的 base/weak/strong target 和 max cap，并增加当前 target、下一轮 target、恢复等待 deadline 与统计字段。然后修改启播计算，让 `MaybeStartDownlinkPlaybackLocked()` 使用当前 target 并在日志中输出 target ms。接着在 `CanProduceJitterPlaybackLocked()` 中增加缺包等待判断：若当前缺 `expected` 但后续包已到，且播放队列至少有 2 包，则设置一个最多一个 frame 的 deadline 并暂时返回 false；deadline 到期或播放队列不足时才允许 `PrepareDownlinkDecodeActionLocked()` 进入 FEC/PLC。最后在 reset summary 中打印 target 和 recovery waits，根据本轮 `plc/late/output_gaps/max_arrival_ms` 更新下一轮 target。

## Validation

在仓库根目录运行：

    git diff --check -- main/audio/audio_service.h main/audio/audio_service.cc

然后加载 ESP-IDF 5.4.1 环境并构建：

    $env:IDF_PYTHON_ENV_PATH='D:\esp\Espressif\python_env\idf5.4_py3.11_env'
    . D:\esp\Espressif\frameworks\esp-idf-v5.4.1\export.ps1
    idf.py build

预期 `git diff --check` 无错误，`idf.py build` 生成 `build/houzzkit.bin`。硬件弱网验收由用户刷机后运行 `tc qdisc replace dev eth0 root netem delay 100ms 50ms loss 1%`，观察 summary 中 `late/plc` 较当前 `late=52/plc=16` 下降且 `output_gaps=0`。
