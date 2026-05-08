# 向 Home Assistant 暴露功能设置与版本实体

本 ExecPlan 是活文档。随着工作推进，`Progress`、`Surprises & Discoveries`、`Decision Log`、`Outcomes & Retrospective` 四个章节必须保持最新。

本仓库的执行计划规范位于 `.agent/PLANS.md`。实施者必须按该规范维护本文件，确保新接手的人只凭本文件和当前工作树即可继续。

## Purpose / Big Picture

用户希望不再只能通过微信小程序蓝牙下发部分功能设置，而是也能在 Home Assistant 中直接看到并控制这些设置。完成后，Home Assistant 中应出现连续对话开关、睡眠模式开关、睡眠模式开始时间、睡眠模式结束时间，以及当前固件版本号。蓝牙入口仍然保留，并且 HA 与蓝牙任一侧修改设置后都应通过同一套设备状态和持久化逻辑保持一致。

## Progress

- [x] (2026-04-29T10:38:44Z) 已阅读 `.agent/PLANS.md`、现有 `ESPHomeDevice`、BLE 设置协议、睡眠时间模型、ESPHome API 裁剪状态和语言生成脚本。
- [x] (2026-04-29T10:38:44Z) 已确认当前分支为 `feature/expose-ha-entities`，工作区开始时无未提交变更。
- [x] (2026-04-29T10:43:23Z) 已创建最小 ESPHome `datetime::TimeEntity` / `TimeCall` 和 `text_sensor::TextSensor` 组件，并接入编译配置。
- [x] (2026-04-29T10:43:23Z) 已在 `ESPHomeDevice` 注册新增 HA 实体，并让 HA 与 BLE 共用现有 setter。
- [x] (2026-04-29T10:43:23Z) 已将睡眠模式默认结束时间改为 `06:00`，并修正跨天睡眠区间判断。
- [x] (2026-04-29T10:43:23Z) 已补充中英文实体名称，并生成/更新 `main/assets/lang_config.h`。
- [x] (2026-04-29T10:43:23Z) 已运行 `source /Users/resmo/esp/esp-idf/export.sh >/tmp/idf_export.log && idf.py build`，构建通过。
- [x] (2026-04-29T10:43:23Z) 已同步 `docs/houzzkit/README.md` 中的 HA 实体类型和能力清单。

## Surprises & Discoveries

- Observation: ESPHome API 层已有 `TimeCommandRequest`、`TimeStateResponse`、`TextSensorStateResponse` 等协议代码，但仓库裁剪掉了 `components/datetime`、`components/text_sensor` 目录，也未打开 `USE_DATETIME_TIME` 和 `USE_TEXT_SENSOR`。
  Evidence: `components/esphome/esphome/components/api/api_connection.cpp` 中存在 `#ifdef USE_DATETIME_TIME` 和 `#ifdef USE_TEXT_SENSOR` 分支；`find components/esphome/esphome/components -maxdepth 2 -type f | rg 'text_sensor|sensor'` 未找到对应组件文件。
- Observation: 固件版本号应来自 ESP-IDF app description，它由根目录 `CMakeLists.txt` 的 `PROJECT_VER` 注入。
  Evidence: `CMakeLists.txt` 设置 `PROJECT_VER "2.1.2"`；BLE、OTA 和 board info 现有代码均使用 `esp_app_get_description()->version`。
- Observation: 本机 `idf.py` 不在默认 PATH，且直接用完整路径会因为 shebang 依赖 `python` 失败；source ESP-IDF export 后可以正常构建。
  Evidence: `idf.py build` 返回 `command not found`；`/Users/resmo/esp/esp-idf/tools/idf.py build` 返回 `env: python: No such file or directory`；`source /Users/resmo/esp/esp-idf/export.sh >/tmp/idf_export.log && idf.py build` 构建成功。

## Decision Log

- Decision: 版本号使用 ESPHome `text_sensor` 实现，HA 侧显示为传感器状态实体。
  Rationale: 版本号如 `v1.0.0` 是字符串，数值 sensor 不能正确表达。用户已确认采用 `text_sensor`。
  Date/Author: 2026-04-29 / Codex
- Decision: BLE 协议和 NVS key 保持不变，HA 控制复用 `ESPHomeDevice` 现有 setter。
  Rationale: 这样能保留小程序蓝牙入口，并确保 HA 与 BLE 状态同步逻辑集中在一处。
  Date/Author: 2026-04-29 / Codex
- Decision: `sleepModeTI` 格式继续使用 `0xSSssEEee`，新增 HA 开始/结束时间实体只拆分和重组这一字段。
  Rationale: 避免迁移已安装设备配置，也避免修改 BLE payload。
  Date/Author: 2026-04-29 / Codex

## Outcomes & Retrospective

实现已完成。新增的 Home Assistant 实体包括连续对话 switch、睡眠模式 switch、睡眠开始 time、睡眠结束 time 和当前版本 text_sensor。连续对话、睡眠模式和睡眠时间复用现有 NVS key 与 BLE notify；版本号使用 `esp_app_get_description()->version` 只读发布。`docs/houzzkit/README.md` 已同步实体类型和能力清单。`idf.py build` 已通过。未做硬件联调，因此 HA 实体可见性、BLE notify 与真实睡眠音量策略仍需在设备上做端到端确认。

## Context and Orientation

主业务桥接类是 `main/esphome/esphome_device.cc` 和 `main/esphome/esphome_device.h`。它启动 ESPHome API server，注册 Home Assistant 可见实体，并维护麦克风、音量、连续对话、睡眠模式等状态。当前已有麦克风 switch、音量 number、若干 text 输入实体；连续对话、睡眠模式、睡眠时间段已经有内部字段、NVS 持久化和 BLE 通知，但还未注册成 HA 实体。

蓝牙设置入口在 `main/ble/ble_manager.cc` 的 `CMD_DEVICE_SETTINGS` 分支。小程序设置连续对话、睡眠模式和睡眠时间段时，最终调用 `ESPHomeDevice::setContinuousDialogue`、`setSleepMode`、`setSleepModeTimeInterval`。这些 setter 会写入 `Settings("esphome")` 命名空间，并通过 BLE notify 回推状态。

睡眠时间段由 `main/esphome/sleep_mode_time_interval.*` 表示。字段 `startHour/startMinute/endHour/endMinute` 可打包成一个 `uint32_t`：高到低四个字节分别是开始时、开始分、结束时、结束分。当前默认结束时间是 `07:00`，需要改为 `06:00`。睡眠区间可能跨天，例如 `22:00-06:00`。

本仓库集成的是裁剪版 ESPHome，源码在 `components/esphome/esphome`。API 协议层已有 time 和 text_sensor 的消息处理分支，但实体组件文件缺失。因此需要补齐最小组件，让 `Application`、`Controller`、API list/state/command 流程可以编译并工作。

## Plan of Work

先在 `components/esphome/esphome/components/datetime/` 下创建 `time_entity.h/.cpp` 和 `time_call.h/.cpp`。`TimeEntity` 继承 `EntityBase`，保存 `hour`、`minute`、`second` 和 `has_state`，提供 `publish_state(hour, minute, second)`、`make_call()` 和 `control(hour, minute, second)` 虚函数。`TimeCall` 保存目标时间，校验小时 0-23、分钟/秒 0-59 后调用父实体 `control`。

再在 `components/esphome/esphome/components/text_sensor/` 下创建 `text_sensor.h/.cpp`。`TextSensor` 继承 `EntityBase` 和 `EntityBase_DeviceClass`，保存字符串 `state`，提供 `publish_state`、`add_on_state_callback` 和可选 `set_unique_id`/`unique_id`。只读版本号实体不需要 command call。

随后更新 `components/esphome/esphome/define.h`、`components/esphome/esphome.h` 和 `components/esphome/CMakeLists.txt`，打开并编译 `USE_DATETIME_TIME` 与 `USE_TEXT_SENSOR`。

接着修改 `main/esphome/esphome_device.cc`。新增连续对话 switch、睡眠模式 switch、睡眠开始 time、睡眠结束 time、当前版本 text_sensor 的类和全局指针。在 `setup()` 中注册这些实体，设置 object_id，发布初始状态；版本号使用 `esp_app_get_description()->version`。在相关 setter 里发布 HA 状态，确保 BLE 或 HA 任一入口修改后都会同步。

最后修改 `SleepModeTimeInterval` 默认值和跨天判断，更新语言 JSON 与生成的 `lang_config.h`，然后运行构建验证。

## Concrete Steps

所有命令均在仓库根目录 `/Users/resmo/project/houzzkit-ai` 执行。

1. 编辑并新增 ESPHome 组件文件。
2. 编辑 `ESPHomeDevice` 和睡眠时间逻辑。
3. 编辑 `main/assets/locales/en-US/language.json`、`main/assets/locales/zh-CN/language.json`，并运行：

       python scripts/gen_lang.py --language zh-CN --output main/assets/lang_config.h

4. 运行：

       idf.py build

## Validation and Acceptance

编译成功是最低验收标准。运行 `idf.py build` 应结束于成功状态。若本机 ESP-IDF 环境缺失，应记录具体错误。

设备接入 Home Assistant 后应能看到新增实体：`continuous_dialogue_switch`、`sleep_mode_switch`、`sleep_mode_start_time`、`sleep_mode_end_time` 和 `current_version`。首次无 NVS 配置时，默认值为连续对话开启、睡眠模式关闭、睡眠开始 `22:00`、睡眠结束 `06:00`。当前版本号状态应等于 `esp_app_get_description()->version`。

从 HA 改连续对话、睡眠模式或睡眠时间后，实体应立即回显新值，NVS 持久化应在重启后恢复，小程序侧应收到既有 BLE property notify。从小程序蓝牙下发相同设置后，HA 实体也应更新。

睡眠区间 `22:00-06:00` 下，系统时间为 `23:00` 或 `05:59` 时应处于睡眠区间，`12:00` 时不处于睡眠区间。睡眠模式开启且在区间内时，现有音量限制策略仍为最高 20。

## Idempotence and Recovery

新增组件和实体注册是增量改动，可重复编译验证。BLE 协议和 NVS key 不变，因此无需迁移步骤。若 `idf.py build` 失败，优先检查新增组件是否已加入 `components/esphome/CMakeLists.txt`，以及 `USE_DATETIME_TIME` / `USE_TEXT_SENSOR` 是否和 include/source 文件一致。

## Artifacts and Notes

当前关键证据：

    CMakeLists.txt: set(PROJECT_VER "2.1.2")
    main/ble/ble_manager.cc: pushString8(esp_app_get_description()->version)
    main/esphome/esphome_device.cc: 已有 setContinuousDialogue / setSleepMode / setSleepModeTimeInterval
    Build: houzzkit.bin binary size 0x29cf00 bytes. Smallest app partition is 0x3f0000 bytes. 0x153100 bytes (34%) free.
    Build: Project build complete.

## Interfaces and Dependencies

新增接口：

    void ESPHomeDevice::setSleepModeStartTime(uint8_t hour, uint8_t minute);
    void ESPHomeDevice::setSleepModeEndTime(uint8_t hour, uint8_t minute);

新增最小 ESPHome 类型：

    namespace esphome::datetime {
      class TimeEntity;
      class TimeCall;
    }

    namespace esphome::text_sensor {
      class TextSensor;
    }

不新增外部依赖，不修改 BLE property 编号，不修改分区表或板卡配置。
