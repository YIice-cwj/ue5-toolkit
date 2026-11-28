# GameFeedback

> **一句话**：以消息总线 + 独立能力组件解耦打击表现，统一处理镜头震动、顿帧、受击闪白、粒子、音效、手柄触觉、分层时间膨胀与紧凑网络派发。
> **版本** v2.0.0 · **引擎** UE 5.1~5.8 · **层级** Level 2 · **直接依赖** GameCore、GameMessage、GameLogging、Niagara

## 30 秒速览

- **解决什么问题**：让调用方只广播一个纯表现信号 `FGameFeedbackSignal`，各表现能力由独立监听者自行消费，避免集中编排的上帝类。
- **核心能力**
  - 单一广播入口：`UGameFeedbackBlueprintLibrary::BroadcastFeedback` 按注册表解析表现资产。
  - 分层时间膨胀：`UTimeDilationSubsystem` + `FGlobalDilationMixer` 驱动全局与 Actor 局部顿帧。
  - 生命周期追踪：闪白、震动、顿帧与预测去重各自有界管理，含 UAF 防护。
  - 紧凑网络派发：`FGameFeedbackEventNet` 以位掩码序列化，权威端先做空间裁剪。
- **什么时候用**：需要把打击感从战斗逻辑中拆出，或需要本地 / 联机一致的顿帧、震屏、闪白与预测去重。
- **什么时候不用**：只需要伤害数值与生命状态时使用 [GameGameplayCore](../GameGameplayCore/README.md)；只需要 GAS 接入时使用 [GameGameplay](../GameGameplay/README.md)。

## 快速开始

模块私有依赖（`MyGame.Build.cs`）：

```csharp
PrivateDependencyModuleNames.AddRange(new string[] {
    "GameFeedback", "GameplayTags", "GameMessage"
});
```

在命中回调中组装信号并广播（节选自 [QuickStart](Docs/Guides/QuickStart.md)）：

```cpp
#include "Bus/GameFeedbackBlueprintLibrary.h"
#include "Feedback/GameFeedbackSignal.h"

FGameFeedbackSignal Signal;
Signal.Instigator = GetInstigator();
Signal.DamageCauser = this;
Signal.Target = TargetActor;
Signal.HitResult = HitResult;
Signal.Magnitude = 50.0f;
Signal.PrimaryChannel = FGameplayTag::RequestGameplayTag(TEXT("Damage.Channel.Blunt"));

UGameFeedbackBlueprintLibrary::BroadcastFeedback(this, Signal);
```

清理残留表现状态：

```cpp
UGameFeedbackBlueprintLibrary::BroadcastClearFeedback(this, DyingCharacter);
```

## 核心能力

### 广播入口与信号契约

- `BroadcastFeedback` 自动隐式绑定 `WorldContextObject`；未显式传入 `Definition` 时在总线上预解析一次，供 4 个监听者复用。
- `BroadcastClearFeedback` 向 `Feedback.Channel.Clear` 广播，不受 `bEnableFeedback` 热关断阻断，随时可清理闪白与顿帧。
- `FGameFeedbackSignal` 只含表现所需字段：施害者 / 受击者、命中结果、方向、通道、状态标签、强度与预测指纹。

### 分层时间膨胀与顿帧

- `UTimeDilationSubsystem` 仅在游戏世界创建；`FGlobalDilationMixer` 使用 `TStaticArray` 零堆分配。
- `IsTickable()` 在无活跃顿帧且无激活通道时自动休眠，空闲不空转。
- `UGameFeedbackHitStopService` 采用 CDO 零堆分配与节流 `PruneIfNeeded`；致死全局定格参数（Duration / Dilation / BlendOut）全部配置化。
- 单机与联机均排除自伤的双重顿帧；`FActorHitStopTable` 承载 Actor 局部顿帧。

### 闪白与生命周期追踪

- `FGameFeedbackFlashTracker` 使用栈内联组件收集零堆分配，`TWeakPtr<bool>` 弱存活令牌 + 析构兜底避免定时器 UAF。
- `PruneInvalid(World)` 同步回收失效定时器，原始 Overlay 经 `TStrongObjectPtr` 保活。
- `FGameFeedbackShakeTracker` 追踪分屏镜头震动实例；`FGameFeedbackHitStopLimiter` 负责冷却、长窗口保护与节流清理。

### 表现资产与注册表

- `UGameFeedbackDefinition` 描述 6 组原子动作：HitStop、CameraShake、HitFlash、VFX、Audio、Haptics。
- `UGameFeedbackRegistry` 基于 `FGameplayTagQuery` 的 `TokenStream` + `TagDictionary` 计算 `DefinitionMappingVersion`，不受可选 `UserDescription` 干扰；复合规则使用 `Algo::StableSort`。
- 编辑器属性变更通过 `PostEditChangeProperty` 同步重建索引；内置带版本校验的 O(1) `DefinitionIdCache`。
- `UGameFeedbackDefinitionProvider` 按路径去重异步预热 Definition、VFX 与 Audio 软资产，处理同步完成竞态，`Deinitialize` 时全量取消句柄并释放缓存。

### 紧凑网络派发与预测去重

- `FGameFeedbackEventNet` 集成 `TBitMask<EGameFeedbackNetFlag>`，`PrimaryChannelIndex` 以 `MAX_uint16` 为默认哨兵，`NetSerialize` 严格归并错误。
- 权威端 `GameFeedbackNetworkDispatcherComponent` 先按原点安全视点完成空间裁剪，零可见远端客户端不消耗单帧配额。
- `FGameFeedbackPredictionHistory` 提供有界本地预测指纹去重；`GameFeedbackReceiverComponent` 负责 RPC 到本地总线的回灌。

### 多世界与分屏装配

- `GameFeedbackWorldComponent` 使用同帧栈内联去重表，支持分屏交错到达（A→B→A→B）。
- `GameFeedbackRuntimeSubsystem` 隔离跨世界 `PostLogin`，免除 ListenServer 本地 PC 的冗余 `ReceiverComponent` 复制。
- 监听 `UGameInstance::OnLocalPlayerAddedEvent`，支持分屏热加入时补装配。

## 架构与依赖

| 项 | 内容 |
| :--- | :--- |
| 公开依赖 | `Core`、`CoreUObject`、`Engine`、`GameplayTags`、`Niagara`、`DeveloperSettings`、`GameCore` |
| 私有依赖 | `GameLogging`、`GameMessage`、`NetCore` |
| C++ 标准 | C++20 |

- **零玩法耦合**：不依赖 `GameGameplayCore` 或 `GameGameplay`；伤害契约适配器 `UGameFeedbackAdapter` 归位于 `Plugins/GameGameplay/Source/GameGameplay/Public/Bridge/UGameFeedbackAdapter.h`。
- 分层定位 Level 2 领域调度层：向下依赖 `GameCore` / `GameMessage` / `GameLogging`，向上为游戏层与 `GameGameplay` 提供表现总线。
- 日志统一走 `GameLogging`（`LogGameFeedback`），按 `[Module]` / `[Provider]` / `[Registry]` / `[TimeDilation]` / `[NetworkDispatcher]` / `[Runtime]` 打子域标签；热路径降至 `Verbose`，被抑制时零时钟零 CAS 短路。

| 关键类 | 职责 |
| :--- | :--- |
| `UGameFeedbackBlueprintLibrary` | 唯一广播与清理入口 |
| `UGameFeedbackRegistry` | 全局表现映射与版本指纹 |
| `UGameFeedbackDefinitionProvider` | 软资产去重预热与 O(1) ID 缓存 |
| `UTimeDilationSubsystem` | 分层时间膨胀与空闲休眠 |
| `UGameFeedbackHitStopService` | 顿帧全责服务 |
| `UGameFeedbackNetworkDispatcherComponent` | 权威端裁剪与派发 |
| `UGameFeedbackPlayerComponent` / `WorldComponent` | 本地玩家 / 世界空间监听者 |

<details>
<summary><b>展开目录结构</b></summary>

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
│       ├── Execution/                                 # 无状态表现执行器（Audio / VFX）
│       ├── Tracker/                                   # 闪白 / 震动 / 顿帧限流 / 预测去重追踪器
│       ├── System/                                    # Provider / HitStopService / RuntimeSubsystem 实现
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

</details>

## 性能与边界

<details>
<summary><b>展开性能基准（历史 Development 构建参考值）</b></summary>

实测基线：Apple Silicon macOS arm64 · UE 5.8 Development · Clang 21.1.6 · 2 轮预热 + 7 轮采样。

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

</details>

- **网络体积**：真实 `UPackageMapClient` 序列化实测典型载荷 41 字节（≤ 64B 预算），完整载荷 44 字节（≤ 96B 预算）。
- **网络裁剪**：权威端先按原点安全视点做空间裁剪；零可见远端客户端时跳过报文构造与单帧配额消耗。
- **生命周期**：`UTimeDilationSubsystem` 仅在游戏世界创建，无活跃顿帧与通道时 `IsTickable()` 休眠；闪白定时器使用弱存活令牌并同步回收。
- **线程与调用入口**：表现信号经 `GameMessage` 总线在游戏线程分发；`BroadcastFeedback` / `BroadcastClearFeedback` 需以有效 `WorldContextObject` 调用。
- **性能口径**：以上为历史 Development 构建参考值，未纳入 CI 自动复测，不构成 Shipping 性能承诺。

<details>
<summary><b>展开功能测试分组（32 组）</b></summary>

运行：`python3 .scripts/run_automation_tests.py --case Game.Feedback`

- **时间与顿帧 (8)**：`Time.HitStopTable` · `Time.DilationMixer` · `Time.ActorHitStop` · `Time.LayeredPriorityStack` · `Time.BlendOutRecovery` · `Time.PersistentBlendOut` · `Time.DilationMixerCrossBoundary` · `HitStopService`
- **资产与注册表 (4)**：`RegistryLookup` · `CompositeRules` · `DefinitionIdentity` · `DefinitionProviderLifetime`
- **执行器与追踪器 (11)**：`AudioExecutor` · `AudioDedicatedServer` · `VFXTraceOrientation` · `VFXExecutorSpawn` · `VFXDedicatedServer` · `VFXUnregisteredFallback` · `FlashTrackerLifecycle` · `FlashTrackerPruneDeadMeshes` · `HitStopLimiter` · `PredictionHistory` · `ShakeTracker`
- **总线、视角、组件与网络 (9)**：`BlueprintLibraryAndSettings` · `Perspective` · `PlayerComponent` · `WorldComponent` · `RuntimeSubsystem` · `NetSerializeByteBudget` · `NetworkDefinitionContract` · `NetworkDispatcherRateAndCull` · `DedicatedServerServices`

</details>

## 文档导航

| 分类 | 文档 |
| :--- | :--- |
| 架构 | [架构概览与总线拓扑](Docs/Architecture/Overview.md) |
| 架构 | [网络同步、裁剪与预测去重](Docs/Architecture/NetworkAndCulling.md) |
| API | [信号载荷与原子动作 API](Docs/API/SignalAndActions.md) |
| API | [总线与广播函数库 API](Docs/API/BusAndLibrary.md) |
| API | [表现资产与注册表 API](Docs/API/AssetAndRegistry.md) |
| API | [执行器与生命周期追踪器 API](Docs/API/ExecutorsAndTrackers.md) |
| API | [分层时间膨胀与顿帧 API](Docs/API/TimeDilationSubsystem.md) |
| 指南 | [快速上手指南](Docs/Guides/QuickStart.md) |
