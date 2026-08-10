# Waveshare ESP32-P4-86-Panel-ETH-2RO

硬件资料：

- [Waveshare 官方产品页](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4b.htm)
- [Waveshare 官方 Wiki](https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-4B)

本目录提供 `ESP32-P4-86-Panel-ETH-2RO` 的最小板级适配，当前优先保证以下能力：

- `720 × 720` MIPI-DSI 屏幕、GT911 触摸、音频和基础 UI
- ES8311 音频输出和 ES7210 麦克风输入，输入/输出采样率均为 `16 kHz`
- 通过 `ESP32-P4 + ESP32-C6` 的 `esp_hosted` 链路使用 `Wi-Fi`
- 优先走蓝牙配网

当前仍属于最小接入范围，额外外设暂未在本目录内接通：

- 以太网
- RS485
- 双继电器

如果要使用蓝牙配网，板载 `ESP32-C6` 需要烧录与当前 `esp_hosted` 版本匹配、且开启蓝牙控制器的从机固件；本仓库本次改动只生成 `ESP32-P4` 主控固件，不会顺带更新 `C6` 从机固件。

## 编译配置

先激活本机 ESP-IDF 环境并设置目标芯片：

```bash
source /path/to/esp-idf/export.sh
idf.py set-target esp32p4
```

在 `menuconfig` 中选择：

- `Xiaozhi Assistant -> Board Type -> Waveshare ESP32-P4-86-Panel-ETH-2RO`

```bash
idf.py menuconfig
idf.py build
```

## 烧录和日志

将 `<PORT>` 替换为实际串口，例如 macOS 下的 `/dev/cu.usbmodem*` 或 Linux 下的 `/dev/ttyACM*`：

```bash
idf.py -p <PORT> flash monitor
```

退出串口监控使用 `Ctrl+]`。

如果需要先生成合并固件再烧录：

```bash
idf.py merge-bin
python -m esptool --chip esp32p4 -p <PORT> -b 460800 \
    write_flash 0x0 build/merged-binary.bin
```

以上命令只烧录 `ESP32-P4` 主控，不会更新板载 `ESP32-C6` 从机固件。

## 发布包

发布包命令：

```bash
python3 scripts/release.py waveshare-p4-86-panel-eth-2ro \
    --name waveshare-p4-86-panel-eth-2ro
```

## 说明

本板与 `Waveshare ESP32-P4-WIFI6-Touch-LCD-4B` 的基础显示、触摸、音频和 `ESP32-C6` 连接资源接近，所以最小适配直接复用了这部分板级实现；但两块板的扩展外设结构不同，因此不能视为完全相同的板型。
