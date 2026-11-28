# GameDataSamples

> **一句话**：`GameData` 的示例域插件，演示 Excel 生成管线与通用分段存档的完整接入。
> **版本** v2.0.0 · **引擎** UE 5.1~5.8 · **层级** Level 3 · **直接依赖** GameData, GameCore, GameLogging

## 30 秒速览

- **解决什么问题**：提供一套可运行的 Item / Shop 生成域与 `PlayerProfile` 存档适配器，作为配表管线、静态数据查询和分段存档的参考实现。
- **核心能力**：
  - `Public/Generated` / `Private/Generated` 中由 `Tools/Excel` 生成的 Item、Shop 两套域代码；
  - `UGamePlayerDataRegistry` 管理等级 / 经验 / 金币等玩家动态数据；
  - `UGamePlayerProfileSaveAdapter` 以 `Player.Profile` 段接入 `USaveGameSubsystem`；
  - 示例域查询、冷启动与存档适配器自动化测试。
- **什么时候用**：学习或验证 Excel → C++ → 快照 → 存档的完整链路，或作为新业务域的起点。
- **什么时候不用**：通用安装与生产部署不需要本插件；`.scripts/install.sh --all` 默认跳过，示例生成代码也不会自动重绑。

## 快速开始

创建玩家数据中心，并把档案注册为通用存档段：

```cpp
#include "Dynamic/GamePlayerDataRegistry.h"
#include "Dynamic/PlayerDataTypes.h"
#include "Save/GamePlayerProfileSaveAdapter.h"
#include "Save/USaveGameSubsystem.h"

UGamePlayerDataRegistry* Registry = NewObject<UGamePlayerDataRegistry>(this);

FPlayerProfileData Profile;
Profile.PlayerId = TEXT("Player_01");
Profile.Progression.Level = 5;
Registry->SetProfile(Profile);

UGamePlayerProfileSaveAdapter* Adapter = NewObject<UGamePlayerProfileSaveAdapter>(this);
Adapter->Setup(USaveGameSubsystem::GetPtr(this), Registry);
```

生成的静态数据查询与 `UStaticDataSubsystem` 用法见 [GameData 快速上手](../GameData/Docs/Guides/QuickStart.md)。

## 核心能力

### Item / Shop 生成域

- `Public/Generated/DataTables` 提供 `FItemRow`、`FShopGoodsRow` 等 RowStruct。
- `Public/Generated/Data/StaticData/{Item,Shop}` 提供静态数据容器、快照、`F{Stem}Query`、`F{Domain}StaticDataInterface` 与 Service 脚手架。
- `FStaticDataSnapshot` / `FStaticDataCache` / `FStaticDataHotReloadWatcher` 展示快照发布与热更缓存结构。
- 生成代码仅存在于本仓库示例；安装到目标工程时由 `install.py` 重绑到 `GameData`。

### 玩家动态数据

- `UGamePlayerDataRegistry` 是普通 `UObject`（非子系统），由使用方自行创建与持有。
- 构造函数以默认档案值初始化 `CurrentProfile`，不触发日志与广播。
- `SetProfile` 覆盖档案并广播原生多播 `OnProfileChanged`；`ResetToDefault` 等价于写入默认档案并广播。
- `GetProfile` 返回 `const FPlayerProfileData&`，包含 `PlayerId`、`Progression{Level, Experience, Gold}` 与 `ArchiveVersion`。

### 档案存档适配器

- `UGamePlayerProfileSaveAdapter` 把档案注册为段名 `Player.Profile`、段版本 `1` 的存档段。
- `Setup(SaveSubsystem, Registry)` 先执行 `Teardown()` 保证幂等；任一参数为空时直接返回、不注册段。
- `Save` 按 `PlayerId → Level → Experience → Gold → ArchiveVersion` 顺序写入；`Load` 同序反序列化，段版本不符、读取出错或注册表失效时返回 `false`。
- 适配器只持有子系统和注册表的弱引用，`BeginDestroy()` 自动调用 `Teardown()` 注销段。

### 测试与导出

- `Private/Tests` 覆盖示例域查询、冷启动与存档适配器。
- 静态数据导出命令 `UGameStaticDataExportCommandlet` 与生成注册代码 `FStaticDataRegistration` 演示 Cook 前置导出流程。
- 自动化前缀 `Game.Data` 同时包含示例域测试与通用存档测试；示例域用例仅在安装本插件后可执行。

## 架构与依赖

| 项目 | 内容 |
| :--- | :--- |
| 插件版本 | v2.0.0 |
| 生成配置 | `output_module_dir` 指向本插件，`api_macro` 为 `GAMEDATASAMPLES_API` |
| 依赖 | `GameData`（含 `GameDataCore`）、`GameCore`、`GameLogging` |
| 生成代码 | `Public/Generated`、`Private/Generated`（仅本仓库示例） |

- 示例域：Item、Shop 两套 RowStruct / Query / Interface / Service。
- 存档适配：`UGamePlayerProfileSaveAdapter` 对接 `USaveGameSubsystem`。
- 安装到外部工程时，`install.py` 会把生成器配置重绑到 `Plugins/GameData/Source/GameData` 与 `GAMEDATA_API`，本插件不参与生成。
- 本插件不属于通用安装集合，需要单独安装或保留在母仓内使用。

## 性能与边界

- **安装边界**：`.scripts/install.sh --all` 默认跳过本插件，避免示例域进入生产工程。
- **生成边界**：生成代码只存在于母仓；重绑到目标工程后，本插件不再参与生成。
- **存档版本**：适配器固定段版本 `1`，`StoredVersion != 1` 时拒绝加载，不做迁移。
- **生命周期**：适配器持弱引用并在 `BeginDestroy` 注销段；`UGamePlayerDataRegistry` 由调用方负责生命周期。
- **数据源**：示例数据位于母仓 `DataTables` / `DataTables_Json`，安装脚本仅在目标缺少数据目录时注入。

<details>
<summary>展开性能基准（历史 Development 构建参考值）</summary>

示例插件没有独立性能用例，沿用 `GameDataCore` 的静态数据与存档基准：

| 基准用例 | 场景 | 预算 |
| :--- | :--- | :--- |
| `Game.Data.Perf.StaticData.RepositoryRoundTrip` | 内存 Add+Extract / 文件 Export+Import（1000 行） | < 2 ms / < 5 ms |
| `Game.Data.Perf.StaticData.SnapshotStore` | `Acquire` / 无锁 `GetVersion` | < 500 ns / < 100 ns |
| `Game.Data.Perf.SaveGame.SegmentRegistry` | 16 段构建 / 恢复 | < 150 μs / < 100 μs |
| `Game.Data.Perf.SaveGame.SubsystemRoundTrip` | 同步存读往返 | < 15 ms |

上表为测试预算上限，不是仓库固化的实测值。

</details>

## 文档导航

| 文档 | 内容 |
| :--- | :--- |
| [GameData 插件说明](../GameData/README.md) | 生成宿主、存档与云同步总览 |
| [GameData 快速上手](../GameData/Docs/Guides/QuickStart.md) | 静态数据查询与存档段接入 |
| [静态数据查询指南](../GameData/Docs/Guides/StaticDataQueryGuide.md) | 查询与聚合用例 |
| [存档接入指南](../GameData/Docs/Guides/SaveGameGuide.md) | 分段存档实践 |
| [静态数据子系统 API](../GameData/Docs/API/StaticDataSubsystem.md) | 生成域 `UStaticDataSubsystem` 接口 |
| [Excel 数据表工具链](../../Tools/Excel/README.md) | 生成器配置与输出 |
