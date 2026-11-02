# [L3 任务计划]：GameFeedback 审查缺陷根因修复

> **关联准则**：[B2 计划先行](../../guidelines/00-行为规范/01-核心铁律与五阶执行模型.md) · [B7 根因溯源](../../guidelines/00-行为规范/04-Bug根因诊断与全局修复.md) · [B-CMT 注释同步](../../guidelines/30-UE5规范/01-命名格式与注释.md)
> **任务等级**：L3 跨模块行为与网络契约修复
> **状态**：已批准，按方案 C 执行 (APPROVED_OPTION_C)

---

## 1. 背景与核心目标

### 1.1 根因与爆炸半径

本次不做局部判空或可靠 RPC 掩盖问题，统一恢复以下不变式：

1. **匹配不变式**：Registry 必须按 GameplayTag 真实层级择优，Cue 与 Adapter 必须得到同一个最大实际伤害通道。
2. **定义身份不变式**：服务端必须显式解析最终 Definition（含 OverrideDefinition），网络只传稳定 DefinitionID 与映射版本；客户端按同一 Registry 还原同一个资产，禁止再用残缺标签重新猜测。
3. **网络保真不变式**：远端执行所需信号必须保留伤害造成者、轨迹方向、命中组件与骨骼，不得依赖空 HitResult 降级。
4. **预测身份不变式**：只有同一个非零事件 ID 的预测与权威回执才可去重；Attacker/Target 仅用于防串实体。Unreliable 丢包、乱序、重复回执均不得误杀后续事件。
5. **时间生命周期不变式**：闪白定时器必须按最大绝对到期时间的剩余时长调度，提前回调必须重新 arm；原 Overlay 在恢复前必须强持有。
6. **权威服务可达性不变式**：`bAllowAuthoritativeHitStop=true` 时，Dedicated Server 的 Cue、DefinitionProvider、HitStopService 必须形成完整可达链路；默认 false 时仍不得修改权威时间。
7. **资产生命周期不变式**：异步加载句柄去重并持有到完成，已解析 Definition 在 World 生命周期内强引用缓存，注销时统一释放。
8. **工程契约不变式**：声明/实现顺序符合 UE 类排布，所有被改函数的多行 Doxygen 与实现同步。

额外确认的同类阻断：`UGameplayCueNotify_GameFeedback` 在 Dedicated Server 提前返回，使 DS 的 GAS 网络派发和权威顿帧均无信号；只放宽 HitStopService 不足以根治，必须纳入同一修复。

### 1.2 目标产出

- 修复审查中的全部 BLOCKER / WARNING，并覆盖上述 DS 同源问题。
- 保持 Unreliable 表现 RPC，不引入队头阻塞。
- 复用 GAS 已有 PredictionKey，不新增全局 CueManager、不另造预测协议。
- 建立真实 PackageMap、DefinitionID/版本、丢包/乱序、定时器提前回调、GC 与 DS 可达性的可复现自动化证据。

### 1.3 非目标

- 不生成二进制 `.uasset`，不重构无关执行器，不修改战斗数值结算规则。
- 不把 RPC 改为 Reliable，不引入运行期 DefinitionID 注册/同步协议。
- 远端不再传输或重解析完整 FeedbackTags；本地信号仍保留完整标签。
- 不做大面积格式化，不擅自执行 `git commit` / `git push`。

---

## 2. 方案对比决策矩阵

| 维度 | 方案 A：紧凑全保真载荷 + GAS PredictionKey | 方案 B：完整 FHitResult + Reliable RPC | 方案 C：服务端 DefinitionID **[已选]** |
|---|---|---|---|
| **核心机制** | 网络传完整标签及命中字段，远端重新解析 | 直接序列化 FHitResult，以可靠有序回执避免旧预测残留 | 服务端先解析最终 Definition，网络传稳定 ID 与映射版本，客户端按同一 Registry 还原 |
| **优势** | 保留 Unreliable；字段按需付费；不改变资产解析所有权 | 实现直观；丢包乱序由传输层处理 | 多标签时带宽稳定；客户端无需标签匹配；Override 与常规解析统一 |
| **劣势** | 标签数量影响带宽；需真实 PackageMap 测预算 | 带宽高；Reliable 有队头阻塞；仍未解决跨端事件身份 | 需要稳定 ID、映射版本和 Override 登记约束；跨端必须同版本部署 |
| **爆炸半径** | 网络/预测/执行器 + GameGameplay 伤害上下文与 Cue | 网络层及所有接收方 | Registry、Provider、Dispatcher、Receiver、消息消费者与网络契约 |

**选型结论**：按用户明确批准采用方案 C。Definition 解析所有权固定在服务端；客户端不再根据网络标签二次匹配。GAS PredictionKey 仍用于独立解决预测事件身份，不与 DefinitionID 混用。

---

## 3. 目标契约与文件结构

### 3.1 稳定 DefinitionID、版本与 Override

- `UGameFeedbackRegistry` 在 `PostLoad` 与 Provider 初始化时重建定义索引，登记 DefaultFallback、ExactChannelMap、ExactFeedbackTagMap、CompositeRules 中全部非空 Definition。
- `DefinitionID` 使用完整 `FSoftObjectPath::ToString()` 的确定性 CRC32；不得使用 FName 运行期索引、对象指针或加载顺序。哈希为 0 时映射为 1，0 保留为无效 ID。
- Registry 同时生成 `DefinitionMappingVersion`：将“映射类型 + 通道/标签/复合查询与优先级 + Definition 路径”规范化、排序后做确定性 CRC32，确保映射变化与资产路径变化都会改变版本。
- 同一 Registry 内若不同路径产生相同 ID，冲突项标记为不可解析；不得按遍历顺序任选一个。缺失、冲突或版本不一致均返回失败并限流 Warning，禁止静默回退到 Default 或其他资产。
- Dispatcher 对 `Message.OverrideDefinition` 优先取 ID，否则使用 Provider 按 Signal 解析的最终 Definition。Override 必须登记在当前 GlobalRegistry 才能网络派发；未登记时仅跳过该远端派发并告警，本地 Override 行为不变。
- Receiver 只通过 `(DefinitionID, DefinitionMappingVersion)` 取回 Definition，并把它随 NetworkMessage 传给 Player/World/HitStop；这些消费者不得再按远端 Signal 重新 Resolve。
- 客户端与服务端必须同版本部署；映射版本用于明确拒绝混跑，不增加旧报文双轨或动态注册协议。

### 3.2 信号与预测身份

- `FGameFeedbackSignal` 增加：
  - `uint16 PredictionEventId = 0`；0 表示无跨端预测身份，禁止记录/消费预测，安全播放权威表现。
  - `FVector TraceDirection = FVector::ZeroVector`；显式保存 VFX 所需轨迹方向，避免伪造 TraceStart/TraceEnd。
- `FGameGameplayEffectContext` 增加 `uint16 FeedbackEventId = 0` 及 getter/setter；`Duplicate` 保留该值，`NetSerialize` 显式传输。
- `UGameDamageExecution::Execute_Implementation` 从 `ExecutionParams.GetPredictionKey().Current` 写入 `FeedbackEventId`。
- Cue 复用 `UGameFeedbackAdapter::MakeSignalFromDamage`，从自定义 Context 读取 ID；原生非 GAS 调用方若需预测去重，必须自行保证双端填入同一个非零 ID。
- PredictionHistory 以 `(PredictionEventId, Attacker, Target)` 匹配；ID 为 0 直接不匹配。继续保留 0.5 秒过期窗口。

### 3.3 网络紧凑载荷

`FGameFeedbackEventNet` 调整为以下逻辑字段：

- 定义：`DefinitionId`、`DefinitionMappingVersion`；移除单一 `FeedbackTagIndex`，不传 FeedbackTags。
- 对象：Instigator、DamageCauser、Target、HitComponent。
- 向量：ImpactLocation、ImpactNormal、ImpactDirection、TraceDirection。
- 标签：保留 PrimaryChannelIndex 作为信号上下文，但不参与远端 Definition 解析。
- 表现：DamageHalf、StateFlags、PredictionEventId。
- 骨骼：服务端由 HitComponent 将 BoneName 转为 `uint16 BoneIndex`，客户端在同一 SkeletalMeshComponent 上还原；无骨骼用哨兵值，不传字符串。

`ToSignal` 还原 DamageCauser、HitResult.Component/BoneName 与 TraceDirection；VFX 的 OppositeToTraceDirection / AlignToReflectedNormal 改读显式 TraceDirection。不可网络寻址的 Component 按引擎反序列化结果自然退化，不伪造组件。Map 是真实网络边界必需依赖，缺失时序列化明确失败，不再写 Dummy Actor。

**带宽验收口径**：真实 PackageMap 下分别记录典型事件与完整对象/骨骼/轨迹/预测事件字节数；初始预算目标为典型 `≤64B`、完整 `≤96B`，以实测为准，若超出必须报告真实数据，严禁删必要字段凑预算。DefinitionID 使带宽不再随 FeedbackTags 数量增长。

### 3.4 资源与定时器

- DefinitionProvider：
  - `UPROPERTY TMap<FSoftObjectPath, TObjectPtr<UGameFeedbackDefinition>> LoadedDefinitions` 强缓存。
  - `TMap<FSoftObjectPath, TSharedPtr<FStreamableHandle>> DefinitionLoads` 持有进行中和已完成句柄，防止重复请求。
  - Resolve 先查强缓存/已加载对象，再查加载句柄；完成回调写入强缓存。Preload 复用同一入口。
  - Deinitialize 先取消句柄，再清空缓存与 Registry。
- FlashTracker：
  - `OriginalOverlayMaterial` 改为 `TStrongObjectPtr<UMaterialInterface>`。
  - 每次续期先取最大 Expiration，再以 `ExpirationRealTime - CurrentRealTime` 重设定时器；提前回调按剩余时间重新 arm，到期才恢复并移除记录。

### 3.5 主要修改文件

- GameFeedback：Registry、Signal、EventNet、Dispatcher、Receiver、Messages、BlueprintLibrary、Player/World/HitStop 消费者、PredictionHistory、Adapter、VFXExecutor、FlashTracker、DefinitionProvider、测试与 README。
- GameGameplay：FGameGameplayEffectContext、UGameDamageExecution、UGameplayCueNotify_GameFeedback、现有 EffectContext 测试与独立 Cue 桥接测试。

---

## 4. 实施步骤（TDD，逐步可编译可验证）

- [ ] **Step 1：先补根因复现测试（Red）**
  - 扩展 `Game.Feedback.RegistryLookup`：构造“字符串更长但层级更浅”和“层级更深但字符串更短”的标签，断言真实深度优先及同深度字典序稳定性。
  - 新增 `Game.Feedback.DefinitionIdentity`：同路径 ID/版本确定；映射变化改变版本；全部 Registry 来源均可按 ID 往返；不同路径哈希冲突被拒绝；未登记 Override 不可解析。
  - 重写 `Game.Feedback.NetSerializeByteBudget`：建立真实 `UNetConnection + UPackageMapClient + 测试 World Actor/Component`，对象映射未建立必须失败；断言 DefinitionID/版本、DamageCauser、HitComponent、BoneName、TraceDirection、PredictionEventId 往返并记录两组真实字节数。
  - 扩展 `Game.Feedback.PredictionHistory`：同实体不同 ID 不误消费；乱序各自正确；ID 7 回执丢失后 ID 8/9 不受旧记录影响；重复回执不二次消费；ID 0 永不消费；过期仍生效。
  - 扩展 `Game.Gameplay.EffectContext`：FeedbackEventId 的 Duplicate/NetSerialize 往返。
  - 新增 `Game.Gameplay.FeedbackCueBridge`：双 ActualChannels 与 MatchedTagName 故意不同，断言最大实际伤害通道胜出、完整本地标签与 PredictionEventId 保留；DS World 下 Play 消息仍广播。
  - **门禁**：新增断言在旧实现上按预期失败；真实 PackageMap 未搭好前不得修改生产代码假称通过。

- [ ] **Step 2：修复 Registry、定义身份与 Cue（Green）**
  - Registry 用 `UGameplayTagsManager::FindTagNode` 的父节点链计算深度，同深度再按 TagName 字典序；实现 §3.1 的索引、版本、冲突检测与 ID 查询。
  - Cue 的自定义 Context 分支构造现有 `FGameDamageContext`，复用 Adapter；填入 FeedbackEventId 与 TraceDirection，移除 DS 提前返回。
  - DamageExecution 写入 PredictionKey.Current；Signal/EffectContext 按 §3.2 扩展。
  - PredictionHistory/PlayerComponent/Receiver 全部切换为非零事件 ID 匹配，不保留 Attacker/Target-only 回退。
  - **门禁**：对应测试转绿；既有 AdapterConversion 不回归。

- [ ] **Step 3：实施服务端 DefinitionID 网络闭环与 VFX 修复（Green）**
  - Provider 增加 ID/版本查询与按 ID 解析；Dispatcher 优先处理 Override，再使用服务端 Resolve 结果。
  - EventNet 按 §3.3 修改 FromSignal/NetSerialize/ToSignal；Receiver 只按 ID/版本解析，并通过 NetworkMessage 将 ResolvedDefinition 传给 Player/World/HitStop，禁止二次标签解析。
  - Adapter/Cue 写入 TraceDirection；VFX 两种轨迹朝向模式改读 TraceDirection。Client RPC 保持 Unreliable。
  - 新增 Dispatcher/Receiver 契约测试：常规与 Override 均还原同一 Definition；未登记 Override、未知 ID、冲突 ID、版本不一致均不广播错误资产。
  - **门禁**：真实 PackageMap 全字段与预算、DefinitionIdentity、网络契约测试全部通过。

- [ ] **Step 4：修复闪白、资产与 DS 生命周期（Green）**
  - FlashTracker 按 §3.4 修改；新增 `Game.Feedback.FlashTrackerLifecycle`：连续命中、第二次 Duration 小于剩余时间、直接 Tick 提前回调后再 Tick 到最终到期，断言最终恢复且提前回调后定时器仍有效；全程无 sleep。
  - 同一测试在仅由 Tracker 持有原材质时触发 GC，断言材质未销毁且最终恢复；覆盖 ClearActor/Shutdown。
  - DefinitionProvider 按 §3.4 增加强缓存和句柄表；新增 `Game.Feedback.DefinitionProviderLifetime`，验证 Resolve 缓存经 GC 仍有效、Deinitialize 释放、同一路径仅一个异步句柄且完成写缓存。
  - HitStopService 与 DefinitionProvider 的 ShouldCreateSubsystem 允许 GameWorld（含 DS）；ApplyHitStop 保留 `bAllowAuthoritativeHitStop` 门禁。
  - 新增 `Game.Feedback.DedicatedServerServices`：DS GameWorld 可创建两类服务；配置 false/true 分别验证广播后 `IsActorInHitStop=false/true`，以真实 TimeDilationSubsystem 状态验证端到端。
  - **门禁**：新增生命周期/DS 测试通过，无 sleep、无全 Mock 同义反复。

- [ ] **Step 5：工程规范与公开说明同步**
  - 给 Receiver 构造函数补多行 Doxygen；将 DefinitionProvider/HitStopService 的静态 GetPtr 放入对应静态函数区，并同步 cpp 定义顺序。
  - 对所有改动函数逐一核对 `@brief/@param/@return`，函数体内不新增业务行内注释。
  - 更新 GameFeedback README 的 DefinitionID/版本/Override 登记规则、非零 PredictionEventId 规则、必要网络字段、真实带宽口径与测试矩阵；同步 EffectContext 序列化说明。
  - **门禁**：无 `FeedbackTags.First()`、无 GameplayTag 字符串长度层级比较、无 Overlay 弱引用残留；声明/定义顺序一致。

- [ ] **Step 6：深度验证与交付**
  - 编译：`/Users/Shared/Epic\ Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh UEToolkitEditor Mac Development -Project=/Users/yiice/code/UE5/UEToolkit/UEToolkit.uproject -WaitMutex`，要求 `Result: Succeeded`。
  - 定向测试：`python3 .scripts/run_automation_tests.py --case Game.Feedback --case Game.Gameplay.EffectContext --case Game.Gameplay.FeedbackCueBridge`，要求每个目标有执行记录、退出码 0、无 Fail/Error。
  - 回归：`python3 .scripts/run_automation_tests.py --all`，要求所有插件前缀均有记录并通过。
  - 审查 `git diff --check` 与最终 diff，仅包含本计划文件列出的生产/测试/文档变更；不处理工作区其他既有修改，不提交 Git。

---

## 5. 风险研判与回滚

| 风险 | 控制措施 |
|---|---|
| CRC32 路径碰撞 | Registry 建索引时检测不同路径同 ID，冲突项明确不可解析并告警 |
| 双端 Registry 映射或版本不同 | 网络携带 DefinitionMappingVersion；不一致直接拒绝，同版本部署，禁止静默 fallback |
| Override 未登记 | 本地 Override 不变；网络派发明确拒绝并告警，测试覆盖 |
| PredictionKey 只在所属客户端有预测意义 | 只在本地 NM_Client 记录非零 ID；Attacker/Target 继续防串实体 |
| 原生非 GAS 调用未提供 ID | ID 0 明确“不去重”，宁可播放权威表现也绝不误杀 |
| BoneIndex 与组件不匹配 | 仅在同一网络还原的 SkeletalMeshComponent 上按索引还原；无效则 BoneName=None，不跨组件猜测 |
| 强缓存增长 | 缓存仅限当前 World 已解析 Definition，Deinitialize 全量释放 |
| DS 默认行为变化 | 服务可创建不等于顿帧生效；默认 `bAllowAuthoritativeHitStop=false` 保持不变 |

**回滚路径**：不引入旧报文兼容层；若验证失败，按 Step 2/3/4 的独立 diff 逆向回退对应文件。网络契约变更必须客户端/服务端同版本部署，禁止只回退一端后继续混跑。

**最终客观验收**：DefinitionID/版本/Override、真实 PackageMap 全字段与预算记录、预测丢包/乱序、闪白提前回调+GC、Provider GC/句柄、DS 双配置、Editor 编译和全量自动化均提供日志；任何无法执行的测试如实标记阻断，不以主观审查代替运行证据。
