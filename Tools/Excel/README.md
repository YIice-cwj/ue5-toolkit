# 数据表自动化构建与类型安全管线 (DataTables Toolchain)

本项目包含一套基于 Excel 的零冗余全自动数据驱动管线，涵盖**跨表外键校验**、**C++ 强类型 RowStruct 与唯一键枚举生成**、以及 **UE5 标准 JSON 导出**。

---

## 1. 架构总览

```
[Excel 文件 (*.xlsx)]
        │ ▲
        │ │ 5. [json_to_excel.py] (逆向反写 / 写前快照 / 原子落盘 / 语义对拍 / 公式保护 / 回滚)
        │ │
        ├── 1. [validate_tables.py] ────────► 静态校验 (主外键完整性校验)
        │
        ├── 2. [main.py] ─────────────────► 生成 C++ RowStruct 与唯一键枚举 (UENUM)
        │                                     生成分类静态数据容器、强类型 Find(Key)
        │                                     与全局 UStaticDataSubsystem 载体（随域自动展开）
        │
        ├── 3. [excel_to_json.py] ──────────► 导出标准 DataTable JSON 供引擎/热更加载
        │                                      │
        │                                      ▼
        │                                [JSON 数据 (*.json)]
        │
        └── 4. [gameplay_tags_to_ini.py] ───► 汇总 FGameplayTag 列 → Config/DefaultGameplayTags.ini
                                               │
                                               ▼
                                    [GameplayTag 注册 (Config/DefaultGameplayTags.ini)]
```

---

## 2. 快速使用

脚本为**配置驱动**：不内置任何默认路径，所有工具（校验/C++生成/JSON导出/JSON反写）必须提供配置文件。
先复制示例配置为实际配置（路径支持相对配置文件所在目录的相对路径，工程整目录搬迁零改动）：

```bash
cp generator_config.example.json generator_config.json
```

在工程根目录或 `Tools/Excel` 目录下执行（macOS / Linux 用 `generate_all_tables.sh`，Windows 用 `generate_all_tables.bat`，两者均转发至同一跨平台调度器）：

```bash
# 1. 正向全流程（校验 -> 代码与枚举生成 -> JSON导出）
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json

# 2. 仅执行数据表外键完整性校验
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --validate

# 3. 仅生成 C++ 代码与枚举
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --struct

# 4. 仅导出 JSON 数据
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --json

# 5. 逆向同步预览（对比 JSON 与 Excel 差异，零磁盘修改）
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --reverse --dry-run

# 6. 逆向反写回 Excel（写前快照 + 原子落盘；写盘后自动校验，失败自动回滚）
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --reverse

# 7. 仅逆向反写指定单表
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --reverse --table DT_Example_Table

# 8. 回滚到写前快照（缺省取最新快照；可配合 --snapshot / --table）
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --rollback

# 9. 往返幂等自检（正向 → 反向 → 正向 逐字节比较；真实 Excel 只读，副本上执行）
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --roundtrip

# 10. 仅生成 GameplayTag 注册 ini（依据 Excel 的 FGameplayTag 列 -> Config/DefaultGameplayTags.ini）
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --tags
```

### 3.1 反向同步安全语义

| 环节 | 语义 |
| :--- | :--- |
| **写前快照** | 仅在确有变动时，改动前把**原文件**复制到 `DataTables_Backup/YYYYMMDD_HHMMSS/`，并落 `_manifest.json`（含每表 `sha1` 与 `excel_dir`） |
| **原子落盘** | 先写 `<名称>.tmp` → 备份原文件 → `os.replace` 原子替换；保存/替换任一步失败都会清理临时文件且**原文件零触碰** |
| **后验回滚** | `--reverse` 写盘后自动跑 `validate_tables.py`；校验不通过时自动执行 `--rollback`（`--no-backup` 时无快照可回滚，会明确提示） |
| **转换失败** | 值无法按表头声明类型转换时**跳过该单元格并汇总告警**（不再静默写 0）；`--strict` 时计入退出码 |
| **形态归一（UE 导出形态 → Excel 可读值）** | 反向入参若为 UE 侧导出形态，先归一为 Excel 可读值再比较与写入：`FText/FString` 列 `NSLOCTEXT("","K","文本")`/`LOCTEXT` → 显示文本；`FGameplayTag` 列 `(TagName="A.B")` → 标签名（空标签归为空值）；软对象列字符串 `None` → 空值。故“Excel `白僵`”与“JSON `NSLOCTEXT(...,"白僵")`”判为**等价**，不会产生无意义重写 |
| **非派生 JSON 告警** | 若某表 JSON 含 UE 文本字面量（说明该 JSON 是 UE 侧导出物、而非本工具正向派生），反向会先归一并在报告中打出 `[非派生 JSON]` 告警，提示“本次回写属预期规范化” |
| **结构漂移报告** | 逐表汇总 `JSON 多余字段` 与 `Excel 存在但 JSON 缺失的字段列`，报告 `[结构漂移]`（此前完全静默）；`--strict` 时计入退出码 |
| **写前规则校验** | 落盘前按 [`Script/table_rules.py`](Script/table_rules.py)（与 `validate_tables.py` **共用同一规则真源**）校验 JSON 侧主键：缺失 / 非法字符 / 重复；违规单元格不写入，`--strict` 时整体终止且原文件零触碰 |
| **`--strict` 阻断范围** | 以下任一命中即中止写入（原文件零触碰）：类型违规、主键规则违规、结构漂移、`--allow-delete` 且目标表含公式 |
| **`--allow-delete` 遇公式表** | 目标表含计算公式时默认**仅告警**（物理删行可能使公式位移/变 `#REF!`），`--strict` 下改为**阻断** |
| **数组书写规范** | 数组/集合字段统一以**标准 JSON 数组语法**书写（如 `["a", "b"]`、`[1, 2, 3]`，反向写入亦为此格式）；比较按“元素序列语义等价”，故 `a,b,c` 与 `["a","b","c"]` 视为相等、不会产生无意义重写 |
| **已知限制** | 写回沿用 openpyxl 原位加载保存：保留文本/样式/列宽/单元格公式，但**不保证**保留单元格图片与嵌入对象、VBA、部分图表与透视缓存（参考“任一单元格变动即整本重写”） |

跨平台单点调度入口：[`Script/run_pipeline.py`](Script/run_pipeline.py)（与 `.sh` 同语义，`generate_all_tables.bat` 为其薄封装，`python3 Script/run_pipeline.py --struct` 可直接调用）。

未提供 `--config`（或环境变量 `UE_TOOL_CONFIG` / `UE_STATIC_DATA_CONFIG`）时脚本直接报错退出，绝无静默默认路径。

## 5. 通用化与配置（无内置默认值）

全部工具脚本（`validate_tables` / `main.py` / `excel_to_json` / `json_to_excel`）均为**纯配置驱动**：
路径与参数全部来自配置文件，脚本内不包含任何项目路径或特化字面量的硬编码。
配置统一由共享加载器 `Script/tool_config.py` 校验，必需键缺失时打印缺失清单并以非零码退出。
优先级：**配置文件为唯一来源**（`--config` > 环境变量 `UE_TOOL_CONFIG` / `UE_STATIC_DATA_CONFIG` > 当前目录 `generator_config.json` > 脚本所在工具目录 `generator_config.json`）；
生成器额外支持命令行对配置字段的覆盖（配置 < CLI）。

| 配置键 | 使用脚本 | 说明 |
|--------|---------|------|
| `data_tables_source_dir` | 全部 | Excel 源目录（校验/生成/导出/反写共用，相对路径按配置文件目录展开） |
| `output_module_dir` | struct | 生成产物输出**模块根**（如 `Source/MyGame`），派生出 `Public/Generated/DataTables`（RowStruct）、`Public/Generated/Data/StaticData`（声明）、`Private/Generated/Data/StaticData`（实现） |
| `api_macro` | struct | 生成类型携带的 `XXX_API` 宏名 |
| `project_prefix` | 全部 | 项目通用大写前缀（如 `MyGame`），未配置时自动从 `api_macro` 智能推导 |
| `blueprint_category` | struct | 蓝图函数与属性所属类目（默认 `{project_prefix}|StaticData`） |
| `log_config` | struct | 统一日志配置对象（`header`, `macro`, `category_name`） |
| `console_commands` | bin / 反写 | 控制台命令名称映射对象（`export_bin`, `channel_cmd_arg`） |
| `registration` | struct | 统一注册类段（`output_header`, `source_roots`, `class_name`） |
| `back_write` | reverse | 反向写回配置段（`backup_dir`, `max_backups`；`protect_formulas` 为**预留键，当前未被消费**——公式保护由 CLI `--force-formula-overwrite` 控制） |
| `subsystem_header_include` | struct | 生成代码中子系统头相对 include 路径 |
| `row_types_include_prefix` | struct | RowStruct 头 include 前缀（默认 `Generated/DataTables`） |
| `static_data_include_prefix` | struct | 静态数据声明 include 前缀（默认 `Generated/Data/StaticData`） |
| `json_root_dir_name` | struct | 运行时 `LoadAllFromJson`/热重载侦测的 JSON 根目录名 |
| `json_output_dir` | json / reverse | JSON 导出输出与反写读取目录 |
| `gameplay_tags_ini_path` | tags | GameplayTag 注册 ini 输出路径（`excel_to_json` 之外的 `--tags` 步骤；相对路径按配置文件目录展开） |
| `foreign_keys_file` | validate | 可选；外置外键规则文件路径（未配置则不加载外置规则） |

```bash
# 查看生效配置（--config 必填）
./Tools/Excel/generate_all_tables.sh --config generator_config.json --struct --print-config

# 配置示例（字段格式参照 generator_config.example.json），生成器支持 CLI 覆盖：
./Tools/Excel/generate_all_tables.sh --config generator_config.json --struct --api-macro OtherApi
```

> 修改输出模块/API 宏等配置会改变生成产物；若模块重命名，需同步更新宿主模块 Build.cs 依赖与 `.uproject` 插件注册。

### 5.1 生成产物落层与 stale 回收

- **落层**：声明（RowStruct / Query / Service / Interface / 容器 / 子系统头）统一落 `{输出模块}/Public/Generated`，反序列化实现落 `{输出模块}/Private/Generated`——生成域与手写域物理隔离，表增删零人工移动文件；
- **stale 回收**：管线在收尾对比生成域现状与本轮产出清单，**删除已移除数据表遗留的孤儿 Row/Query 文件与空目录**；
- **preserve 契约**：Query / Service / Interface 骨架首次生成后不再覆盖（开发者扩展安全），仅 overwrite 类产物（RowStruct / 容器 / 汇总 / 子系统 / 注册入口）每轮全量重写。

---

## 3. 表头契约 (Header Contract)

Excel 工作表中前 3 行为契约行：
1. **第 1 行**：字段说明 / 注释（作为 C++ UPROPERTY 的 DisplayName 与文档注释）
2. **第 2 行**：C++ 字段名（必须为合法小写/下划线标识符，如 `zombie_name`, `base_health`）
3. **第 3 行**：类型签名（如 `FName`, `FString`, `int32`, `float`, `bool`, `FGameplayTag`, `TArray<int32>`, `TSoftObjectPtr<UAnimMontage>` 等）
4. **第 4 行及之后**：实际数据记录。

---

## 4. 关键特性

### 4.1 编译前 Python 外键静态校验
- 由 `validate_tables.py` 自动扫描分表依赖（如 `attributes_id -> DT_僵尸基础属性表`、`evolution` 进化前后键依赖等）。
- 发现悬空引用或数据类型异常直接报错中断，无需将校验逻辑耦合进 C++ 游戏运行时代码，零运行时开销。

### 4.2 强类型唯一键枚举（消除魔术字符串）
- 为每张数据表自动生成对应 `UENUM(BlueprintType) enum class E{Stem}Key : uint8`。
- 提供 `ToRowName(Key)` 与 `ToString(Key)` 高性能内联映射函数。
- 生成的分类静态数据容器（如 `F<Category>StaticData`）由全局 `UStaticDataSubsystem` 持有，容器针对每张分表提供类型重载的 `Find(E{Stem}Key Key)` 接口，彻底杜绝输入拼写错误，享受 IDE 自动补全与编译期安全。
