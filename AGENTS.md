# AGENTS.md

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
