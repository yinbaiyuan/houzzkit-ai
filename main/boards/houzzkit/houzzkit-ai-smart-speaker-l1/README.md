# Houzzkit Smart Speaker

## 简介

`houzzkit-smart-speaker` 是 Houzzkit 的 ESP32-S3 音箱设备板级适配。该设备以音频交互为主，当前按无屏设备接入：不初始化 LCD，也不启用 Emote 显示链路。

## 硬件特性

- **主控**: ESP32-S3
- **音频**: ES8311 音频 Codec + ES7210 麦克风 ADC
- **音频功能**: 支持设备端 AEC
- **按键**: Boot、播放、音量加、音量减
- **状态控制**: GPIO1 用于麦克风使能状态检测
- **指示灯**: 单 GPIO LED

## 配置与编译

先配置编译目标为 ESP32-S3：

```bash
idf.py set-target esp32s3
```

通过 `menuconfig` 选择板型：

```bash
idf.py menuconfig
```

在 `Device Configure Options` → `Board Type` 中选择 `Houzzkit Smart Speaker`。

也可以直接使用构建参数编译：

```bash
idf.py -DBOARD_NAME=houzzkit-smart-speaker build
```

发布打包使用：

```bash
python3 scripts/release.py houzzkit-smart-speaker
```

## 默认构建配置

`config.json` 中默认构建名为 `houzzkit-smart-speaker`，并启用：

```text
CONFIG_USE_DEVICE_AEC=y
```

该构建名需要与板目录名前缀保持一致，以适配现有发布脚本和 OTA 包命名规则。

## 按键说明

- **Boot 单击**: 配网阶段重置 WiFi 配置；其他状态切换对话状态。
- **Boot 双击**: 在空闲状态切换设备端 AEC 开关。
- **播放键单击**: 配网阶段重置 WiFi 配置；其他状态切换对话状态。
- **音量加/减单击**: 按 10% 步进调整输出音量。
- **音量加长按**: 设置为最大音量。
- **音量减长按**: 静音。
