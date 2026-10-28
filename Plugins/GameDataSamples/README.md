# GameDataSamples 插件

**GameDataSamples** 是 `GameData` 插件的示例域插件，用于演示 Excel 生成管线与通用分段存档的完整接入方式。

> `.scripts/install.sh --all` 默认跳过本插件，不随通用安装部署到目标工程。

---

## 内容

| 内容 | 说明 |
| :--- | :--- |
| `Public/Generated` / `Private/Generated` | Item、Shop 两个示例域由 `Tools/Excel` 生成（RowStruct、静态数据容器、快照缓存、Query / Interface / Service 脚手架） |
| `Dynamic/GamePlayerDataRegistry` | 示例玩家动态数据中心（等级 / 经验 / 金币） |
| `Save/GamePlayerProfileSaveAdapter` | 以 `Player.Profile` 存档段接入 `USaveGameSubsystem`，演示段注册与版本迁移 |
| `Private/Tests` | 示例域查询、冷启动、存档适配器测试 |

依赖：`GameData`（含 `GameDataCore`）、`GameCore`、`GameLogging`。

---

## 生成配置

示例工程使用 `Tools/Excel/generator_config.json`：

```json
{
    "output_module_dir": "Plugins/GameDataSamples/Source/GameDataSamples",
    "api_macro": "GAMEDATASAMPLES_API"
}
```

安装到外部工程时，`install.py` 会把生成器配置重绑到 `Plugins/GameData/Source/GameData` 与 `GAMEDATA_API`，示例插件不参与生成。

---

## 示例 API：玩家数据与存档段

### `UGamePlayerDataRegistry`（`Dynamic/GamePlayerDataRegistry.h`）

普通 `UObject`（非子系统），由使用方自行创建与持有；构造函数内部调用 `ResetToDefault()`。

| 成员 | 实际行为 |
| :--- | :--- |
| `GetProfile()` | 返回 `const FPlayerProfileData&`（`PlayerId` / `Progression{Level, Experience, Gold}` / `ArchiveVersion`） |
| `SetProfile(InProfile)` | 覆盖当前档案，并广播 `OnProfileChanged(CurrentProfile)` |
| `ResetToDefault()` | 等价于 `SetProfile(FPlayerProfileData())`：`Level=1`、`Experience=0`、`Gold=0`、`PlayerId` 为空、`ArchiveVersion=1`，同样触发广播 |
| `OnProfileChanged` | 原生多播 `FOnPlayerProfileChanged(const FPlayerProfileData&)`（非动态委托，蓝图不可见） |

### `UGamePlayerProfileSaveAdapter`（`Save/GamePlayerProfileSaveAdapter.h`）

把玩家档案接入通用存档流程，段名为 `Player.Profile`（`SegmentName` 常量），段版本 `1`。

- `Setup(SaveSubsystem, Registry)`：先执行 `Teardown()` 保证幂等；任一参数为空时直接返回、不注册段；否则保存引用并注册段回调。
  - **Save**：经 `TWeakObjectPtr` 取注册表，取不到则 `Ar.SetError()`；取到则拷贝后按 `PlayerId → Progression.Level → Progression.Experience → Progression.Gold → ArchiveVersion` 顺序写入。
  - **Load**：按同顺序反序列化；`Ar.IsError()` 或注册表已失效返回 `false`，否则 `SetProfile` 并返回 `true`。
- `Teardown()`：若存在绑定则 `UnregisterSegment("Player.Profile")`，随后清空两个引用；可重复调用。
- `BeginDestroy()`：自动调用 `Teardown()`，避免适配器销毁后残留段注册。
- 段回调仅持有注册表的弱引用，不延长 `UGamePlayerDataRegistry` 生命周期。

> 生成的静态数据子系统（`Public/Generated/Data/StaticData/UStaticDataSubsystem.h`）中的 `Get` / `IsAllDataLoaded` 等接口属于生成域，文档见 [GameData 静态数据子系统](../GameData/Docs/API/StaticDataSubsystem.md)。

---

## 测试

```bash
UnrealEditor <Project.uproject> -ExecCmds="Automation RunTests Game.Data; Quit" -unattended -nullrhi
```

`Game.Data` 前缀包含示例域测试与通用存档测试；示例域测试仅在安装本插件后可执行。
