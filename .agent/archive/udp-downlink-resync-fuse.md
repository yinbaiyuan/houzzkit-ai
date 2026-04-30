# UDP Downlink Resync Fuse

本 ExecPlan 是活文档。随着工作推进，`Progress`、`Surprises & Discoveries`、`Decision Log`、`Outcomes & Retrospective` 四个章节必须保持最新。本文件遵循仓库根目录 `.agent/PLANS.md`。

## Purpose / Big Picture

用户正在验证长回复中途网络突然极差、随后恢复的场景。当前固件已有 200/240/280ms 自适应 target、500ms max cap、短等、FEC 和 PLC；这些能处理普通弱网，但在极端网络中途把播放打穿时，播放指针可能持续用 PLC 补旧缺口，未来包堆满 500ms cap 并被持续 trim，导致异常蔓延到后续内容。本次改动要增加一个非常保守的 resync 熔断器：只有硬失败已经发生时，才跳过不可恢复旧缺口，把 `expected_downlink_sequence_` 重锚到 jitter buffer 当前最早可用包，让网络恢复后的后续内容尽快回到正常播放。用户可通过 summary 看到 `resync_events`，并且极端弱网下 trim 日志不再刷屏。

## Progress

- [x] (2026-04-22 12:07+08:00) 阅读 `.agent/PLANS.md`、当前 `AudioService` 恢复路径和用户提供的极端弱网日志。
- [x] (2026-04-22 12:14+08:00) 增加 resync 统计、限频常量和状态字段。
- [x] (2026-04-22 12:14+08:00) 实现 hard failure 判断、resync 动作和 trim 日志限频。
- [x] (2026-04-22 12:14+08:00) 更新下一轮 target 自适应，让硬失败直接 next target 到 280ms。
- [x] (2026-04-22 12:18+08:00) 运行 `git diff --check` 和 `idf.py build`，准备归档本 ExecPlan。

## Surprises & Discoveries

- Observation: 当前 `TrimJitterBufferLocked()` 每丢一个未来包都输出 warning，极端弱网下会连续刷数百行。
  Evidence: 用户日志中 `Dropping future downlink packet to cap jitter buffer` 从 `trimmed=1` 连续刷到上百。
- Observation: 当前 `AdaptDownlinkTargetAfterSessionLocked()` 对 `output_gap_events` 只做逐级升档，极端硬失败后会从 200 只升到 240。
  Evidence: 用户结束日志显示 `output_gaps=2 trimmed=809 plc=906 max_arrival_ms=1222`，但 summary 是 `target_ms=200 next_target_ms=240`。
- Observation: Resync 必须避免把 `expected_downlink_sequence_` 往回拉，否则可能重放已补偿过的旧时间线。
  Evidence: 实现中只有 `first_available > expected_downlink_sequence_` 时才执行 resync。

## Decision Log

- Decision: Resync 只在硬失败后触发，并限制每轮最多 2 次、两次间隔至少 500ms。
  Rationale: Resync 可能跳过音频内容，不能抢在普通 FEC/PLC 之前发生。
  Date/Author: 2026-04-22 / Codex
- Decision: Resync 后只清理未输出的 UDP 下行播放任务，保留非 UDP 播放任务。
  Rationale: 下行熔断不应误删提示音或其他非 UDP 音频任务。
  Date/Author: 2026-04-22 / Codex
- Decision: `output_gaps`、`trimmed`、`plc > 30`、`max_arrival_ms > 500` 命中任一硬失败时下一轮直接使用 280ms target。
  Rationale: 这些信号说明普通逐级升档已经太保守，但 280ms 仍是最高低延迟 target，不把 500ms cap 当 target。
  Date/Author: 2026-04-22 / Codex

## Outcomes & Retrospective

已完成。`main/audio/audio_service.h` 增加了 resync 统计、阈值常量和状态字段；`main/audio/audio_service.cc` 增加了 trim 日志限频、hard failure resync、硬失败直升 280ms target 和 summary 字段。`git diff --check` 通过；加载 ESP-IDF 5.4.1 后 `idf.py build` 通过并生成 `build/houzzkit.bin`。尚未做硬件刷机后的极端弱网复测，下一步应重点验证正常网络和普通弱网下 `resync_events=0`，极端弱网下最多 1-2 次 resync 且不会持续刷 trim。

## Context and Orientation

固件仓库位于 `D:\houzzkit\houzzkit-ai-firmware`。下行 UDP 音频由 `main/protocols/mqtt_protocol.cc` 接收并交给 `main/audio/audio_service.cc`。`AudioService` 用 `downlink_jitter_buffer_` 保存按 sequence 编号的 Opus 包，用 `expected_downlink_sequence_` 表示下一个应该播放的 sequence。正常路径会优先播放当前 expected 包；如果缺包但下一个包到了，就用 Opus FEC；如果缺口更大，就用 PLC。`audio_playback_queue_` 保存已经解码但还没写到扬声器的 PCM。Resync 的意思是当当前时间线已经不可恢复时，主动把 expected 跳到 jitter buffer 当前最早包，放弃旧缺口，避免持续 PLC 和 trim。

## Plan of Work

先在 `main/audio/audio_service.h` 增加 resync 统计字段、连续 PLC 计数、resync 冷却时间和 helper 函数声明。然后在 `main/audio/audio_service.cc` 的 trim、can-produce 和 decode action 路径中接入 hard failure 判断：trim 超阈值、输出 gap、连续 PLC、jitter buffer 最早包远超 expected 都可触发。触发时清空尚未输出的 UDP 下行播放任务，重设 `expected_downlink_sequence_`，清掉 recovery deadline 和边界状态，并输出一条 warning。最后更新 summary 和 target 自适应，确保硬失败下一轮直接到 280ms，同时 trim 日志按前 4 次和每 50 次限频。

## Validation

在仓库根目录运行：

    git diff --check -- main/audio/audio_service.h main/audio/audio_service.cc

然后加载 ESP-IDF 5.4.1 环境并构建：

    $env:IDF_PYTHON_ENV_PATH='D:\esp\Espressif\python_env\idf5.4_py3.11_env'
    . D:\esp\Espressif\frameworks\esp-idf-v5.4.1\export.ps1
    idf.py build

预期 `git diff --check` 无错误，`idf.py build` 生成 `build/houzzkit.bin`。硬件弱网验收由用户刷机后运行普通网络、`loss 1%`、相关抖动、强乱序和极端弱网。正常和普通弱网应为 `resync_events=0`；极端弱网若触发 output gap 或大量 trim，应最多出现 1-2 次 resync，summary 显示 `next_target_ms=280`。
