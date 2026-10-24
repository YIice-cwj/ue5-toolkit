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

## 测试

```bash
UnrealEditor <Project.uproject> -ExecCmds="Automation RunTests Game.Data; Quit" -unattended -nullrhi
```

`Game.Data` 前缀包含示例域测试与通用存档测试；示例域测试仅在安装本插件后可执行。
