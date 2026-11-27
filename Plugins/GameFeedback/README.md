# GameFeedback 插件

## 1. 模块定位

`GameFeedback`（**v2.0.0**）是通用战斗表现解耦与分层时间膨胀基础设施插件（Level 2 领域调度层，**零 `GameGameplayCore` / GAS 强依赖**）。采用 **消息总线 + 独立能力组件** 架构：调用方仅需广播一个纯表现信号 `FGameFeedbackSignal`，镜头震动、手柄触觉、顿帧、受击闪白、粒子、音效与紧凑网络派发均由独立监听者各自消费，无集中编排上帝类。

```text
Plugins/GameFeedback/
├── Source/GameFeedback/
│   ├── Public/
│   │   ├── Feedback/GameFeedbackSignal.h              # 纯表现信号 / 本地视角 / 状态标签
│   │   ├── Feedback/GameFeedbackActions.h             # 6 组原子动作参数（含可配致死全局定格）
│   │   ├── Feedback/GameFeedbackPerspective.h         # 本地视角判定（共享导出纯函数）
│   │   ├── Bus/GameFeedbackChannels.h                 # Play / Network / Clear 通道标签
│   │   ├── Bus/GameFeedbackMessages.h                 # 总线消息载荷
│   │   ├── Bus/GameFeedbackBlueprintLibrary.h         # 唯一广播入口（支持 WorldContext 与预解析）
│   │   ├── Component/GameFeedbackPlayerComponent.h    # 本地玩家表现监听者
│   │   ├── Component/GameFeedbackWorldComponent.h     # 世界空间表现监听者
│   │   ├── Component/GameFeedbackNetworkDispatcherComponent.h # 权威端派发监听者
│   │   ├── Network/GameFeedbackEventNet.h             # 紧凑网络报文（TBitMask / 典型 41B，完整 44B）
│   │   ├── Network/GameFeedbackReceiverComponent.h    # RPC → 本地总线桥接
│   │   ├── System/GameFeedbackDefinitionProvider.h    # 资产解析 / 统一软引用去重预热 / O(1) ID 缓存
│   │   ├── System/GameFeedbackHitStopService.h        # 顿帧全责服务（含自伤防双重顿帧）
│   │   ├── System/GameFeedbackRuntimeSubsystem.h      # 组件接线（多世界隔离 + 分屏热加入）
│   │   ├── Time/TimeDilationTypes.h                   # 时间膨胀通道与状态类型
│   │   ├── Time/FActorHitStopTable.h                  # Actor 局部顿帧表
│   │   ├── Time/FGlobalDilationMixer.h                # 全局分层时间膨胀混音器（TStaticArray 零堆分配）
│   │   ├── Time/TimeDilationSubsystem.h               # 分层时间膨胀子系统（空闲零 Tick 休眠）
│   │   ├── Asset/GameFeedbackDefinition.h             # 单条打击表现配置资产
│   │   ├── Asset/GameFeedbackRegistry.h               # 全局表现映射注册表（TokenStream 版本指纹）
│   │   ├── Config/GameFeedbackSettings.h              # 项目全局配置
│   │   └── GameFeedback.h
│   └── Private/
│       ├── Asset/
│       ├── Bus/
│       ├── Component/
│       ├── Config/
│       ├── Execution/                                 # 无状态表现执行器
│       │   ├── GameFeedbackAudioExecutor.h / .cpp     # 空间音与确认音执行器（统一去重预热）
│       │   └── GameFeedbackVFXExecutor.h / .cpp       # 粒子决策计划与无状态执行器
│       ├── Feedback/
│       ├── Network/
│       ├── System/
│       │   ├── GameFeedbackDefinitionProvider.cpp
│       │   ├── GameFeedbackHitStopService.cpp
│       │   ├── GameFeedbackInternalHelpers.h          # 内部共享命中点/组件装配/世界解析助手
│       │   └── GameFeedbackRuntimeSubsystem.cpp
│       ├── Time/
│       ├── Tracker/                                   # 有状态生命周期追踪器与限流器
│       │   ├── GameFeedbackFlashTracker.h / .cpp      # 闪白 Overlay 追踪（弱存活令牌防 UAF）
│       │   ├── GameFeedbackHitStopLimiter.h / .cpp    # 顿帧冷却、长窗口保护与节流清理
│       │   ├── GameFeedbackPredictionHistory.h / .cpp # 有界本地预测指纹去重
│       │   └── GameFeedbackShakeTracker.h / .cpp      # 分屏镜头震动实例追踪
│       ├── Tests/                                     # 19 个专项测试与基准文件（42 组自动化用例）
│       └── GameFeedback.cpp
├── Docs/
│   ├── Architecture/
│   │   ├── Overview.md                                # 总线解耦架构与视角判定拓扑
│   │   └── NetworkAndCulling.md                       # 紧凑网络报文、版本校验、空间裁剪与预测去重
│   ├── API/
│   │   ├── SignalAndActions.md                        # 表现纯信号载荷与 6 组原子动作参数
│   │   ├── BusAndLibrary.md                           # 总线通道标签、消息载荷与统一广播函数库
│   │   ├── AssetAndRegistry.md                        # 表现资产、注册表四级匹配与统一预热提供者
│   │   ├── ExecutorsAndTrackers.md                    # 闪白/震动/顿帧/预测追踪器与执行器
│   │   └── TimeDilationSubsystem.md                   # 分层时间膨胀与局部顿帧子系统
│   └── Guides/
│       └── QuickStart.md                              # 快速上手与资产配置接入指南
├── GameFeedback.uplugin
└── README.md
```

---

## 2. 插件依赖（严格 Level 2 无环拓扑）

- **公开依赖 (`PublicDependencyModuleNames`)**：`Core`, `CoreUObject`, `Engine`, `GameplayTags`, `Niagara`, `DeveloperSettings`, `GameCore`
- **私有依赖 (`PrivateDependencyModuleNames`)**：`GameLogging`, `GameMessage`, `NetCore`
- **零玩法耦合**：不依赖 `GameGameplayCore` 或 `GameGameplay`；伤害契约适配器 `UGameFeedbackAdapter` 归位于 `Plugins/GameGameplay/Source/GameGameplay/Public/Bridge/UGameFeedbackAdapter.h`。
- **日志治理**：统一基于 `GameLogging`（`LogGameFeedback`）输出结构化子域标签日志（`[Module]` / `[Provider]` / `[Registry]` / `[TimeDilation]` / `[NetworkDispatcher]` / `[Runtime]`），高频热路径降至 `Verbose` 并在被抑制时触发零时钟零 CAS 短路。

---

## 3. 核心特性

1. **单一广播入口与预解析复用**：`UGameFeedbackBlueprintLibrary::BroadcastFeedback` 自动隐式绑定 `WorldContextObject`，未显式传入 `Definition` 时在总线上预解析一次供 4 个监听者零重复复用；`BroadcastClearFeedback` **不受 `bEnableFeedback` 热关断阻断**，随时可清理残留闪白与顿帧。
2. **分层时间膨胀与空闲零 Tick 休眠**：`UTimeDilationSubsystem` 仅在游戏世界创建，`FGlobalDilationMixer` 采用 `TStaticArray` 零堆分配，`IsTickable()` 在无活跃顿帧与无激活通道时自动休眠实现零空转。
3. **顿帧保护与自伤防双重卡肉**：`UGameFeedbackHitStopService` 采用 CDO 零堆分配与节流 `PruneIfNeeded`；致死全局定格参数（Duration / Dilation / BlendOut）全部配置化；单机与联机均严格排除自伤双重顿帧。
4. **闪白生命周期防 UAF**：`FGameFeedbackFlashTracker` 采用栈内联组件收集零堆分配、弱存活令牌（`TWeakPtr<bool>`）+ 析构兜底杜绝裸指针定时器 UAF、`PruneInvalid(World)` 同步回收失效定时器、原始 Overlay 经 `TStrongObjectPtr` 保活。
5. **统一软引用去重预热（零句柄泄漏）**：`UGameFeedbackDefinitionProvider` 统一接管 Definition、VFX 与 Audio 软资产的按路径去重异步预热（正确处理同步完成竞态），并在 `Deinitialize` 全量取消句柄释放缓存；同时内置带版本校验的 O(1) 指针直查 `DefinitionIdCache`。
6. **确定性 TokenStream 版本指纹**：`UGameFeedbackRegistry` 基于 `FGameplayTagQuery` 真实表达式 `TokenStream` + `TagDictionary` 计算 `DefinitionMappingVersion`（不受可选 `UserDescription` 干扰），复合规则采用 `Algo::StableSort`，编辑器属性变更自动 `PostEditChangeProperty` 同步重建索引。
7. **紧凑 41B/44B 网络报文与零受众免耗配额**：`FGameFeedbackEventNet` 集成 `GameCore::TBitMask<EGameFeedbackNetFlag>`、`PrimaryChannelIndex` 默认 `MAX_uint16` 哨兵、`NetSerialize` 严格错误归并；权威端 `NetworkDispatcherComponent` 先按原点安全视点完成空间裁剪，零可见远端客户端不消耗单帧配额且不白算网络报文。
8. **分屏交错去重与多世界隔离装配**：`GameFeedbackWorldComponent` 采用同帧栈内联去重表支持分屏交错到达（A→B→A→B）；`GameFeedbackRuntimeSubsystem` 隔离跨世界 `PostLogin`、免除 ListenServer 本地 PC 冗余复制 `ReceiverComponent`，并监听 `UGameInstance::OnLocalPlayerAddedEvent` 支持分屏热加入装配。

---

## 4. 自动化测试与性能基准

插件内置 **42 组**自动化测试（32 组功能/边界/生命周期/网络真测试 + 10 组多轮微基准性能回归，零空洞断言、零全局编辑器世界污染），一键运行：

```bash
python3 .scripts/run_automation_tests.py --case Game.Feedback
```

### 4.1 性能基准实测（Apple Silicon macOS arm64 · UE 5.8 Development · Clang 21.1.6 · 2 轮预热 + 7 轮采样）

| 基准项 | 中位数耗时 | 均值 | 极值范围 | 离散度 (CV%) | 吞吐量 |
|---|---:|---:|---|---:|---:|
| `Game.Feedback.Perf.DilationMixer` | `2.00 ns/op` | `1.98 ns` | `[1.94, 2.01] ns` | `1.47%` | `499.97 Mops/s` |
| `Game.Feedback.Perf.RegistryLookup` | `2.75 ns/op` | `2.76 ns` | `[2.73, 2.81] ns` | `0.88%` | `363.64 Mops/s` |
| `Game.Feedback.Perf.PerspectiveResolution` | `4.45 ns/op` | `4.54 ns` | `[4.45, 4.88] ns` | `3.23%` | `224.58 Mops/s` |
| `Game.Feedback.Perf.DefinitionIdentityLookup` | `5.32 ns/op` | `5.27 ns` | `[5.00, 5.50] ns` | `2.98%` | `188.13 Mops/s` |
| `Game.Feedback.Perf.ShakeTrackerArbitration` | `13.49 ns/op` | `13.52 ns` | `[13.46, 13.60] ns` | `0.42%` | `74.13 Mops/s` |
| `Game.Feedback.Perf.PredictionHistory` | `23.32 ns/op` | `23.41 ns` | `[23.30, 23.63] ns` | `0.48%` | `42.87 Mops/s` |
| `Game.Feedback.Perf.ActorHitStopTable` | `23.94 ns/op` | `25.03 ns` | `[23.81, 31.14] ns` | `10.01%` | `41.77 Mops/s` |
| `Game.Feedback.Perf.HitStopLimiter` | `28.74 ns/op` | `28.68 ns` | `[28.26, 29.07] ns` | `1.00%` | `34.79 Mops/s` |
| `Game.Feedback.Perf.BusEndToEndBroadcast` | `86.06 ns/op` | `86.13 ns` | `[84.15, 88.23] ns` | `1.41%` | `11.62 Mops/s` |
| `Game.Feedback.Perf.NetEventSerializeRoundTrip` | `785.42 ns/op` | `789.90 ns` | `[781.98, 818.00] ns` | `1.48%` | `1.27 Mops/s` |

> 真实 `UPackageMapClient` 序列化实测体积：典型载荷 **41 字节**（≤ 64B 预算），完整载荷 **44 字节**（≤ 96B 预算）。

### 4.2 功能测试分组一览（32 组）

- **时间与顿帧 (8)**：`Time.HitStopTable` · `Time.DilationMixer` · `Time.ActorHitStop` · `Time.LayeredPriorityStack` · `Time.BlendOutRecovery` · `Time.PersistentBlendOut` · `Time.DilationMixerCrossBoundary` · `HitStopService`
- **资产与注册表 (4)**：`RegistryLookup` · `CompositeRules` · `DefinitionIdentity` · `DefinitionProviderLifetime`
- **执行器与追踪器 (11)**：`AudioExecutor` · `AudioDedicatedServer` · `VFXTraceOrientation` · `VFXExecutorSpawn` · `VFXDedicatedServer` · `VFXUnregisteredFallback` · `FlashTrackerLifecycle` · `FlashTrackerPruneDeadMeshes` · `HitStopLimiter` · `PredictionHistory` · `ShakeTracker`
- **总线、视角、组件与网络 (9)**：`BlueprintLibraryAndSettings` · `Perspective` · `PlayerComponent` · `WorldComponent` · `RuntimeSubsystem` · `NetSerializeByteBudget` · `NetworkDefinitionContract` · `NetworkDispatcherRateAndCull` · `DedicatedServerServices`

---

## 5. 文档导航

- **架构设计 (Architecture)**
  - [架构概览与总线拓扑](Docs/Architecture/Overview.md)
  - [网络同步、裁剪与预测去重](Docs/Architecture/NetworkAndCulling.md)
- **接口参考 (API Reference)**
  - [信号载荷与原子动作 API](Docs/API/SignalAndActions.md)
  - [总线与广播函数库 API](Docs/API/BusAndLibrary.md)
  - [表现资产与注册表 API](Docs/API/AssetAndRegistry.md)
  - [执行器与生命周期追踪器 API](Docs/API/ExecutorsAndTrackers.md)
  - [分层时间膨胀与顿帧 API](Docs/API/TimeDilationSubsystem.md)
- **使用指南 (Guides)**
  - [快速上手指南](Docs/Guides/QuickStart.md)
