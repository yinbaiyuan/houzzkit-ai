# Waveshare ESP32-P4-86-Panel-ETH-2RO

本目录提供 `ESP32-P4-86-Panel-ETH-2RO` 的最小板级适配，当前优先保证以下能力：

- 设备可以正常点亮屏幕、触摸、音频和基础 UI
- 通过 `ESP32-P4 + ESP32-C6` 的 `esp_hosted` 链路使用 `Wi-Fi`
- 优先走蓝牙配网

当前仍属于最小接入范围，额外外设暂未在本目录内接通：

- 以太网
- RS485
- 双继电器

如果要使用蓝牙配网，板载 `ESP32-C6` 需要烧录与当前 `esp_hosted` 版本匹配、且开启蓝牙控制器的从机固件；本仓库本次改动只生成 `ESP32-P4` 主控固件，不会顺带更新 `C6` 从机固件。

## 配置

在 `menuconfig` 中选择：

- `Xiaozhi Assistant -> Board Type -> Waveshare ESP32-P4-86-Panel-ETH-2RO`

发布包命令：

    python3 scripts/release.py waveshare-p4-86-panel-eth-2ro --name waveshare-p4-86-panel-eth-2ro

## 说明

本板与 `Waveshare ESP32-P4-WIFI6-Touch-LCD-4B` 的基础显示、触摸、音频和 `ESP32-C6` 连接资源接近，所以最小适配直接复用了这部分板级实现；但两块板的扩展外设结构不同，因此不能视为完全相同的板型。
