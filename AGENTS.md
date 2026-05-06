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
