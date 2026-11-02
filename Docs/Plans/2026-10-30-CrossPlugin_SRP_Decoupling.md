# [L4 任务计划]：跨插件单一职责解耦（Stat / Buff / StaticData / Time / Bootstrap）

> **关联准则**：[B2 计划先行](../../guidelines/00-行为规范/01-核心铁律与五阶执行模型.md) · [B3 克制修改](../../guidelines/00-行为规范/01-核心铁律与五阶执行模型.md) · [B4 拒绝过敏](../../guidelines/00-行为规范/01-核心铁律与五阶执行模型.md) · [R-ARCH-CLASS 职责边界](../../guidelines/50-审查规范/01-代码审查规范.md)
> **任务等级**：[ L4 基础设施 / 破坏性变迁 ]
> **状态**：已批准 (APPROVED) —— 用户于 2026-10-30 明确指令「开始执行吧」；范围为全量 P0+P1+P2、彻底删除代理模式、允许破坏性 Public API、为拆出新模块补单测

---

## 1. 背景与核心目标

- **背景**：GameFeedback 已完成消息总线 + 组件化重构，无上帝类。对其余插件做同等标准的体检后，发现职责过载集中在「算法 + 状态 + 复制 + 通知」混装于单个 UActorComponent 的四类场景。
- **目标**：把过载类的「状态与算法」下沉为**无 UObject 依赖的纯 C++ 协作类**，使每个单元可脱机单测；组件回归为「生命周期 + 网络复制 + 授权 + 转发」。
- **非目标（Out of Scope）**：
  - 不改动 GameFeedback（已完成重构）。
  - 不改动 `GameDataSamples` 的 `Public/Generated` 与 `Private/Generated`（Excel 管线产物）。
  - 不拆分 `USaveGameSubsystem`、`UGameVitalComponent`（理由见 §2.3）。
  - 不改变 `.bin` 文件字节格式（必须与现有资产二进制兼容）。
  - 不改变 `UTimeDilationSubsystem` 的子系统边界与单 Tick 时钟宿主。

---

## 2. 诊断结论

### 2.1 职责过载分级

| 插件 | 单元 | 规模（头+实现） | 混装职责 | 评级 |
|---|---|---:|---|---|
| GameGameplayCore | `UGameStatComponent` | 358 + 515 | ① 数值存储 + 网络复制 ② 修正器演算 ③ **代理模式第二套存储策略** ④ 通知出口 | **严重** |
| GameGameplayCore | `UGameBuffComponent` | 184 + 412 | ① Buff 实例/叠层/时长 ② FastArray 复制 ③ **Stat 修正器桥接** ④ 本地预测副本 ⑤ 标签聚合 | 中等 |
| GameData | `FStaticDataRepository` | 183 + 264 | ① 内存表容器 ② **.bin 编解码 + CRC + 摘要** | 中等 |
| GameCore | `UTimeDilationSubsystem` | 183 + 289 | ① Actor 顿帧合并表 ② 全局通道混音（共享 Tick 时钟） | 轻微 |
| GameGameplay | `UGameAbilitySystemBootstrap` | 145 + 217 | 编排状态机 + 两套限次重试计数 | 轻微 |
| GameData | `USaveGameSubsystem` | 196 + 233 | 门面 + 白名单策略（IO/段注册表已下沉） | 轻微 |

### 2.2 关键证据

- **代理模式生产零使用**：`IGameStatValueSource`、`EGameStatSourceMode`、`EGameStatWriteOp`、`FGameStatQueryDelegate`、`FGameStatWriteDelegate` 的全部命中仅存在于 `UGameStatComponent` 自身、`UGameStatComponentTest.cpp` 与文档；无任何生产调用点。
- **封装泄漏**：`GetModifierContainer()` / `GetSubscriptionHub()` 的非 const 重载允许外部绕过组件直接改写内部状态（破坏广播不变式），且零外部调用者。
- **重复演算**：`ModifierContainer.EvaluateModifiers` 在 `ModifyStat`、`SetStatCurrent`、`SetStatMax`、`ExecuteExternalWrite`、`GetStatEffectiveCurrent/Max` 共 11 处重复调用，无统一入口。
- **Buff 修正器桥接**：`ModifierHandlesByInstance` 与 `RegisterInstanceModifiers` / `UnregisterInstanceModifiers` 使 Buff 组件直接持有 Stat 的修正器句柄，且移除存在三条路径（Tick 到期 L29-77、按标签 L146-223、按实例 L224-247），回滚一致性依赖实现者记忆。

### 2.3 明确不拆的理由（B4 拒绝过度设计）

- **`USaveGameSubsystem`**：`FSaveGameFileIO`（文件 IO）与 `FSaveGameSegmentRegistry`（段注册表）已下沉，白名单校验也已委托 `FSaveGameFileIO::IsSlotNameAllowed` 静态函数。本体是门面（Facade），再抽一层策略对象只会增加无收益的跳转层。
- **`UGameVitalComponent`**：147 行，单一职责边界教科书级，不动。
- **`UTimeDilationSubsystem`**：两块时间模型共享同一 `Tick` 时钟与 `LastRealTime` 基准。拆成两个子系统会造成时钟漂移与 `WorldSettings` 写回竞争，**拆完比现在更差**。仅抽纯容器，保留单一时钟宿主。

---

## 3. 方案对比决策矩阵

| 维度 | 方案 A：纯 C++ 协作类下沉 [推荐] | 方案 B：新增组件 / 子系统 | 方案 C：仅删死代码 |
|---|---|---|---|
| **核心机制** | 状态与算法抽为无 UObject 的纯 C++ 类；UCLASS 只保留生命周期、复制、授权与转发 | 按职责新增 UActorComponent / UWorldSubsystem，组件间通过接口通信 | 只删除代理模式与封装泄漏，不拆分 |
| **优势** | 可脱机单测；零 GC/Tick/复制开销；不改变 Actor 组件拓扑；引用方零感知 | 职责物理隔离，与 GameFeedback 风格一致 | 改动最小，风险最低 |
| **劣势** | 协作类与组件仍同仓，需靠命名与目录严格区分职责 | ReplicatedStats 必须留在 UCLASS，复制语义被割裂；组件间查找与生命周期耦合；Actor 组件数膨胀 | Stat 仍约 630 行，双轨删除后算法仍混装，未达目标 |
| **爆炸半径** | 局部于被拆类所在插件；Public 头新增文件 | 涉及 Actor 装配、复制注册、初始化顺序 | 局部 |
| **迁移成本** | 中（每步独立可编译） | 高 | 低 |

**选型结论**：采用方案 A。GameFeedback 之所以用方案 B，是因为它有 4 种不同宿主与生命周期（PC / GameState / 权威端 / 世界服务）；而 `UGameStatComponent` / `UGameBuffComponent` 只有单一宿主 Actor，且其复制状态必须留在 UCLASS 内 —— 强行组件化只会把复制语义割裂到多处。方案 A 同时满足「单文件单类、可脱机单测、UCLASS 只做生命周期」三项标准。

---

## 4. 实施步骤（步步可编译、步步可验证）

### Step 1（P0-A）删除代理模式（Expand→Contract 的 Contract 阶段）✅ 已完成

**删除清单（精确到符号）**

- 删除文件：`Plugins/GameGameplayCore/Source/GameGameplayCore/Public/Stat/IGameStatValueSource.h`
- `Public/Stat/FGameStatTypes.h`：删除 `#include "Stat/IGameStatValueSource.h"`、`EGameStatSourceMode`、`EGameStatWriteOp`、`FGameStatQueryDelegate`、`FGameStatWriteDelegate`
- `Public/Stat/UGameStatComponent.h`：删除 `BindStatSource`、`BindNativeStatQueries`、`BindNativeStatWriter`、`UnbindStatSource`、`GetSourceMode`、`CanWriteStat`、`ExecuteExternalWrite`、成员 `SourceMode` / `ProxySource` / `NativeCurrentQuery` / `NativeMaxQuery` / `NativeStatWriter`
- `Private/Stat/UGameStatComponent.cpp`：删除上述 8 个函数体与 `GetStatCurrent`、`GetStatMax`、`GetStatValue`、`ModifyStat`、`SetStatCurrent`、`SetStatMax` 中的 6 处 `SourceMode == Proxy` 分支
- `Private/Tests/UGameStatComponentTest.cpp`：删除 `Game.GameplayCore.Stat.ProxyMode`、`.ProxyWriteContract`、`.ProxyReviveAndVital` 三个用例；修正 `.FullApiCoverage` 与 `.TaggedSubscription` 中的代理断言
- 文档：删除 `Docs/Guides/ProxyStatGuide.md`；改写 `Docs/API/GameStatComponent.md`、`Docs/Architecture/Overview.md`、`README.md`
- 同时删除非 const `GetModifierContainer()` / `GetSubscriptionHub()`（零调用者死代码，B3 安全放行）

**验证门禁**：`Result: Succeeded`；`Game.GameplayCore.Stat.*` 剩余用例全绿；`git grep -E "IGameStatValueSource|EGameStatSourceMode|EGameStatWriteOp|FGameStatQueryDelegate|FGameStatWriteDelegate|CanWriteStat|GetSourceMode"` 在 `Source/` 下零命中。

### Step 2（P0-B）抽 EffectiveEvaluator 与 AcknowledgedDiff ✅ 已完成

**新增**

- `Public/Stat/FGameStatEffectiveEvaluator.h` + `Private/Stat/FGameStatEffectiveEvaluator.cpp`
  - `static float EvaluateCurrent(const FGameStatValue& Base, const FGameStatModifierContainer&, float Min)`
  - `static float EvaluateMax(const FGameStatValue& Base, const FGameStatModifierContainer&, float Min)`
  - `static float EvaluatePercent(float Current, float Max, float Min)`
  - 职责：收敛 11 处重复的 `EvaluateModifiers` 调用，统一 Min 钳制规则
- `Public/Stat/FGameStatAcknowledgedDiff.h` + `Private/Stat/FGameStatAcknowledgedDiff.cpp`
  - `bool NeedsBroadcast(const FGameplayTag&, float EffectiveCurrent, float EffectiveMax, float& OutOldCurrent, float& OutOldMax)`
  - `void Record(const FGameplayTag&, float Current, float Max)`
  - `void Remove(const FGameplayTag&)`
  - `void PruneMissing(const TMap<FGameplayTag, int32>& StatIndex)`
  - 职责：承载 `AcknowledgedStats` 的全部差分判定，替代 `OnRep_ReplicatedStats` 内联逻辑

**修改**

- 组件以 `FGameStatAcknowledgedDiff` 替代 `AcknowledgedStats`；`ModifyStat` / `SetStatCurrent` / `SetStatMax` / `NotifyIfEffectiveStatChanged` / `OnRep_ReplicatedStats` 统一经 Evaluator + Diff

**验证门禁**：新增 `Game.GameplayCore.Stat.EffectiveEvaluator`、`Game.GameplayCore.Stat.AcknowledgedDiff`；现有 Stat 用例全绿。

### Step 3（P1-A）下沉 Buff 修正器桥接 ✅ 已完成

> 微调：`AcknowledgedStacks` 保留在组件内。经复核它与 `BroadcastBuffChanged` / `BroadcastBuffRemoved` / `PublishReplicatedDiff` 同属「广播差分」职责且仅 4 处使用，移入 `FGameActiveBuffArray` 会让复制容器承担广播决策，职责错位（B3/B4）。

**新增** `Public/Buff/FGameBuffModifierBridge.h` + `Private/Buff/FGameBuffModifierBridge.cpp`

- `void Register(int32 InstanceId, UGameStatComponent& Stat, const FGameBuffInstance& Instance)`
- `void Unregister(int32 InstanceId, UGameStatComponent& Stat)`
- `void RemoveInstance(int32 InstanceId)`
- `void ClearAll(UGameStatComponent& Stat)`
- 内部持有 `TMap<int32, TArray<FGameBuffAppliedModifier>>`

**修改**：组件删除 `ModifierHandlesByInstance`、`RegisterInstanceModifiers`、`UnregisterInstanceModifiers`；Tick 到期、`RemoveBuff`、`RemoveBuffStacks`、`RemoveBuffInstance` 四条路径统一经 Bridge，保证修正器回滚零遗漏。

**验证门禁**：新增 `Game.GameplayCore.Buff.ModifierBridge`；现有 6 个 Buff 用例全绿。

### Step 4（P1-B）下沉静态数据编解码 ✅ 已完成

**新增**

- `FBinaryTable` 迁至 `Public/StaticData/FStaticDataRepositoryTypes.h`
- `Public/StaticData/FStaticDataBinCodec.h` + `Private/StaticData/FStaticDataBinCodec.cpp`
  - `static bool Encode(const TMap<FName, FBinaryTable>&, TArray<uint8>& OutFileBytes)`（含 magic / 版本 / CRC32）
  - `static EStaticDataBinError Decode(const TArray<uint8>& InFileBytes, TMap<FName, FBinaryTable>& OutTables)`
  - `static FStaticDataContentDigest ComputeDigest(const TMap<FName, FBinaryTable>&)`
  - 接收 `ExportToFile`/`ImportFromFile`/`ParseTables`/`ComputeContentDigest` 全部字节级逻辑

**修改**：`FStaticDataRepository` 仅保留 `Tables` / `RowPayloadFuncs` / `RegisterRowSerializer` / `AddTable` / `ExtractTable` / `TableCount` / `Reset` / `GetLastError`；导出导入改为「Codec 编解码 + `FFileHelper` 落盘」。

**硬约束**：文件字节格式零变更，必须与现有 `.bin` 资产兼容。

**验证门禁**：新增 `Game.Data.Repository.Codec`（往返 / CRC 损坏 / magic 错 / 版本过高 / 截断 / 重复键）；现有 `Game.Data.Repository.*` 三个用例全绿。

### Step 5（P2-A）抽时间状态纯容器 ✅ 已完成

**新增**

- `Public/Time/FActorHitStopTable.h` + cpp
  - `Request / Clear / ClearAll / IsActive / RemoveInvalid / Advance(RealDelta) -> 待还原 Actor 列表`
  - 承载「同 Actor 取大时长、取小缩放」合并规则
- `Public/Time/FGlobalDilationMixer.h` + cpp
  - `Request / Clear / ClearWithBlend / ClearAll / IsActive / GetDilation / GetEffective / Advance(RealDelta) -> 生效值`
  - 承载通道优先级覆盖与平滑淡出

**修改**：`UTimeDilationSubsystem` 仅保留 Tick、`LastRealTime`、`WorldSettings` 写回、`Get/GetPtr`、`AdvanceTime`、委托转发；`ActiveActorHitStops` / `ChannelStates` / `UpdateEffectiveGlobalTimeDilation` 迁出。

**验证门禁**：新增 `Game.Core.Time.HitStopTable`、`Game.Core.Time.DilationMixer`（纯脱机，无需 World）；现有 8 个 `Game.Core.Time.*` 用例全绿。

### Step 6（P2-B）抽 Bootstrap 重试门 ✅ 已完成

**新增** `Public/Ability/FBootstrapRetryGate.h` + cpp

- `Configure(float IntervalSeconds, int32 MaxRetries)` / `Advance() -> 是否可继续` / `Reset()` / `GetAttempt()`

**修改**：组件以两个 Gate 替代 `ReplicationRetryCount` / `InitRetryCount` 及其上限比较；`MaxReplicationRetries` / `MaxInitRetries` / `RetryIntervalSeconds` 配置项保留并注入 Gate。

**验证门禁**：新增 `Game.Gameplay.Bootstrap.RetryGate`；现有 `Game.Gameplay.Bootstrap` 用例全绿。

### Step 7 文档与索引同步 ✅ 已完成

- 各插件 `README.md` 架构段与目录树、`Docs/Architecture/Overview.md`、`Docs/API/*.md`
- 清理 `ProxyStatGuide.md` 的全部引用（`Docs` 索引、交叉链接）
- 根 `README.md` 插件矩阵职责描述按需更新
- 本计划文档步骤勾选

---

## 5. 风险研判与回滚预案

| 风险 | 触发条件 | 处置 |
|---|---|---|
| Step 1 破坏性删除遗漏引用 | 下游工程仍调用 `BindStatSource` | 已全仓确认生产零调用；若编译失败按符号名全库修复 |
| Step 2 差分语义漂移 | 客户端首次到达不广播 / 已注销条目未清理 | 以现有 `Game.GameplayCore.Stat.RegistrationAndClamping` 与 `.ModifierStack` 作为不变式护栏；新增 Diff 单测覆盖三条分支 |
| Step 3 修正器回滚遗漏 | 某条移除路径未调 Bridge，导致 Stat 永久偏移 | 四条路径统一入口；`Buff.StackAndExpire` 与 `.RemoveStacks` 用例作为护栏 |
| Step 4 `.bin` 格式漂移 | Encode 与旧实现字节不一致 | 以 `Game.Data.Repository.RoundTrip` 与 `Corruption` 为硬门禁；Codec 不引入任何字段顺序变更 |
| Step 5 时钟分裂 | 拆成两个子系统导致漂移 | 明确禁止：容器无 Tick，仅由子系统单点驱动 |
| Step 6 重试计数语义变化 | 上限判定 off-by-one | Gate 单测覆盖 0 次 / 恰好上限 / 超限三种边界 |

- **回滚路径**：每步为独立原子变更。新增文件可直接删除；删除性变更（Step 1）通过 Git 恢复。Step 4 若字节不兼容，立即回退 Repository 实现并将 Codec 标记未启用。
- **客观验收指标**：
  1. 每步 `Build.sh UEToolkitEditor Mac Development` 输出 `Result: Succeeded`。
  2. 新增 7 个自动化用例：`Stat.EffectiveEvaluator`、`Stat.AcknowledgedDiff`、`Buff.ModifierBridge`、`Data.Repository.Codec`、`Time.HitStopTable`、`Time.DilationMixer`、`Gameplay.Bootstrap.RetryGate`。
  3. 全量回归前缀：`Game.GameplayCore`、`Game.Gameplay`、`Game.Data`、`Game.Core`。
  4. `git grep` 确认代理模式符号在 `Source/` 下零残留。
  5. 拆分后单文件规模（实测）：`FStaticDataRepository.cpp` 264 → 107 行；`TimeDilationSubsystem.cpp` 289 → 180 行；`UGameStatComponent.cpp` 515 → 377 行（头 358 → 273 行）；`UGameBuffComponent.cpp` 412 → 382 行。
     - 说明：Stat / Buff 未达到初稿设定的 220 / 320 / 330 行目标。原因是 B-CMT 要求的逐项 Doxygen 注释占头文件约 60%，且剩余内容为「查询 + 修正器 CRUD + 写入 + 订阅 + 复制索引」这一组不可再分的组件本职；继续压缩只能改为方案 B 的组件分家，与已批准方案 A 冲突。

---

## 6. 执行结果汇总（2026-10-30）

| 步骤 | 新增单元 | 编译 | 残留符号 |
|---|---|---|---|
| Step 1 删除代理模式 | 删除 1 头文件 + 8 个 API + 5 个成员 | Succeeded | 零命中 |
| Step 2 数值演算与差分 | `FGameStatEffectiveEvaluator`、`FGameStatAcknowledgedDiff` | Succeeded | `AcknowledgedStats` 零命中 |
| Step 3 Buff 修正器桥接 | `FGameBuffModifierBridge` | Succeeded | `ModifierHandlesByInstance` 零命中 |
| Step 4 静态数据编解码 | `FStaticDataBinCodec`、`FStaticDataBinaryTable` | Succeeded | `FBinaryTable` 零命中 |
| Step 5 时间纯容器 | `FActorHitStopTable`、`FGlobalDilationMixer` | Succeeded | `ChannelStates` 等零命中 |
| Step 6 引导重试闸门 | `FBootstrapRetryGate` | Succeeded | `ReplicationRetryCount` 零命中 |
| Step 7 文档同步 | 4 个插件 README 目录树 + 2 个 Docs | — | — |

新增自动化用例 7 个：`Stat.EffectiveEvaluator`、`Stat.AcknowledgedDiff`、`Buff.ModifierBridge`、`Data.Repository.Codec`、`Time.HitStopTable`、`Time.DilationMixer`、`Gameplay.Bootstrap.RetryGate`。

**运行取证**：`.scripts/run_automation_tests.py` 实跑 7/7 PASS，日志含 `TEST COMPLETE. EXIT CODE: 0`。

该脚本同时修正了一个既有隐患：`-ExecCmds` 中用 `;` 串联多条 `Automation RunTests` 时，后续命令会因 `Automation` 前缀被拆分丢弃而报 `Unknown Automation command`，只有第一条生效。正确写法是用 `+` 拼接全部测试名（见引擎 `AutomationCommandline.cpp` L135）。

### 受限环境下的外部写入隔离

引擎在无头启动时会向用户目录写入三类内容，在 CI 容器或沙箱中会分别失败，并最终导致
`DerivedDataBackends.cpp:817`「no writable nodes available」致命错误与 SIGSEGV：

| 写入点 | 开关 | 引擎实现位置 |
|---|---|---|
| 共享 DDC（`~/Library/.../DerivedDataCache`） | `-DDC-ForceMemoryCache` | `DerivedDataBackends.cpp:766` |
| ZenServer 安装目录与 `~/.zen` 历史 | `-NoZenAutoLaunch` | `ZenServerInterface.cpp:1753` |
| UnrealTrace 服务端日志 | `-notraceserver` | `TraceAuxiliary.cpp:2365` |

三个开关均为 `FParse::Param` 精确匹配的无值开关，脚本已内置。

### 回归中修复的运行时缺陷

`UGameFeedbackHitStopService` 原在 `Initialize` 中订阅消息总线。世界子系统在 `Initialize`
阶段先于 GameInstance 子系统集合初始化，自动化测试新建 World 时 `UGameplayMessageSubsystem`
尚未就绪，`Get` 的 `check(Subsystem != nullptr)` 断言失败并崩溃。已迁移到 `OnWorldBeginPlay`，
确保订阅晚于 GameInstance 子系统创建。

### 全量回归结果

`run_automation_tests.py --all`：**77 个用例全部 PASS**

| 前缀 | 通过 |
|---|---:|
| Game.Core | 32 |
| Game.Logging | 5 |
| Game.GameplayCore | 19 |
| Game.Gameplay | 7 |
| Game.Data | 9 |
| Game.Feedback | 5 |
