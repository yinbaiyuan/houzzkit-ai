# UDP Downlink Resync Anti-Spread

本 ExecPlan 是活文档。随着工作推进，`Progress`、`Surprises & Discoveries`、`Decision Log`、`Outcomes & Retrospective` 四个章节必须保持最新。本文件遵循仓库根目录 `.agent/PLANS.md`。

## Purpose / Big Picture

用户正在验证 UDP 下行音频在极端弱网中的恢复能力。固件已经有 200/240/280ms 自适应 target、500ms max cap、短等、Opus FEC/PLC，以及第一版 hard-failure resync。实测证明第一版 resync 能触发，但在长回复中出现短黑洞后，固定每轮 2 次的预算可能耗尽，随后新包继续到达而播放指针仍落后，导致 `trimmed` 和 `plc` 持续滚增。本次改动要把 resync 从固定 2 次熔断改成“新故障增量 + 冷却预算 + 最新可播放窗口重锚”，让极端网络恢复后尽快追上新包，避免旧缺口拖垮长回复后续内容。

## Progress

- [x] (2026-04-22 14:20+08:00) 阅读 `.agent/PLANS.md`、上一版归档计划、当前 `AudioService` resync 实现和用户的触发日志。
- [x] (2026-04-22 14:31+08:00) 实现 resync 增量 hard-failure 基线、6 次预算、800ms 冷却和更合理的锚点选择。
- [x] (2026-04-22 14:33+08:00) 更新 summary/log 字段，并同步 `docs/mqtt-udp.md`。
- [x] (2026-04-22 14:36+08:00) 运行 `git diff --check` 和 `idf.py build`。
- [x] (2026-04-22 14:38+08:00) 更新本 ExecPlan 的结果并准备归档到 `.agent/archive/`。

## Surprises & Discoveries

Observation: 第一版 resync 确实能在短黑洞后触发，但固定 2 次预算在长回复灾难场景中不够。

Evidence: 用户日志中先出现 `Downlink resync after hard failure: old_expected=576 new_expected=610 ... resync_events=1`，随后 `resync_events=2`；之后继续出现 `Jitter buffer trim continuing ... trimmed=1400` 和 `plc=1254`。

Observation: 总 `plc_packets` 很高不等于会触发 resync；当前触发点依赖当前连续 PLC、trim、output gap 或 buffer ahead。

Evidence: 用户多轮 `plc=87/129` 的弱网测试仍 `resync=0`，因为 FEC/正常包会清掉连续 PLC，且没有 `output_gaps` 或 `trimmed`。

Observation: `idf.py build` 通过，说明新增字段、日志格式和 resync 锚点逻辑能在当前 ESP-IDF 配置下编译。

Evidence: 构建输出包含 `Generated D:/houzzkit/houzzkit-ai-firmware/build/houzzkit.bin` 和 `Project build complete`。

## Decision Log

Decision: 不把普通弱网路径改成更大 target 或更激进等待，仍保留现有 short wait、FEC/PLC 和 200/240/280ms target。

Rationale: 用户正常网络和强乱序可恢复测试已经证明主路径有效；问题只在 hard failure 后异常蔓延。

Date/Author: 2026-04-22 / Codex

Decision: 再次 resync 必须基于自上次 resync 后新增 hard signal，而不是累计 `output_gaps` 或累计 `trimmed`。

Rationale: 旧故障累计值不会下降，如果直接用累计值会导致冷却一到就重复跳段。

Date/Author: 2026-04-22 / Codex

Decision: 对 output gap 或 trim storm 使用最新可播放窗口锚点，而不是永远锚到 jitter buffer 的最早包。

Rationale: trim storm 说明 buffer 尾部不断被丢，而 begin 可能仍太旧；锚到最新约一个 target 窗口的起点能更快追上恢复后的音频。

Date/Author: 2026-04-22 / Codex

Decision: trim 日志仍保留前 4 次和早期每 50 次 warning，但 200 次之后降到每 200 次 warning。

Rationale: 灾难场景下需要知道 cap 仍被触发，但不应让日志刷屏掩盖 resync 和 summary。

Date/Author: 2026-04-22 / Codex

## Outcomes & Retrospective

已完成。`main/audio/audio_service.h` 将每轮 resync 预算提升到 6 次、冷却调整为 800ms，并新增 `resync_dropped_jitter_packets` 与 resync baseline 状态。`main/audio/audio_service.cc` 改为使用新增 output gap、新增 trim storm、连续 PLC 和 buffer ahead 触发 resync；output gap 或 trim storm 会锚到最新约一个 target window 的起点，并丢弃低于新 expected 的 jitter 包。summary 和 resync warning 已增加 `dropped_jitter`、`reason`、`buffer_first`、`buffer_last`、`consecutive_plc`。`docs/mqtt-udp.md` 已同步机制描述。`git diff --check` 通过；加载 ESP-IDF 5.4.1 后 `idf.py build` 通过并生成 `build/houzzkit.bin`。硬件复测尚未在本轮执行，下一步应由用户刷机后重复短黑洞和持续极端网络场景，确认不再出现 resync 用尽后长期 trim/PLC 风暴。

## Context and Orientation

固件仓库位于 `D:\houzzkit\houzzkit-ai-firmware`。下行 UDP 音频由 `main/protocols/mqtt_protocol.cc` 解密后作为 `AudioStreamPacket` 交给 `main/audio/audio_service.cc`。`AudioService` 用 `downlink_jitter_buffer_` 保存按 `sequence` 排序的 Opus 包，用 `expected_downlink_sequence_` 表示下一帧应该播放的序号。正常路径优先播放 expected 包；缺一帧时用下一包的 Opus FEC；缺口更大时用 PLC 生成掩蔽音。`audio_playback_queue_` 保存已经解码但尚未写到扬声器的 PCM。

第一版 resync 在 `MaybeResyncDownlinkAfterHardFailureLocked()` 中实现。它只允许每轮 2 次、冷却 500ms，并把 expected 重锚到 jitter buffer 当前最早包。这个行为能证明熔断器可以触发，但用户实测发现长回复里两次用完后，后续仍可能进入持续 trim/PLC 风暴。

## Plan of Work

先在 `main/audio/audio_service.h` 调整 resync 常量和统计状态。`DOWNLINK_MAX_RESYNC_EVENTS` 改为 6，`DOWNLINK_RESYNC_COOLDOWN_MS` 改为 800。`DownlinkDebugStatistics` 增加 `resync_dropped_jitter_packets`。`AudioService` 私有状态增加上次 resync 后的 baseline：`downlink_resync_baseline_output_gaps_`、`downlink_resync_baseline_trimmed_packets_`、`downlink_resync_baseline_plc_packets_`。

然后改 `main/audio/audio_service.cc` 的 `MaybeResyncDownlinkAfterHardFailureLocked()`。函数先计算 buffer 的 first 和 last、当前 target packets、是否出现新增 output gap、是否自 baseline 后新增 trim 达到 20、是否连续 PLC 超过 30、以及 first 是否领先 expected 超过 max packets。没有新增 hard signal 时直接返回。触发时选择锚点：如果是 output gap 或 trim storm，就从 jitter buffer 尾部向前保留约一个 target window，锚到该窗口第一包；否则锚到 first。重锚前丢弃低于新 expected 的 jitter 包，清空未输出的 UDP playback task，保留非 UDP task，清除 recovery deadline、连续 loss/PLC、recovery boundary/declick 状态，并更新 baseline。

接着更新 summary 和自适应。`AdaptDownlinkTargetAfterSessionLocked()` 把 `resync_events > 0` 视为 hard failure，下一轮直接到 280ms。summary 日志增加 `dropped_jitter`。resync warning 增加 reason、buffer_first、buffer_last、dropped_jitter、consecutive_plc。trim warning 继续限频，灾难场景可把超过 200 次后的频率降到每 200 次一次。

最后同步 `docs/mqtt-udp.md`，把“每轮最多 2 次、500ms 冷却、锚到最早包”的描述更新为“每轮最多 6 次、800ms 冷却、新故障增量、最新可播放窗口重锚”，并补充 `dropped_jitter` 日志字段。

## Validation

在仓库根目录运行：

    git diff --check

然后加载 ESP-IDF 5.4.1 环境并构建：

    $env:IDF_PYTHON_ENV_PATH='D:\esp\Espressif\python_env\idf5.4_py3.11_env'
    . D:\esp\Espressif\frameworks\esp-idf-v5.4.1\export.ps1
    idf.py build

预期 `git diff --check` 无错误，`idf.py build` 生成 `build/houzzkit.bin`。硬件验收由用户刷机后运行：正常网络降回 200，强乱序可恢复时 `resync=0`，短黑洞后出现 resync 且恢复后不长期 trim/PLC 风暴，持续极端网络最多 6 次且限频。
