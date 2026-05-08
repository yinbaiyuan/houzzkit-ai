# Home Assistant 固件升级按钮

本 ExecPlan 是活文档。随着工作推进，`Progress`、`Surprises & Discoveries`、`Decision Log`、`Outcomes & Retrospective` 四个章节必须保持最新。

本文件遵循仓库根目录 `.agent/PLANS.md` 的规则。完成后应归档到 `.agent/archive/`。

## Purpose / Big Picture

用户希望在 Home Assistant 中直接点击按钮触发固件升级。完成后，HA 会新增一个 `button.firmware_upgrade_button`，名称为 `固件升级`。按钮入口已经接入应用层；但按用户后续要求，`ota.cc` 中此前注释掉的 firmware 解析逻辑先保持注释，因此按钮不会在本次改动中恢复该解析逻辑。

## Progress

- [x] (2026-04-30 03:36Z) 阅读现有 OTA 检查、升级入口和 ESPHome button 实体注册方式。
- [x] (2026-04-30 03:36Z) 新增手动固件检查开关，使开机 OTA 检查不恢复自动升级。
- [x] (2026-04-30 03:36Z) 新增 Application 入口供 HA button 异步触发升级。
- [x] (2026-04-30 03:36Z) 注册 HA 固件升级按钮和本地化文案。
- [x] (2026-04-30 03:38Z) 编译验证并归档计划。
- [x] (2026-04-30 03:45Z) 按用户要求还原 `Ota::CheckVersion()` 中此前注释掉的 firmware 解析逻辑，按钮不再启用该段解析。
- [x] (2026-04-30 03:52Z) 按用户确认新增 `text.ota_upgrade_url`，按钮从该 HA text 实体读取固件 URL 并直接升级。

## Surprises & Discoveries

- Observation: `Ota::CheckVersion()` 里的 firmware 解析被注释，并有“关闭开机OTA检查，在微信小程序/APP中进行检查”的注释。
  Evidence: `main/ota.cc` 中 `has_new_version_ = false;` 后的 firmware JSON 解析整段注释。

- Observation: MCP 已有从指定 URL 升级的工具，并通过 `Application::Schedule` 在主事件循环调用 `UpgradeFirmware`。
  Evidence: `main/mcp_server.cc` 的 `self.upgrade_firmware` 工具创建 `Ota` 后调用 `app.UpgradeFirmware(*ota, url)`。

## Decision Log

- Decision: 按钮只触发“检查并升级最新固件”，不新增 URL 输入实体。
  Rationale: HA button 是无参数瞬时动作，固件来源应复用已有 OTA 检查接口；指定 URL 升级已经由 MCP 工具覆盖。
  Date/Author: 2026-04-30 / Codex

- Decision: 保留 `Ota::CheckVersion()` 中此前注释掉的 firmware 解析逻辑，不通过按钮路径恢复该段代码。
  Rationale: 用户后续明确要求“之前注释掉的逻辑先保留”。
  Date/Author: 2026-04-30 / Codex

- Decision: 新增 `text.ota_upgrade_url` 作为 HA 侧固件地址输入，`button.firmware_upgrade_button` 读取该地址后调用 `Application::StartFirmwareUpgrade(url)`。
  Rationale: HA button 无法携带参数，用户选择用 text 实体提供 OTA 地址。
  Date/Author: 2026-04-30 / Codex

## Outcomes & Retrospective

已完成。HA 新增 `button.firmware_upgrade_button` 和 `text.ota_upgrade_url`。用户在 `OTA升级地址` 填入固件 bin URL 后，点击 `固件升级` 会直接复用现有 URL 升级流程。按用户后续要求，`Ota::CheckVersion()` 中此前注释掉的 firmware 解析逻辑保持注释状态，因此按钮不会恢复这段解析，也不会改变开机 OTA 检查行为。

验证通过：

    source /Users/resmo/esp/esp-idf/export.sh >/tmp/idf_export.log && idf.py build
    Project build complete. To flash, run:
     idf.py flash

未做实机 HA 联调，因此按钮发现和点击触发应用层入口仍需在设备上验证。

## Context and Orientation

`main/esphome/esphome_device.cc` 注册 HA 实体；已有 `WakeupButton` 是 `esphome::button::Button` 的示例。`main/application.cc` 管理 OTA 执行流程，`Application::UpgradeFirmware` 会关闭音频、下载固件、更新 HA 下载进度并成功后重启。`main/ota.cc` 负责调用 OTA 检查接口和解析版本信息。

## Plan of Work

保留 firmware JSON 解析注释，不恢复该段逻辑。然后在 `Application` 中新增 `StartFirmwareUpgrade(const std::string &url)`，通过 `Schedule` 在主事件循环中复用现有 `UpgradeFirmware(ota, url)`。最后在 ESPHome 中新增 `FirmwareUpgradeButton` 和 `OtaUpgradeUrlText`，按钮按下时读取 text 实体保存的 URL，并补中文和英文实体名称。

## Concrete Steps

在仓库根目录 `/Users/resmo/project/houzzkit-ai` 实现后运行：

    source /Users/resmo/esp/esp-idf/export.sh >/tmp/idf_export.log && idf.py build

## Validation and Acceptance

编译必须通过。HA 接入后应看到 `OTA升级地址` text 和 `固件升级` button。URL 为空时点击按钮应提示先填写地址；URL 非空时点击按钮会开始下载该 URL 指向的固件并更新 `OTA下载进度`。本次不恢复此前注释掉的 firmware 解析，因此不会改变开机或检查接口的固件解析行为。

## Idempotence and Recovery

本计划只新增按钮和手动 OTA 检查入口，不改 BLE 协议，不改 NVS。工作区已有无关未跟踪 `Makefile`，不得修改或提交。

## Interfaces and Dependencies

新增 C++ 接口：

    void Application::StartFirmwareUpgrade(const std::string &url);

新增 HA 实体：

    button.firmware_upgrade_button
    text.ota_upgrade_url

新增本地化 key：

    ESPHOME_ENTITY_BUTTON_NAME_FIRMWARE_UPGRADE
    ESPHOME_ENTITY_TEXT_NAME_OTA_UPGRADE_URL
