# Repository Guidelines

## 项目结构与模块组织

本仓库是基于 ESP-IDF 的嵌入式固件项目，主要目录如下：

- `main/`：核心业务代码，`audio/`、`ble/`、`display/`、`esphome/`、`led/`、`protocols/` 分别负责音频、蓝牙、显示、HA 接入、灯效与通信协议。
- `main/boards/`：板级适配目录，每个开发板通常包含 `config.h`、`README.md`、可选 `config.json`。
- `components/`、`managed_components/`：组件依赖；`managed_components/` 视为上游代码，非必要不要直接修改。
- `docs/`、`partitions/`、`scripts/`：分别存放文档、分区表和构建/资源脚本。

## 构建、测试与开发命令

先准备 ESP-IDF 5.4+ 环境。

```bash
idf.py set-target esp32s3
idf.py menuconfig
idf.py -DBOARD_NAME=kevin-sp-v3-dev build
idf.py flash monitor
python scripts/release.py kevin-sp-v3-dev
```

`set-target` 选择芯片，`menuconfig` 调整板级与功能开关，`build` 编译当前固件，`flash monitor` 烧录并查看串口日志，`release.py` 按板级 `config.json` 产出打包固件。资源分区需要单独处理时，使用 `python scripts/spiffs_assets/build.py ...`。不要提交 `build/` 产物。

## 代码风格与命名约定

C/C++ 代码沿用现有风格：4 空格缩进，大括号另起一行，头文件使用相对模块路径引用。类名使用 `PascalCase`，函数与局部变量使用 `camelCase`，宏和编译开关使用 `UPPER_SNAKE_CASE`。新增板目录名使用短横线风格，如 `waveshare-c6-lcd-1.69`；板级源文件使用 `<board>_board.cc`。Python 脚本保持简洁命令式风格。Markdown 文档需遵守 `markdownlint` 常见规则，尤其是标题前后、列表前后保留空行。

## 沟通与文档语言

仓库协作默认全程使用中文，包括终端对话、代码评审说明、提交信息、PR 描述，以及在本仓库新增或更新的 Markdown、ExecPlan 等说明性文档。只有当文件内容本身明确要求保留英文标识、协议字段或上游接口原文时，才保留必要英文，其余说明文字应以中文表达。

## 测试规范

仓库当前没有顶层自动化单元测试；默认验证方式是 `idf.py build` 成功、目标板启动正常、关键链路可回归。涉及板级适配时，至少验证编译、启动日志、网络连接、音频链路或显示链路之一。新增脚本时，优先提供最小可复现命令示例。

除非用户明确要求，不要自动执行 `idf.py build`、`python scripts/release.py ...` 等耗时构建或打包命令；需要验证时先说明建议的命令，等待用户确认。

## 提交与合并请求规范

近期提交以简短中文说明为主，常见格式为版本更新或多条变更摘要，例如 `完善项目文档`、`修复mqtt连接情况下...`。建议继续使用中文祈使句，单次提交聚焦单一主题。PR 应包含变更目的、影响范围、验证命令；如果改动 `main/boards/`、UI 资源或协议行为，请附日志、截图或串口输出，并关联相关 Issue。

## 配置与资源注意事项

修改开发板时，优先在 `main/boards/<board>/config.json` 中追加配置，不要复用错误的板标识；这会影响 OTA 升级通道。`sdkconfig` 与 `sdkconfig.defaults*` 改动要说明目标芯片与 Flash 分区影响。资源文件较大，新增前确认分区容量与 `assets.bin` 大小。

## ExecPlans

编写复杂功能或进行较大重构（跨两轮及以上独立修改）时，应从设计到实现全程使用 ExecPlans（规范见 `.agent/PLANS.md`）。

## 项目概览

- 本仓库是 `houzzkit` 固件，基于 ESP-IDF 构建，根目录 `CMakeLists.txt` 当前版本号为 `2.1.2`。
- 项目分叉自 `78/xiaozhi-esp32`，并集成了 ESPHome 能力，用于面向 Home Assistant 的 AI 智能音箱固件开发。
- 当前仓库已经存在未提交改动。除非用户明确要求，agent 只能新增或修改与当前任务直接相关的文件，不能回退他人的变更。

## 目录约定

- `main/`：主业务代码。
- `main/application.*`：应用主流程和设备状态调度入口。
- `main/audio/`：音频编解码、音频服务、处理链。
- `main/protocols/`：通信协议实现，主要包括 MQTT 和 WebSocket。
- `main/boards/`：板级适配代码；每个板卡一个目录，通常包含 `README.md`、`config.json`、`config.h`、板级 `.cc` 文件。
- `components/`：独立组件与第三方依赖封装。
- `docs/`：协议、自定义板卡等开发文档。
- `partitions/`：分区表，当前主要使用 `partitions/v2/`。
- `scripts/`：资源生成、打包发布、版本处理等辅助脚本。
- `build/`：本地构建产物，不要手工编辑。
- `managed_components/`：ESP-IDF 管理组件目录，除非任务明确要求，否则不要手改。

## 常用命令

- 列出支持的板卡变体：
  - `python scripts/release.py --list-boards`
- 设置目标芯片：
  - `idf.py set-target esp32s3`
  - `idf.py set-target esp32c3`
  - `idf.py set-target esp32c6`
  - `idf.py set-target esp32p4`
- 打开配置界面：
  - `idf.py menuconfig`
- 本地编译：
  - `idf.py build`
- 烧录并查看日志：
  - `idf.py flash monitor`
- 合并固件：
  - `idf.py merge-bin`
- 按板卡配置自动打包：
  - `python scripts/release.py <board-dir>`
- 只编译某个变体：
  - `python scripts/release.py <board-dir> --name <variant-name>`

## 当前仓库状态

- 当前 `sdkconfig` 里启用的是：
  - `CONFIG_IDF_TARGET="esp32s3"`
  - `CONFIG_BOARD_TYPE_ZHENGCHEN_1_54TFT_WIFI=y`
  - `CONFIG_PARTITION_TABLE_FILENAME="partitions/v2/16m.csv"`
- 这只是当前工作区配置，不代表所有任务都应继续沿用；涉及板卡、Flash 大小、分区表的修改前，要先确认任务目标。

## 开发规则

- 优先做最小改动，保持现有目录结构和命名风格。
- 修改协议逻辑时，优先检查 `main/protocols/`、`main/application.*` 和相关头文件是否需要同步。
- 修改音频链路时，优先检查 `main/audio/`、对应板卡 codec 配置，以及是否影响采样率、I2S、AEC、唤醒词等配置。
- 修改显示或交互逻辑时，优先检查 `main/display/` 和对应板卡目录中的屏幕配置。
- 非必要不要改 `sdkconfig`、`sdkconfig.old`、`build/` 产物；只有当任务明确涉及配置切换或新板卡适配时才修改。
- 除非用户明确要求，不要顺手升级依赖、重排大段代码或批量格式化无关文件。

## 板卡适配规则

- 新增板卡时，必须在 `main/boards/<board-name>/` 下创建独立目录，不要复用现有板卡标识去“覆盖编译”。
- 新板卡通常至少需要：
  - `config.json`
  - `config.h`
  - 板级初始化 `.cc`
  - `README.md`
- 还需要在 `main/CMakeLists.txt` 中注册对应的 `CONFIG_BOARD_TYPE_*` 到 `BOARD_TYPE` 映射。
- `scripts/release.py` 会读取 `main/boards/*/config.json` 来构建变体；`build.name` 必须以板卡目录名开头。
- 不要随意复用已有板卡的 OTA 身份。文档已明确说明：覆盖原板卡标识会带来 OTA 升级串线风险。

## 验证预期

- 能编译时，优先做与改动范围匹配的最小验证，至少尝试 `idf.py build`。
- 涉及板卡、资源或打包流程时，可补充使用 `idf.py merge-bin` 或 `python scripts/release.py ...` 验证。
- 如果本地缺少 ESP-IDF 环境、工具链或硬件，需在交付说明里明确写出未验证项和阻塞原因。

## MR 创建规范

- 创建 MR 时，应自动基于当前分支对目标分支（默认 `main`）的提交记录与文件差异生成 MR 描述。
- MR 描述至少包含：变更目的、主要变更、影响范围、验证情况；涉及板级、协议、资源或 UI 行为时，应补充对应日志、截图、串口输出或未验证原因。
- 如果受限于本地工具、权限或 GitLab token 无法直接写入 MR 描述，应在最终回复中提供可直接粘贴到 MR 的完整 Markdown 描述。

## 文档同步

- 新增板卡、协议行为、构建方式或资源生成流程时，同步更新对应 `README.md` 或 `docs/` 文档。
- 如果任务只涉及局部实现修复，文档无需机械更新，但要在最终说明里写清楚行为变化。
