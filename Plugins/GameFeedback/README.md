# GameFeedback 插件

## 1. 模块定位

`GameFeedback` 是战斗表现解耦基础设施插件（零 GAS 强耦合）。采用 **消息总线 + 能力组件** 架构：调用方只广播一个纯表现信号，镜头震动、触觉、顿帧、闪白、粒子、音效与网络派发各自由独立监听者消费，不存在集中编排的上帝类。

```text
Plugins/GameFeedback/
├── Source/GameFeedback/
│   ├── Public/
│   │   ├── Feedback/GameFeedbackSignal.h        # 纯表现信号 / 本地视角 / 状态标签
│   │   ├── Feedback/GameFeedbackActions.h       # 6 组原子动作参数（纯类型聚合头）
│   │   ├── Feedback/GameFeedbackPerspective.h   # 本地视角判定（共享纯函数）
│   │   ├── Bus/GameFeedbackChannels.h           # Play / Network / Clear 通道标签
│   │   ├── Bus/GameFeedbackMessages.h           # 总线消息载荷
│   │   ├── Bus/GameFeedbackBlueprintLibrary.h   # 唯一广播入口（蓝图可调）
│   │   ├── Component/GameFeedbackPlayerComponent.h   # 本地玩家表现监听者
│   │   ├── Component/GameFeedbackWorldComponent.h    # 世界空间表现监听者
│   │   ├── Component/GameFeedbackNetworkDispatcherComponent.h # 权威端派发监听者
│   │   ├── Network/GameFeedbackEventNet.h       # 紧凑网络报文（典型 ≤64B，完整 ≤96B）
│   │   ├── Network/GameFeedbackReceiverComponent.h # RPC → 本地总线桥接
│   │   ├── System/GameFeedbackDefinitionProvider.h # 资产解析 / 预热服务
│   │   ├── System/GameFeedbackHitStopService.h  # 顿帧全责服务
│   │   ├── System/GameFeedbackRuntimeSubsystem.h # 组件接线（无业务逻辑）
│   │   ├── Asset/GameFeedbackDefinition.h       # 单条打击表现配置资产
│   │   ├── Asset/GameFeedbackRegistry.h         # 全局表现映射注册表
│   │   ├── Config/GameFeedbackSettings.h        # 项目全局配置
│   │   └── Adapter/GameFeedbackAdapter.h        # 伤害结算转换为表现信号
│   └── Private/
│       ├── Adapter/GameFeedbackAdapter.cpp
│       ├── Asset/GameFeedbackDefinition.cpp
│       ├── Asset/GameFeedbackRegistry.cpp
│       ├── Bus/GameFeedbackBlueprintLibrary.cpp
│       ├── Bus/GameFeedbackChannels.cpp
│       ├── Component/GameFeedbackNetworkDispatcherComponent.cpp
│       ├── Component/GameFeedbackPlayerComponent.cpp
│       ├── Component/GameFeedbackWorldComponent.cpp
│       ├── Config/GameFeedbackSettings.cpp
│       ├── Feedback/GameFeedbackPerspective.cpp
│       ├── Feedback/GameFeedbackSignal.cpp
│       ├── Network/GameFeedbackEventNet.cpp
│       ├── Network/GameFeedbackReceiverComponent.cpp
│       ├── System/GameFeedbackAudioExecutor.h / .cpp   # 空间音与确认音无状态执行器
│       ├── System/GameFeedbackDefinitionProvider.cpp
│       ├── System/GameFeedbackFlashTracker.h / .cpp    # 闪白 Overlay 生命周期追踪
│       ├── System/GameFeedbackHitStopLimiter.h / .cpp  # 顿帧冷却与时长预算限流
│       ├── System/GameFeedbackHitStopService.cpp
│       ├── System/GameFeedbackPredictionHistory.h / .cpp # 本地预测指纹去重
│       ├── System/GameFeedbackRuntimeSubsystem.cpp
│       ├── System/GameFeedbackShakeTracker.h / .cpp    # 分屏镜头震动实例追踪
│       ├── System/GameFeedbackVFXExecutor.h / .cpp      # 粒子特效无状态执行器
│       ├── Tests/                                      # 模块化分类自动化测试集（15 组专项测试文件）
│       └── GameFeedback.cpp
```

## 2. 订阅拓扑

```text
调用方 / GAS Cue / 适配器
        │  UGameFeedbackBlueprintLibrary::BroadcastFeedback(Signal)
        ▼
UGameplayMessageSubsystem  ── Feedback.Channel.Play
        ├── GameFeedbackWorldComponent(GameState)        闪白 / VFX / 空间音
        ├── GameFeedbackPlayerComponent(PlayerController) 镜头震动 / 触觉 / 确认音 / 预测记录
        ├── GameFeedbackHitStopService(WorldSubsystem)   顿帧冷却 / 累积预算 / 全局定格
        └── GameFeedbackNetworkDispatcherComponent(GameState) 权威端派发

客户端收到 RPC
        ▼
GameFeedbackReceiverComponent(PlayerController, 服务端复制)
        │  BroadcastNetworkFeedback(Signal, bSuppressed)
        ▼
Feedback.Channel.Network ── 上述监听者（抑制时整段跳过）
```

- 任何类都不持有其他能力的实现细节；新增一种表现只需新增一个总线监听者。
- `GameFeedbackRuntimeSubsystem` 只负责接线：在 GameState 与本地 PlayerController 上惰性装配组件。

## 3. 核心特性

1. **单一广播入口**：`BroadcastFeedback` / `BroadcastFeedbackWithDefinition` / `BroadcastClearFeedback`，原生与蓝图一致。
2. **顿帧保护**：攻击者/目标滑动窗口 + `MaxCumulativeDuration` 封顶；DefinitionProvider 与 HitStopService 在 Dedicated Server 正常创建，权威端物理定格仍默认由 `bAllowAuthoritativeHitStop` 禁止。
3. **闪白安全**：按最大绝对到期时间 `ExpirationRealTime` 续期，提前回调按剩余真实时间重新 arm；原 Overlay 由强引用保活，清除、注销与最终到期均可安全还原。
4. **分屏隔离**：镜头震动按 `APlayerCameraManager` 独立维护同帧最大振幅实例。
5. **网络派发与拓扑预热**：Listener/Dedicated Server 自动向视距内远端客户端派发；`GameFeedbackRuntimeSubsystem` 在权威服务端为连接就绪的 PlayerController **提前预装配 `UGameFeedbackReceiverComponent`**（同时在 Dispatch 时保留惰性兜底），彻底消除首击 Unreliable RPC 瞬时组件跨端未就绪引发的丢包隐患；单帧派发上限由 `MaxDispatchPerFrame` 控制；无 Pawn 的观战者用视点位置参与裁剪。
6. **稳定网络身份与高效匹配**：服务端解析最终 Definition，Unreliable Client RPC 仅传稳定 `DefinitionID + DefinitionMappingVersion`；ID 由完整软对象路径的确定性 CRC32 生成，0 映射为 1，冲突、未知 ID 与版本不一致均拒绝解析。`UGameFeedbackRegistry::ResolveDefinitionPtr` 遍历匹配时同步缓存最优定义值指针，消除二次哈希表寻址；同深度标签采用 `FName::LexicalLess` 进行纯键字典序比较，避免临时字符串堆内存分配。
7. **预测去重**：按非零 `PredictionEventId + Attacker + Target` 精确匹配，0.5s 窗口内命中即广播 `bSuppressed`；ID 0 不参与记录或去重。
8. **生命周期、异步降级与视觉稳健性**：DefinitionProvider 强缓存已解析资产、去重并持有异步句柄，注销时取消并释放；VFX / 音效 / 确认音软引用未就绪时异步预热并跳过本次播放，不阻塞主线程；`ResolveVFXRotation` 在缺少轨迹向量时（如范围伤害或无端点网络同步）优雅自适应降级为冲击法线朝向，避免粒子朝向锁定世界原点。
9. **GAS 统一收敛**：`Plugins/GameGameplay` 提供 `UGameplayCueNotify_GameFeedback` 桥接，视听资产全工程统一配置。

## 4. 接入步骤

1. **启用插件**：在 `.uproject` 或编辑器 Plugins 面板启用 `GameFeedback`；目标模块 `Build.cs` 增加 `"GameFeedback"` 依赖。
2. **配置注册表**：Project Settings → Game → Game Feedback → `GlobalRegistry` 指定一个 `UGameFeedbackRegistry` 资产。
   - `ExactChannelMap`：主伤害通道 → 表现资产（最高优先）。
   - `ExactFeedbackTagMap`：反馈标签 → 表现资产；多个标签命中时按标签层级深度择优（同深度按字典序，结果稳定）。
   - `CompositeRules`：`FGameplayTagQuery` 复合规则，按 `Priority` 降序。
   - `DefaultFallbackDefinition`：均未命中时的兜底资产。
3. **创建表现资产**：新建 `UGameFeedbackDefinition`，按需配置 HitStop / CameraShake / HitFlash / VFX / Audio / Haptics。
4. **触发表现**：
   - 原生：`UGameFeedbackBlueprintLibrary::BroadcastFeedback(WorldContext, Signal)`。
   - 覆盖资产：`BroadcastFeedbackWithDefinition(WorldContext, Signal, DefinitionAsset)`；Override 优先于 Registry，权威派发前必须已通过 Registry 的任一映射来源登记，未登记 Override 不进行远端派发。
   - 清除状态：`BroadcastClearFeedback(WorldContext, Target)`。
   - 伤害链路：`UGameFeedbackAdapter::MakeSignalFromDamage(Target, Context, Result)`。
   - GAS：在 GameplayCue 上使用 `UGameplayCueNotify_GameFeedback` 桥接类。
5. **网络说明**：Listener/Dedicated Server 在服务端解析最终 Definition，通过 Unreliable Client RPC 发送其稳定 ID、映射版本及紧凑事件，客户端不得重跑 Registry 或重建 `FeedbackTags`。报文保留 `Instigator`、`DamageCauser`、`Target`、`HitComponent`、骨骼索引还原的 `BoneName`、冲击/轨迹方向、`PrimaryChannelIndex` 与 `PredictionEventId`；真实 PackageMap 下典型载荷不超过 64B，完整载荷不超过 96B。`PrimaryChannelIndex` 仅在工程启用 GameplayTag `FastReplication` 时携带真实网络索引；未启用时写入 `MAX_uint16` 哨兵，客户端依据 `DefinitionId` 与映射版本还原表现，不重建主通道标签。客户端预测方以非零事件 ID 记录本地表现，服务器回执精确命中后抑制重复表现。接收组件与表现组件都由 `GameFeedbackRuntimeSubsystem` 自动装配（服务端在玩家进入世界时提前挂载 Receiver 组件，首击直接派发），无需手动挂载。

> 本插件 `CanContainContent: false`，仓库不附带示例 `.uasset`；请按上述步骤在目标工程中创建 Registry 与 Definition 资产。

## 5. 自动化测试

全套自动化测试共 21 组用例，涵盖全状态机、网络序列化、视角判定、执行器、限流器与生命周期，覆盖率达到 95% 以上，且遵循零 sleep 确定性时钟模拟原则：

| 用例 | 覆盖点 |
|---|---|
| `Game.Feedback.NetSerializeByteBudget` | 真实 PackageMap 的 64B/96B 预算、对象/骨骼/方向还原、半精度伤害、Definition 身份与预测 ID |
| `Game.Feedback.RegistryLookup` | 通道精确匹配、标签最具体择优、同深度稳定决策、兜底资产 |
| `Game.Feedback.DefinitionIdentity` | 全来源稳定 ID、映射版本、未知 ID、CRC 冲突拒绝 |
| `Game.Feedback.NetworkDefinitionContract` | Override 优先与登记门禁、真实 RPC 出口、客户端同版本解析及未知/过期事件拒绝 |
| `Game.Feedback.HitStopLimiter` | 冷却窗口、累积预算截断、重置窗口、清除 |
| `Game.Feedback.PredictionHistory` | 非零 ID 与双实体精确消费、乱序/丢失/重复、ID 0 禁用、0.5s 窗口过期 |
| `Game.Feedback.AdapterConversion` | 主导通道、伤害总值、致死标签、冲击法线与轨迹方向 |
| `Game.Feedback.VFXTraceOrientation` | 网络缺失 Trace 端点时按显式轨迹方向计算朝向，及缺失 TraceDirection 时自适应降级为撞击法线 |
| `Game.Feedback.FlashTrackerLifecycle` | 最大绝对到期续期、提前回调重新 arm、原 Overlay 强引用与清除/注销还原（虚拟时间零等待） |
| `Game.Feedback.DefinitionProviderLifetime` | 同步/异步强缓存、同路径句柄去重、真实资产重载与注销释放 |
| `Game.Feedback.DedicatedServerServices` | DS 服务创建、权威顿帧配置门禁与端到端顿帧 |
| `Game.Feedback.Perspective` | 攻击者、受害者、伤害源、旁观者四视角判定，深度 >5 环路保护及空指针安全 |
| `Game.Feedback.ShakeTracker` | 镜头震动空/零振幅防御、同帧振幅择优合并、撞击方向定位与 StopAll 清理 |
| `Game.Feedback.AudioExecutor` | 空间音与确认音最小震级门禁、空音效安全、软引用未加载异步预热及回退机制 |
| `Game.Feedback.VFXExecutorSpawn` | 粒子特效最小震级门禁、未加载软引用异步预热、指定位置及骨骼挂接生成 |
| `Game.Feedback.CompositeRules` | 复合 GameplayTagQuery 规则匹配、Priority 降序择优、与通道/标签优先序校验 |
| `Game.Feedback.PlayerComponent` | 本地控制器自动发现、客户端预测登记、抑制重复消费及网络消息处理 |
| `Game.Feedback.WorldComponent` | HitComponentOnly / AllSkeletalMeshes 模式切换、材质 Overlay 应用与注销还原 |
| `Game.Feedback.NetworkDispatcherRateAndCull` | 单帧派发上限截断 (MaxDispatchPerFrame) 与超视距客户端空间裁剪（Pawn/视点空间判定） |
| `Game.Feedback.RuntimeSubsystem` | 专用服与监听服世界组件惰性装配与自动生命周期托管 |
| `Game.Feedback.AdapterAndLibrary` | 伤害上下文生成打击信号、蓝图函数库入口广播与全局门禁验证 |
