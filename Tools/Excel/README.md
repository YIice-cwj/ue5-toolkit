# 数据表自动化构建与类型安全管线 (DataTables Toolchain)

本项目包含一套基于 Excel 的零冗余全自动数据驱动管线，涵盖**跨表外键校验**、**C++ 强类型 RowStruct 与静态数据层生成**、**UE5 标准 JSON 导出**、**GameplayTag 注册清单生成**以及**双向安全反写与往返自检**。

---

## 1. 架构总览

```text
[Excel 文件 (*.xlsx)]
        │ ▲
        │ │ 5. [json_to_excel.py] (逆向反写 / 秒内防冲突写前快照 / SHA-1 校验回滚 / 公式保护)
        │ │
        ├── 1. [validate_tables.py] ────────► 静态校验 (主外键完整性、全系列标量类型、损坏文件阻断)
        │
        ├── 2. [main.py] ───────────────────► 生成 C++ RowStruct、免反射序列化器、容器、快照别名与
        │                                     UStaticDataSubsystem（事务暂存树原子换入，零污染 Source/）
        │
        ├── 3. [excel_to_json.py] ──────────► 导出标准 DataTable JSON（合法空表导出 []，原子落盘）
        │                                      │
        │                                      ▼
        │                                [JSON 数据 (*.json)]
        │
        └── 4. [gameplay_tags_to_ini.py] ───► 汇总 FGameplayTag / TArray<FGameplayTag> 列
                                               │
                                               ▼
                                    [Config/DefaultGameplayTags.ini]
```

---

## 2. 快速使用

脚本为**纯配置驱动**：所有工具（校验 / C++ 生成 / JSON 导出 / GameplayTag 生成 / JSON 反写）统一复用 [`Tools/Script/tool_config.py`](../Script/tool_config.py) 读取配置文件，不内置任何工程硬编码路径。

```bash
# 1. 正向全流程（校验 -> C++ 生成 -> JSON 导出 -> GameplayTag 注册）
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --all

# 2. 仅执行数据表外键完整性校验
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --validate

# 3. 仅生成 C++ 代码
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --struct

# 4. 仅导出 JSON 数据
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --json

# 5. 仅生成 GameplayTag 注册 ini
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --tags

# 6. 仅导出运行期 .bin（Cook 前置，需编辑器已编译）
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --bin

# 7. 逆向同步预览（对比 JSON 与 Excel 差异，零磁盘修改）
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --reverse --dry-run

# 8. 逆向反写回 Excel（写前快照 + 原子落盘；写盘后自动校验，失败自动回滚）
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --reverse

# 9. 仅逆向反写指定单表（可选配合 --allow-delete / --force-formula-overwrite / --fill-defaults / --float-epsilon 1e-6 / --no-backup / --strict）
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --reverse --table DT_Item

# 10. 回滚到写前快照（校验 SHA-1 后原子还原；可配合 --snapshot / --table）
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --rollback

# 11. 往返幂等自检（正向 → 反向 → 正向逐字节比较；真实 Excel 只读，可配合 --dry-run / --keep-temp）
./Tools/Excel/generate_all_tables.sh --config Tools/Excel/generator_config.json --roundtrip
```

---

## 3. 反向同步安全语义

| 环节 | 语义 |
| :--- | :--- |
| **防冲突写前快照** | 仅在确有变动时复制原文件至 `DataTables_Backup/YYYYMMDD_HHMMSS[_NN]/`，原子落盘 `_manifest.json`（含每表 `sha1` 与 `excel_dir`） |
| **原子落盘** | 先写 `<名称>.tmp` → 备份原文件 → `os.replace` 原子替换；任一步失败清理临时文件且原文件零触碰 |
| **SHA-1 校验回滚** | `--rollback` 先核对备份文件 SHA-1 与清单一致，再以 `.tmp` + `os.replace` 原子还原，防止篡改或损坏的快照污染正式表 |
| **后验回滚** | `--reverse` 写盘后自动跑 `validate_tables.py`；校验不通过时自动回滚至本次写前快照 |
| **形态归一** | 自动将 UE 导出字面量归一为 Excel 可读形态：`NSLOCTEXT`/`LOCTEXT` → 显示文本；`(TagName="A.B")` → 标签名；软对象 `None` → 空值 |
| **结构漂移与严格模式** | 逐表汇总 JSON 多余字段与 Excel 缺失列（空 JSON 数组不误报漂移）；`--strict` 下命中类型违规、主键违规、结构漂移或公式表删行即阻断写入 |
| **数组书写规范** | 数组/集合字段（`TArray<T>` / `TSet<T>`）统一以标准 JSON 数组语法写出，比较按元素序列语义等价 |

---

## 4. 配置驱动与发现优先级

配置加载器唯一真源为 [`Tools/Script/tool_config.py`](../Script/tool_config.py)，发现优先级：

`--config <path>` > 环境变量 `UE_TOOL_CONFIG` > 当前目录 `generator_config.json` > `Tools/Excel/generator_config.json`

| 配置键 | 使用脚本 | 说明 |
| :--- | :--- | :--- |
| `data_tables_source_dir` | 全部 | Excel 源目录（相对路径按工程根展开，`.` 开头相对配置文件目录展开） |
| `output_module_dir` | struct | 生成产物输出模块根（如 `Plugins/GameData/Source/GameData`） |
| `api_macro` | struct | 模块导出宏（缺省按 `output_module_dir` 末级目录名自动推导） |
| `project_prefix` | 全部 | 项目前缀（默认 `Game`，用于推导控制台命令、注册类与蓝图分类） |
| `log_config` | struct | 日志配置（默认统一使用 `GameLogging`：`Logging/GameLogCategories.h` / `GAME_LOG`） |
| `row_key_mode` | struct | 行键模式：`fname`（默认，无行数上限）或 `enum`（生成 `E{Stem}Key : uint8`，含 `None = 0` 共 255 枚举项，最多支持 254 行数据） |
| `nested_struct_types` | validate / struct / json / roundtrip | 嵌套 USTRUCT 映射（类型名 -> 头文件 include 相对路径） |
| `back_write` | reverse | 反向回写配置段（`backup_dir`, `max_backups`, `protect_formulas` 均已生效消费） |
| `json_output_dir` | json / reverse | JSON 导出输出与反写读取目录 |
| `gameplay_tags_ini_path` | tags | GameplayTag 注册 ini 输出路径 |
| `foreign_keys_file` | validate | 可选外置外键规则文件路径 |

### 生成产物落层与事务化换入

- **零污染暂存树**：`GenerationStaging` 将暂存树置于插件/工程的 `Intermediate/GameDataGenStaging/` 下，绝不在 `Source/<Module>/` 中创建 `Intermediate` 目录；
- **事务原子替换**：所有产物全部生成无误后一次性原子替换 `Public/Generated` 与 `Private/Generated`（自动回收已删除分类与孤儿产物，任意单表解析或校验失败则整体回滚）；
- **preserve / repair 契约**：`Query` / `Service` 首次生成后保留；`Interface` 在保持用户自定义代码的同时支持损坏检测修复。

---

## 5. 表头契约 (Header Contract)

Excel 工作表前 3 行为契约行：

1. **第 1 行**：字段说明 / 中文名（自动转义为 C++ `DisplayName` 与多行 Doxygen `@brief`）
2. **第 2 行**：字段名（首列必须为 `RowName` 或 `Name` 作为主键；除精确主键 `"Name"` 外，禁止在表头字段行使用 `"name"` 及其任意大小写变体如 `name`/`NAME`）
3. **第 3 行**：C++ 类型签名（支持标量 `bool`/`float`/`double`/`int8..64`/`uint8..64`、`FName`/`FString`/`FText`/`FGameplayTag`、UE 数学结构体 `FVector`/`FIntPoint`/`FIntVector` 等、`TSoftObjectPtr<T>`/`TSoftClassPtr<T>`、`TArray<T>`/`TSet<T>` 及已登记的嵌套 USTRUCT）
4. **第 4 行起**：数据记录（具备完整 3 行表头但无数据行的合法空表导出为空数组 `[]`）。

---

## 6. 自动化测试与性能微基准

工具链配套完整的标准库 `unittest` 测试套件与 1000 行规模微基准测试，位于 [`Tools/Excel/Tests/`](Tests/)：

```bash
python3 -m unittest discover -s Tools/Excel/Tests -v
```
