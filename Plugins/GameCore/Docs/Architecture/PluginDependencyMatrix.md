# 全插件单向无环依赖矩阵与架构防线规范 (Plugin Dependency Matrix)

**版本**: 1.0.0  
**适用范围**: `UEToolkit` 全工程所有插件及新增功能模块

---

## 一、 架构分层拓扑 (Architecture Hierarchy)

为彻底根治大型工程中模块互相引用、循环依赖与死锁，本项目严格实施 **4 级单向无环分层拓扑（Level 0 ~ Level 3）**：

```
Level 0: GameCore (基石底座)
   │
   ├── Level 1: 基础设施通用插件 (GameLogging, GameDebug, GameData, GameAssetCore, GameSpatial, GamePool)
   │      │
   │      └── Level 2: 领域与调度级插件 (GameNetCore, GameNetService, GameNetSession, GameFlow, GameMessage, GameFeedback)
   │             │
   │             └── Level 3: Gameplay 与上层业务插件 (GameGameplay, GameGameplayCore, 游戏具体逻辑)
```

---

## 二、 分层定义与依赖规则矩阵

| 层级 | 包含插件 | 允许依赖的下层模块 | 严禁事项 (一票否决) |
|---|---|---|---|
| **Level 0 (基石底座)** | **`GameCore`** | 仅允许依赖 UE 原生模块：`Core`, `CoreUObject`, `Engine`, `GameplayTags` | **严禁依赖任何其他插件！**<br>保持 100% 纯粹原语与微核心状态。 |
| **Level 1 (基础设施)** | **`GameLogging`**<br>**`GameDebug`**<br>**`GameData`**<br>**`GameAssetCore`**<br>**`GameSpatial`**<br>**`GamePool`** | 仅允许依赖 **Level 0 (`GameCore`)** 以及 UE 原生基础模块 | **严禁 Level 1 内部横向互相依赖！**<br>例如：`GameSpatial` 绝不能依赖 `GamePool`；`GamePool` 绝不能依赖 `GameAssetCore`。若需协作，通过底层原语实现或事件解耦。 |
| **Level 2 (领域调度)** | **`GameNetCore`**<br>**`GameNetService`**<br>**`GameNetSession`**<br>**`GameFlow`**<br>**`GameMessage`**<br>**`GameFeedback`** | 允许依赖 **Level 0** 与 **Level 1** 插件 | 严禁依赖 Level 3 具体业务插件。 |
| **Level 3 (业务实现)** | **`GameGameplay`**<br>**`GameGameplayCore`**<br>各玩法功能模块 | 允许依赖 **Level 0, Level 1, Level 2** 全部插件 | 严禁被 Level 0 / 1 / 2 反向依赖。 |

---

## 三、 循环依赖与架构违规防范规则

1. **单向流动原则**：所有 `.Build.cs` 中的 `PublicDependencyModuleNames` 与 `PrivateDependencyModuleNames` 必须严格遵循低层指向高层的单向原则，任何反向引入在 CI/CD 中一票否决。
2. **零横向交叉原则 (Level 1)**：
   - 基础设施插件应具备“即插即用”的独立性。如果一个项目只需要 `GameSpatial` 空间索引，它引入该插件后应当仅仅拉起 `GameCore`，而不会被迫带出 `GamePool`、`GameAssetCore` 全家桶。
3. **接口契约前置**：
   - 若不同插件间需要产生交互，必须使用 `GameCore/Result`（强类型错误）、`GameCore/Memory`（句柄/槽位表）或 `GameCore/Listener`（事件总线）进行纯抽象解耦。
