# Houzzkit 关键调用链路图

## 1. 文档目的

这份文档用于梳理 `houzzkit-ai` 当前最值得优先掌握的关键调用链路，帮助接手时快速理解：

- 设备是如何启动的
- 唤醒词和手动唤醒如何进入主对话流程
- Home Assistant 文本实体如何触发设备行为
- BLE 设置项如何同步到设备内部状态
- OTA 如何从外部入口进入升级流程

本文以“主干流程”为主，省略部分异常处理和细节分支。

## 1.1 总览图

![Houzzkit Module Relationship](images/module-relationship.png)

## 2. 启动链路

核心文件：

- [`houzzkit-ai/main/main.cc`](houzzkit-ai/main/main.cc)
- [`houzzkit-ai/main/application.cc`](houzzkit-ai/main/application.cc)
- [`houzzkit-ai/main/esphome/esphome_device.cc`](houzzkit-ai/main/esphome/esphome_device.cc)

### 2.1 启动流程图

![Houzzkit Startup Flow](images/startup-flow.png)

### 2.2 关键理解

- `app_main()` 是固件入口。
- `ESPHomeDevice::setupPreferences()` 会先从本地持久化配置恢复音量、麦克风、连续对话、睡眠模式等状态。
- `Application::Start()` 是真正的系统装配入口。
- 启动阶段会拉起音频服务、主事件循环、ESPHome 组件循环，并完成版本检查和协议初始化。

## 3. 唤醒与对话主链路

核心文件：

- [`houzzkit-ai/main/application.cc`](houzzkit-ai/main/application.cc)
- [`houzzkit-ai/main/audio/audio_service.cc`](houzzkit-ai/main/audio/audio_service.cc)
- [`houzzkit-ai/main/protocols/protocol.cc`](houzzkit-ai/main/protocols/protocol.cc)

### 3.1 唤醒词触发链路

![Houzzkit Wakeword Dialogue Flow](images/wakeword-dialogue-flow.png)

### 3.2 手动唤醒链路

手动唤醒入口不止一个，很多板型按键最终都会落到：

- `Application::ToggleChatState()`

此外在 ESPHome 中也注册了一个按钮实体：

- `WakeupButton::press_action() -> Application::ToggleChatState()`

流程图如下：

![Houzzkit Manual Wakeup Flow](images/manual-wakeup-flow.png)

### 3.3 对话中音频发送链路

音频发送属于唤醒后的主链路一部分，已合并在上面的“唤醒与对话主链路”图片中展示。

### 3.4 关键理解

- `Application` 是唤醒、聆听、说话、停止的总状态机。
- `AudioService` 负责采音、唤醒词检测、录音数据输出和播放控制。
- `Protocol` 负责和服务端建立控制与音频通道。
- 后期引入了 `MQTT + UDP` 后，音频承载和控制消息已不再只有单一链路。

## 4. Home Assistant 文本实体调用链路

核心文件：

- [`houzzkit-ai/main/esphome/esphome_device.cc`](houzzkit-ai/main/esphome/esphome_device.cc)
- [`houzzkit-ai/main/application.cc`](houzzkit-ai/main/application.cc)
- [`houzzkit-ai/main/protocols/protocol.h`](houzzkit-ai/main/protocols/protocol.h)

当前比较关键的文本实体有 3 个：

- `play_voice_text`
- `execute_command_text`
- `ask_and_execute_command_text`

### 4.1 播放文字转语音链路

![Houzzkit HA Text Flow](images/ha-text-flow.png)

### 4.2 执行命令链路

执行命令与播放文字、询问后执行共用同一张 Home Assistant 文本实体总图。

### 4.3 询问后执行链路

询问后执行与播放文字、执行命令共用同一张 Home Assistant 文本实体总图。

### 4.4 关键理解

- 这条链路是项目中很重要的产品能力增量。
- `ESPHomeDevice` 是 Home Assistant 和设备内部业务之间的桥接层。
- `Application` 把文本输入统一转成协议动作。
- 最终真正落地给服务端的是 `Protocol` 提供的几个文本类发送接口。

## 5. BLE 设置同步链路

核心文件：

- [`houzzkit-ai/main/ble/ble_manager.cc`](houzzkit-ai/main/ble/ble_manager.cc)
- [`houzzkit-ai/main/esphome/esphome_device.cc`](houzzkit-ai/main/esphome/esphome_device.cc)
- [`houzzkit-ai/main/settings.cc`](houzzkit-ai/main/settings.cc)

### 5.1 BLE 下发设置到设备

以“麦克风开关”和“睡眠模式”为代表，流程类似：

![Houzzkit BLE Settings Flow](images/ble-settings-flow.png)

### 5.2 设备设置变更回推 BLE

BLE 回推链路已包含在同一张 BLE 设置同步总图中。

### 5.3 关键理解

- BLE 既是配网入口，也是本地设置同步通道。
- `ESPHomeDevice` 和 `BLEManager` 存在双向联动。
- 若后续出现“HA 上设置变了，但小程序没变”或反过来的问题，这条链路要优先排查。

## 6. 配网链路

核心文件：

- [`houzzkit-ai/main/ble/ble_manager.cc`](houzzkit-ai/main/ble/ble_manager.cc)
- [`houzzkit-ai/main/boards/common/wifi_board.cc`](houzzkit-ai/main/boards/common/wifi_board.cc)
- [`houzzkit-ai/components/esp-wifi-connect`](houzzkit-ai/components/esp-wifi-connect)

![Houzzkit WiFi And OTA Flow](images/wifi-ota-flow.png)

## 7. OTA 链路

核心文件：

- [`houzzkit-ai/main/ota.cc`](houzzkit-ai/main/ota.cc)
- [`houzzkit-ai/main/application.cc`](houzzkit-ai/main/application.cc)
- [`houzzkit-ai/main/ble/ble_manager.cc`](houzzkit-ai/main/ble/ble_manager.cc)

### 7.1 启动阶段自动检查 OTA

配网与 OTA 已合并在同一张总图中展示，见上图。

### 7.2 BLE 触发 OTA

BLE 触发 OTA 也已合并到上面的配网与 OTA 总图中。

## 8. 接手时建议优先读懂的链路

如果时间有限，建议优先掌握下面 4 条：

1. 启动链路
2. 唤醒与对话链路
3. Home Assistant 文本实体链路
4. BLE 设置同步链路

原因如下：

- 这 4 条链路覆盖了设备启动、核心交互、HA 集成和配置管理
- Git 历史中的大多数新增功能最终都落在这几条主干上
- 后续排查问题时，绝大多数 bug 都能先落到这些链路中的某一段

## 9. 后续建议补充的图

如果后续继续完善文档，建议再补以下几类图：

- `DeviceState` 状态流转图
- `WebSocket` 与 `MQTT + UDP` 协议对比图
- 音频输入、唤醒词检测、编码与播放时序图
- BLE 协议命令字到处理函数的映射表
- 当前主板型的硬件模块装配图
