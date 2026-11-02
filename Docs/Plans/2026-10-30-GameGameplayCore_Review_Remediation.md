# [L3 任务计划]：GameGameplayCore 审查问题根因修复

> **关联准则**：[B2 计划先行](../../../../ai-guidelines/guidelines/00-行为规范/01-核心铁律与五阶执行模型.md) · [B3 最小变更](../../../../ai-guidelines/guidelines/00-行为规范/01-核心铁律与五阶执行模型.md) · [B5 深度验证](../../../../ai-guidelines/guidelines/00-行为规范/01-核心铁律与五阶执行模型.md) · [B7 根因修复](../../../../ai-guidelines/guidelines/00-行为规范/04-Bug根因诊断与全局修复.md)
> **任务等级**：L3 复杂缺陷修复（Buff / Stat / FastArray / 生命周期）
> **状态**：已批准 (APPROVED) · 2026-10-30 · 方案 A

---

## 1. 背景与核心目标

- **现状**：上一轮审查确认 4 个阻断项与 4 个警告项。核心根因是 Buff Modifier 回滚依赖“当前”Stat 绑定而非原绑定，FastArray 差分只识别叠层数，以及 Owner 订阅把单订阅者初始回放扩大为全量广播。
- **当前基线**：工作区已存在大量未提交修改，包含本模块生产代码、测试和文档。实施时必须保留这些修改，只允许在其上追加本任务所需 hunk；禁止 `git reset`、`git checkout --`、`git restore`、`git clean` 或整文件覆盖。
- **目标**：修复全部 8 项问题，使 Modifier 在晚绑定、重绑和 Buff 销毁时正确迁移或回滚；服务器与客户端 Buff 变更通知一致；初始订阅回放只影响新订阅者；时长与 Tick 边界符合公开契约；GC、文档头和核心 UCLASS 排布满足 AGENTS。
- **非目标**：
  - 不新增插件、组件、子系统、通用同步框架或新的运行时抽象层。
  - 不改变现有 UFUNCTION / UPROPERTY 公共签名，不改变 Buff 叠层、Modifier 演算和 Vital 业务规则。
  - 不修改其他插件，不做全库声明重排或格式化，不重新生成 Doxygen HTML。
  - 不执行 `git commit` 或 `git push`。

---

## 2. 方案对比决策矩阵

| 维度 | 方案 A：生命周期归一 + 定向修复 [推荐] | 方案 B：逐点最小修补 | 方案 C：重构通用状态同步框架 |
|---|---|---|---|
| **核心机制** | Bridge 提供统一全量回滚；Bind/EndPlay 共用同一不变式；复制差分接收明确 ChangedTags；订阅仅回放新回调 | 每个移除/重绑调用点分别补循环；PostReplicatedChange 单独补广播；其余问题逐行修补 | 为 Buff 引入完整快照哈希、Modifier 所有权记录和通用订阅回放框架 |
| **优势** | 根治回滚遗漏；通知恰好一次；改动集中；无需改变公共 Actor 组件拓扑 | 初始改动行数略少 | 可覆盖未来更多状态类型 |
| **劣势** | 需要补一个 Bridge 全量回滚入口并调整私有差分接口 | 多条生命周期路径仍重复，容易再次漏调；复制通知易双发 | 爆炸半径大，引入当前无需求的复杂度和浮点/标签哈希风险 |
| **爆炸半径** | 仅限 GameGameplayCore 的 Buff、Stat 与对应测试 | 同左，但分散在更多分支 | 整个 Buff/Stat 状态与复制链路 |
| **迁移成本** | 中，步步可编译和回归 | 低，但维护债务保留 | 高 |

- **选型结论**：采用方案 A。它只增加当前已有两次以上真实需求的统一回滚能力，不复制生命周期逻辑，也不为未来假想需求建立框架，最符合 B3/B4/B7。

---

## 3. 目标设计与实施步骤

### Step 1：先补真实对象回归测试（Red）

- 在现有 `UGameBuffComponentTest.cpp` 增加：
  1. 无 Stat 时 ApplyBuff，后绑定 Stat 后 Modifier 生效。
  2. Stat A→B 重绑后，A 回滚、B 生效；移除 Buff 后两边都恢复基础值，且不误删 B 的其他 Modifier。
  3. Buff 组件销毁而 Stat 保持存活时，全部 Modifier 回滚。
  4. PostReplicatedChange 只改变 GrantedTags / Modifier、叠层不变时，每个受影响 Tag 恰好广播一次。
  5. AddDuration 从限时配置转为永久配置后，剩余时间返回 0。
  6. 客户端复制删除最后一条 Buff 后，空 Tick 自动关闭。
- 在现有 `UGameStatComponentTest.cpp` 增加：第二个 Owner 订阅初始回放时，第一个订阅者的调用次数保持不变，新订阅者仅收到一次当前值。
- **验证门禁**：编译新增测试并确认对应旧实现出现预期失败；不得用 sleep 或全 Mock 制造结果。

### Step 2：统一 Buff Modifier 迁移与销毁回滚

- `FGameBuffModifierBridge` 增加 `ClearAll(UGameStatComponent& Stat)`：逐个移除已记录 Modifier 并清空映射；现有 `Reset` 作为内部收尾能力保留并被真实调用。
- `UGameBuffComponent::BindStatComponent`：
  1. 相同绑定直接返回。
  2. 使用旧 Stat 执行 `ClearAll`。
  3. 更新弱绑定。
  4. 对新 Stat 重新注册全部活跃 Buff；空绑定只执行解绑和回滚。
- 增加 `EndPlay` 覆盖：在 Stat 仍存活时执行同一 `ClearAll`，服务器与客户端都只回滚各自本地 Modifier。
- 同步更新 Bind、EndPlay、Bridge 的 Doxygen 契约。
- **验证门禁**：Step 1 的晚绑定、重绑、误删保护和销毁回滚测试通过；现有 Buff/ModifierBridge 测试不回归。

### Step 3：修复 FastArray 通知、时长与空 Tick 边界

- 将 `PublishReplicatedDiff` 扩展为同时接收“排除索引”和“强制变更 Tags”；PostReplicatedChange 从 ChangedIndices 收集受影响 Tags。
- 差分对待确认 Tags、当前 Tags 与强制变更 Tags 的并集，每个 Tag 最多广播一次；不引入 Spec 浮点哈希或新复制字段。
- AddDuration 遇到 `Duration <= 0` 时将 RemainingSeconds 归零，正时长保持现有累加语义。
- Tick 空数组分支在返回前关闭组件 Tick；不改变服务器权威移除和客户端本地倒计时语义。
- 同步更新对应函数文档头。
- **验证门禁**：同叠层 Spec 变更、永久化和空 Tick 测试通过；现有复制新增/变更/移除与 StackAndExpire 用例通过。

### Step 4：将 Owner 订阅初始回放收窄到新订阅者

- 在 SubscriptionHub 的 Public SDK 边界拒绝无效 Tag 或空 TFunction，返回无效 Handle。
- Owner 订阅在移动回调前保留本次回调副本；仅当 Handle 有效且 Stat 已注册时，对该回调执行一次初始回放，不再调用全量 `SubscriptionHub.Broadcast`。
- 保持原生委托版本、有效值差分广播和 Owner 自动清理逻辑不变。
- **验证门禁**：双订阅者测试通过；现有 TaggedSubscription、OwnerSubscription 与 FullApiCoverage 用例通过。

### Step 5：清理 GC 与声明规范阻断项

- `FGameActiveBuffArray::OwnerComponent` 改为 `TWeakObjectPtr<UGameBuffComponent>`；复制回调通过 `Get()` 获取宿主，不改变 FastArray 注册时序。
- 为上一轮指出的 Buff/Stat/Vital 私有函数补齐多行分标签 Doxygen 文档头；所有本任务修改函数同步校准 `@brief/@param/@return`。
- 仅重排 `UGameBuffComponent`、`UGameStatComponent`、`UGameVitalComponent` 的声明区域，使 public 委托成员位于第 15 层；不重排无关 USTRUCT，不做大面积格式化。
- **验证门禁**：UHT/编译通过；无新增业务行内注释；变更 diff 仅含目标声明与文档。

### Step 6：深度验证与受影响文档同步

- 运行 GameGameplayCore 全前缀测试，再运行全部通用插件前缀回归。
- 编译 UEToolkitEditor Mac Development，要求 `Result: Succeeded` 且无新增 error/warning。
- 检查 Buff 正常叠加、重绑、解绑、销毁、复制新增/变更/移除、限时/永久、最后实例移除，以及 Stat 首订阅/后续订阅/退订/Owner 失效路径。
- 仅同步受影响的公开 API 文档与架构说明；不生成 HTML，不修改无关文档。
- 最终交付 `git diff --stat`、目标文件 diff、编译结果和逐测试用例统计；不提交代码。

---

## 4. 风险研判与应急回滚预案

| 风险 | 控制措施 | 回滚路径 |
|---|---|---|
| 重绑迁移改变既有生效顺序 | 先完整回滚旧 Stat，再按 ActiveBuffs 当前顺序注册新 Stat；用双 Stat 真实对象测试验证 | 仅反向移除本轮 Bind/Bridge hunk，保留其他用户修改 |
| EndPlay 与其他移除路径重复回滚 | ClearAll 在回滚后清空句柄，后续 Unregister 对缺失实例保持无操作 | 回退 EndPlay 覆盖即可恢复局部行为 |
| 复制变更重复广播 | 以待确认、当前和强制变更 Tags 的并集单点广播；测试精确计数为 1 | 回退私有差分签名及 PostReplicatedChange hunk |
| TFunction 副本延长捕获生命周期 | 副本仅在订阅调用栈内存在，不存入成员；Hub 不新增持久捕获 | 回退为原订阅路径并保留独立回归测试证明问题 |
| 声明重排影响 UHT | 只移动声明区域，不改反射宏、类型或默认值；以 UHT/编译为门禁 | 按文件反向移动本任务声明 hunk |
| 覆盖用户已有工作 | 所有编辑基于当前文件精确替换；禁止恢复命令和整文件覆盖 | 不使用任何会回退整个文件的 Git 命令 |

### 客观验收命令

```bash
python3 .scripts/run_automation_tests.py --prefix Game.GameplayCore.Buff --prefix Game.GameplayCore.Stat
python3 .scripts/run_automation_tests.py --all
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" UEToolkitEditor Mac Development -project="/Users/yiice/code/UE5/UEToolkit/UEToolkit.uproject" -NoUBA -NoTrace -WaitMutex
```

- **通过标准**：新增回归用例全部实际执行并通过；`Game.GameplayCore` 无 Fail/Error；全插件回归无新增失败；编译成功且无新增警告；8 项审查发现逐项关闭；工作区中的既有修改全部保留。

---

## 5. 执行验证记录（2026-10-30）

- Step 1：7 个新增真实对象用例均在旧实现上取得预期 RED；销毁用例修正为合法 `BeginPlay → EndPlay` 生命周期后通过。
- Step 2～4：晚绑定、A→B 重绑、其他 Modifier 句柄保护、销毁回滚、同叠层 Spec 变更、永久化、空 Tick 与 Owner 定向回放共 7 项新增用例全部 GREEN。
- Step 5：FastArray 宿主引用改为 `TWeakObjectPtr`；Buff / Stat / Vital 私有函数文档头及 public 委托第 15 层排布完成。
- 核心回归：`Game.GameplayCore` 共发现 26 个用例，26 个 `Result={Success}`，无失败，日志结束标记为 `TEST COMPLETE. EXIT CODE: 0`。
- 核心编译：`-Module=GameGameplayCore -DisablePlugin=GameGameplay+GameFeedback` 编译及 UHT 成功；同参数完整 `UEToolkitEditor Mac Development` 构建返回 `Result: Succeeded`，未出现新增警告。
- 全目标编译：按原始参数执行完整 `UEToolkitEditor Mac Development` 构建，未排除 GameGameplay / GameFeedback，11 个动作完成并返回 `Result: Succeeded`；先前出现的无关插件编译阻断未复现。
- 全插件统一回归：实际完成 54 个用例（Game.Core 39、Game.Data 12、Game.Feedback 3），49 个成功、5 个失败；随后在 `Game.Feedback.NetSerializeByteBudget` 的 `UGameplayTagsManager::GetNetIndexFromTag` 网络索引断言处崩溃，未形成全局退出标记。
- 补充回归：`Game.Gameplay` 已执行的 7 个用例全部成功，随后 `Game.Gameplay.FeedbackCueBridge` 在 `UGameplayMessageSubsystem::RegisterListenerInternal` 处崩溃；`Game.Logging + Game.GameplayCore` 共 31 个用例全部成功并正常退出（Game.Logging 5、Game.GameplayCore 26，`TEST COMPLETE. EXIT CODE: 0`）。三次运行累计取得 92 个用例结果：87 成功、5 失败、2 个崩溃中断。
- 非核心回归边界：失败项为 `Game.Core.Config.NotificationCommitOrder`、`Game.Core.Observer.NotificationCommitOrder`、`Game.Core.Observer.ReentrantReplayOrder`、`Game.Core.Time.DilationMixerCrossBoundary`、`Game.Data.DataTableLoaderUtils`；上述失败与两个崩溃均位于本任务未修改的插件/调用路径，未为其扩大修改范围。
- 文档：已同步 Buff 绑定迁移、复制通知、永久化、空 Tick，以及 Stat 定向初始回放的公开 API 与架构契约；未生成 HTML，未执行 Git 提交。

### 5.1 七项回归闭环（2026-11-02）

- Config / Observer 并发通知：统一接入 `FNotificationDispatcher` 全局 FIFO，回调在源状态锁外按提交顺序执行；`ReentrantReplayOrder` 的回放与重入写入同样入队，不再递归派发。
- 时间膨胀跨阶段：`Advance` 保留越过持续边界的剩余 Delta，继续推进淡出插值。
- 数据表加载：`OutMap.Empty()` 前移到函数入口，所有失败早退均满足“失败清空输出”契约。
- `Game.Feedback.NetSerializeByteBudget` 崩溃：曾尝试全局 `FastReplication=True`，但网络冒烟实测服务端/客户端索引哈希不一致（`f1839b82` / `2cc81c7b`），证明工程存在非对称标签注册，全局开关不可行；改为仅在 `ShouldUseFastReplication()` 为真时写入 `PrimaryChannelIndex`，否则写 `MAX_uint16` 哨兵，客户端依据 `DefinitionId` 还原表现。
- `Game.Gameplay.FeedbackCueBridge` 崩溃：夹具改用 `UGameInstance::InitializeStandalone`，确保子系统集合完成初始化。
- 追加修复：`UGameFeedbackHitStopService::OnWorldBeginPlay` 改用 `UGameplayMessageSubsystem::GetPtr`，无 GameInstance 的 Game 世界不再断言崩溃。
- 验证：`UEToolkitEditor Mac Development` 编译 `Result: Succeeded`；七项定向用例全部通过；引擎用例 `System.AbilitySystem.GameplayEffects.Test_AttributeAggregators` 通过；插件前缀回归共 102 个用例全部 `Result={Success}`（Game.Core 39、Game.Logging 5、Game.GameplayCore 26、Game.Gameplay 9、Game.Data 12、Game.Feedback 11）；网络冒烟 `ClientResult=PASS`。
- 说明：全量 `Automation RunTests Game` 多次在编辑器启动后因 DDC 写入受限未进入自动化而超时，属于运行环境限制；以上结论由拆分后的前缀回归与定向用例取得。
