# OTA 下载进度实体暴露到 Home Assistant

本 ExecPlan 是活文档。随着工作推进，`Progress`、`Surprises & Discoveries`、`Decision Log`、`Outcomes & Retrospective` 四个章节必须保持最新。

本文件遵循仓库根目录 `.agent/PLANS.md` 的规则。完成后应归档到 `.agent/archive/`，`.agent/` 根目录只保留仍在执行的计划。

## Purpose / Big Picture

用户希望 OTA 升级过程中能在 Home Assistant 里看到固件下载进度。完成后，HA 会新增一个诊断实体，用数值传感器显示固件 HTTP 下载百分比。小程序 BLE 触发 OTA 和系统内已有 OTA 路径都要同步这个实体，同时保留 BLE 原有进度通知与屏幕显示。

## Progress

- [x] (2026-04-30 03:04Z) 确认当前分支为 `feature/expose-ha-entities`，工作区仅有无关未跟踪 `Makefile`。
- [x] (2026-04-30 03:04Z) 阅读 OTA 路径、ESPHome API sensor 分支、已有 `text_sensor` 与实体注册代码。
- [x] (2026-04-30 03:04Z) 补齐 ESPHome numeric `sensor::Sensor` 最小组件并接入构建。
- [x] (2026-04-30 03:04Z) 注册 `OTA下载进度` HA 实体。
- [x] (2026-04-30 03:04Z) 将 BLE OTA 和 `Ota::Upgrade` 两条 OTA 路径的下载进度同步发布到 HA。
- [x] (2026-04-30 03:07Z) 更新本地化文案并编译验证。
- [x] (2026-04-30 03:07Z) 完成后更新结果、归档 ExecPlan。
- [x] (2026-04-30 03:15Z) 按用户后续要求移除 `OTA升级状态` 文本实体，只保留 `OTA下载进度`。

## Surprises & Discoveries

- Observation: ESPHome API 已经有 `USE_SENSOR` 条件编译下的协议代码，但仓库裁剪版缺少 `components/esphome/esphome/components/sensor` 组件。
  Evidence: `api_connection.cpp` 中已有 `send_sensor_state` 和 `try_send_sensor_info`，但 `rg --files components/esphome/esphome/components | rg sensor` 只发现 `text_sensor`。

- Observation: 固件有两条 OTA 进度路径。
  Evidence: `Application::otaUpgrade()` 计算进度后调用 `BLEManager::otaProgress(...)`；`Ota::Upgrade(...)` 计算进度后调用 `upgrade_callback_`。

## Decision Log

- Decision: 新增 `sensor.ota_download_progress` 而不是 `sensor.ota_upgrade_progress`。
  Rationale: 用户明确要求实体名称表达为 OTA 下载进度；当前百分比来自 HTTP 已读字节数。
  Date/Author: 2026-04-30 / Codex

- Decision: OTA 下载进度不持久化；启动后默认 `0`。
  Rationale: 这是过程状态，重启后旧进度没有控制意义。
  Date/Author: 2026-04-30 / Codex

- Decision: 移除 `text_sensor.ota_upgrade_status`，只保留 `sensor.ota_download_progress`。
  Rationale: 用户后续明确要求删掉 OTA 升级状态实体。
  Date/Author: 2026-04-30 / Codex

## Outcomes & Retrospective

已完成。新增 numeric sensor 最小实现并注册 `sensor.ota_download_progress`。两条 OTA 路径都会在开始和下载过程中发布 HA 下载进度，同时 BLE 原有进度通知保留。按用户后续要求，`text_sensor.ota_upgrade_status` 已移除。

验证通过：

    source /Users/resmo/esp/esp-idf/export.sh >/tmp/idf_export.log && idf.py build
    Project build complete. To flash, run:
     idf.py flash

未做硬件 HA 联调，因此实体发现和 OTA 实机状态流仍需在设备接入 HA 后确认。

## Context and Orientation

仓库是 ESP-IDF 固件，`main/application.cc` 管理设备状态和 OTA 入口，`main/ota.cc` 封装另一条 OTA 下载升级流程，`main/esphome/esphome_device.cc` 注册 Home Assistant 可见实体。ESPHome 是本仓库内裁剪后的本地组件，位于 `components/esphome/`，已有 API 协议对 numeric sensor 的支持，但缺少最小 `sensor::Sensor` 类型，因此本任务需要补齐。

“numeric sensor” 指 HA 中显示数值状态的传感器实体。这里的 OTA 下载进度来自 `total_read * 100 / content_length`，也就是 HTTP 响应 body 已读取比例。当前 OTA 代码边读 HTTP 边写 OTA 分区，所以下载进度也间接反映主体写入进度，但实体命名只承诺“下载进度”。

## Plan of Work

先在 `components/esphome/esphome/components/sensor/` 新增 `sensor.h` 与 `sensor.cpp`，提供 API 已经使用的字段和方法：`state`、`publish_state(float)`、`add_on_state_callback`、`unique_id()`、`get_accuracy_decimals()`、`get_force_update()`、`get_state_class()`、单位、设备类和 has_state。然后打开 `USE_SENSOR`，把组件源文件和 include 加入 `components/esphome/CMakeLists.txt`，并在 `components/esphome/esphome.h` 中 include。

随后在 `ESPHomeDevice` 中增加 `_otaDownloadProgress` 内部状态，新增 `setOtaDownloadProgress(uint8_t)`。`setup()` 注册 `sensor.ota_download_progress`，实体类别为 diagnostic，初始值为 `0`。

最后修改 OTA 入口。`Application::startOtaUpgrade()` 和 `Application::UpgradeFirmware()` 开始时发布 `0`；下载循环中每次计算进度时发布到 HA；成功设置 boot partition 或成功回调后发布 `100`。

## Concrete Steps

在仓库根目录 `/Users/resmo/project/houzzkit-ai` 执行开发和验证。实现后运行：

    source /Users/resmo/esp/esp-idf/export.sh >/tmp/idf_export.log && idf.py build

若本地 ESP-IDF 环境可用，预期构建完成并输出项目 app/bin 生成信息。若构建失败，按编译错误修正，不应跳过验证。

## Validation and Acceptance

编译通过是最低验收。HA 接入后应观察到 `OTA下载进度` 实体。空闲启动后进度为 `0%`。发起 OTA 后下载进度从 `0` 逐步增长到 `100`。BLE 原有 `CMD_OTA_PROGRESS` 通知仍然发送。

## Idempotence and Recovery

本计划只新增和修改源码，不改 BLE 协议，不写 NVS 新键。失败时可重新运行构建命令。工作区已有无关未跟踪 `Makefile`，不得删除、提交或修改。

## Artifacts and Notes

关键现状证据：

    ## feature/expose-ha-entities...origin/feature/expose-ha-entities
    ?? Makefile

    APIConnection::try_send_sensor_state(...) 已读取 sensor->state 和 sensor->has_state()
    APIConnection::try_send_sensor_info(...) 已读取 unit、accuracy_decimals、force_update、device_class、state_class、unique_id

构建证据：

    [43/126] Building CXX object esp-idf/esphome/CMakeFiles/__idf_esphome.dir/esphome/components/sensor/sensor.cpp.obj
    Project build complete. To flash, run:
     idf.py flash

## Interfaces and Dependencies

新增 C++ 接口：

    void ESPHomeDevice::setOtaDownloadProgress(uint8_t progress);
新增 HA 实体：

    sensor.ota_download_progress

新增本地化 key：

    ESPHOME_ENTITY_SENSOR_NAME_OTA_DOWNLOAD_PROGRESS

Numeric sensor 最小实现依赖 ESPHome 已有 `EntityBase`、`EntityBase_DeviceClass`、`EntityBase_UnitOfMeasurement`、`CallbackManager` 和 API protobuf 枚举 `SensorStateClass`。
