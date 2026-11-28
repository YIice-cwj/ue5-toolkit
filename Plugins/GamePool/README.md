# GamePool

> **一句话**：世界级 `AActor` 池、非 Actor 纯 `UObject` 池、世代防悬空句柄、协程流式预热与原生 `TStructPool<T>` 结构体内存池。
> **版本** v2.0.0 · **引擎** UE 5.1~5.8 · **层级** Level 1 · **直接依赖** GameCore, GameLogging（私有）

## 30 秒速览

- **解决什么问题**：高频 `SpawnActor` / `DestroyActor` 与 `NewObject` 的分配和 GC 开销；归还对象的组件状态需要重置；工作线程无法直接安全归还；池内对象易被 GC 误收或产生悬空引用。
- **核心能力**：
  - `UObjectPoolSubsystem` 统一编排 Actor 池与纯 UObject 池；
  - `TPooledObjectHandle<T>` 世代句柄 + `TScopedPoolRental<T>` RAII 租约；
  - `FPoolAcquirePayload` 16 字节栈值载荷，带编译期 CityHash64 类型指纹；
  - C++20 协程事件驱动流式预热、TTL 弹性淘汰与跨线程无锁延迟归还；
  - 原生 C++ 定长结构体内存池 `TStructPool<T, BlocksPerChunk>`。
- **什么时候用**：子弹、投射物、命中特效、伤害数字等高频短生命周期 Actor；高频临时 UObject；战斗事件等高频结构体分配。
- **什么时候不用**：需要跨关卡常驻的对象（池是 `UWorldSubsystem`，随世界销毁清空）；含堆分配或长度不定的数据结构（`TStructPool` 只做定长块分配，不替代通用分配器）。

## 快速开始

```cpp
#include "Subsystem/UObjectPoolSubsystem.h"

void AMyWeapon::Fire() {
    UObjectPoolSubsystem& Pool = UObjectPoolSubsystem::Get(this);

    // 借出 RAII 租约：离开作用域自动归还
    TGameStatusResult<TScopedPoolRental<AMyProjectile>> Res =
        Pool.AcquireScopedActor<AMyProjectile>(AMyProjectile::StaticClass(), GetActorTransform());
    if (Res.IsSuccess()) {
        TScopedPoolRental<AMyProjectile> Rental = Res.ExtractValue();
        Rental->Launch();
    }
}
```

```cpp
#include "Memory/TStructPool.h"

struct FHitEventContext {
    int32 SourceId = 0;
    float Damage = 0.0f;
};

TStructPool<FHitEventContext, 128> HitEventPool;

void DispatchHit(int32 SourceId, float Damage) {
    TScopedStructRental<FHitEventContext, 128> Event = HitEventPool.AcquireScoped();
    Event->SourceId = SourceId;
    Event->Damage = Damage;
} // 离开作用域原地析构并回池
```

- Actor 实现 `IGamePoolable` 后可收到 `OnAcquireFromPool` / `OnReleaseToPool` 回调，并用 `IGamePoolable::ExtractPayload<TPayload>()` 解包借出载荷；未实现该接口也可正常借还。

## 核心能力

### Actor 池
- 复用热实例：空闲实例存放于 `TDeque`，借出从栈顶弹出，归还入栈，均为 $O(1)$。
- 借出时自动恢复可见性与碰撞、唤醒 Tick；归还时按 `EPoolResetFlags` 单次遍历重置组件并休眠。
- 外部 `Destroy()` 经委托自愈，避免池持有已销毁实例。
- 首次生成的新实例不覆写其构造期字段。

### 纯 UObject 池
- 严格拦截 `AActor` 派生类与抽象类（返回 `InvalidClass`）。
- `UObjectPoolSubsystem::AddReferencedObjects` 统一向 GC 注册 `Outer`、全部 `UClass` 键、空闲与活跃实例，防止池内对象被误收。
- 与 Actor 池共用借还、句柄、租约、预热与淘汰门面，仅不涉及 Transform 与组件重置。

### 载荷注入与生命周期契约
- `FPoolAcquirePayload` 为 16 字节栈值视图（`const void*` + `uint64` 类型指纹），借出时零堆分配传递只读参数。
- `IGamePoolable::ExtractPayload<TPayload>()` 以编译期 CityHash64 指纹校验类型，不匹配返回 `nullptr`。
- 原生类走直调快路径，蓝图派生类走 `Execute_OnAcquireFromPool` / `Execute_OnReleaseToPool`，保证蓝图覆写生效。
- 带载荷借出时引擎先调 `OnAcquireFromPool`，再调 `OnAcquireFromPoolWithPayload`。

### 世代句柄与 RAII 租约
- 每次借出分配单调递增世代号，内部以不可变 `FObjectKey` 追踪对象，归还或复用后旧句柄自动失效。
- `TPooledObjectHandle<T>` 支持 `IsValid()` / `Get()`，句柄归还时校验归属子系统与世代。
- `TScopedPoolRental<T>` 支持移动语义、`->` / `*` 解引用、`EarlyRelease()` 与 `[[nodiscard]] ReleaseAndDetach()`。

### 预热、淘汰与延迟归还
- `PrewarmAsync` / `PrewarmObjectPoolAsync` 支持 `TSubclassOf`、`FSoftObjectPath`、`TSoftClassPtr`，基于 `FStreamableManager` 回调与 `FGameCoroSuspensionGuard` 事件驱动唤醒。
- 分帧限流由 `UTimeSlicedTaskSubsystem` 执行，触达 `MaxIdleCount` 自动短路，支持 `FCancellationToken` 协作取消。
- `TPoolBucket` 用 `TDeque<FIdleEntry>` 绑定对象与归还时间戳，实现 $O(1)$ LIFO 热借出与 FIFO 冷淘汰；心跳托管于 `UThrottledTickerSubsystem`，全停时自动注销。
- `ReleaseActorDeferred` / `ReleaseObjectDeferred` 经 `TLockFreeRingBuffer<FDeferredReleaseEntry, 1024>` 入队，GameThread 调 `ProcessDeferredReleases(MaxProcessCount)` 批量收割。

### 结构体内存池与低内存泄洪
- `TStructPool<T, BlocksPerChunk>` 基于 `TFixedSizePool<T, BlocksPerChunk>`，原地构造 / 析构，支持 `Acquire(Args...)` / `AcquireScoped(Args...)` 参数转发。
- 提供 `GetAllocatedCount()` / `GetFreeCount()` / `GetChunkCount()` / `GetTotalCapacity()` 观测接口。
- `FPoolResilienceController` 监听 `FCoreDelegates::GetMemoryTrimDelegate()`，低内存时按各池 `MinPreservedCount` 从冷端泄洪。
- 泄漏保护：过滤 GC / 增量清理期间的 `FMemory::Trim()`，非主线程触发时经弱宿主守卫切回 GameThread。

## 架构与依赖

- **公开依赖**：`Core`, `CoreUObject`, `Engine`, `GameCore`
- **私有依赖**：`GameLogging`（经 `LogGamePool` 输出可观测日志）
- **关键类**：
  - `UObjectPoolSubsystem`：`UWorldSubsystem` 门面，`TSharedPtr<FImpl>` Pimpl，CDO 零堆分配、零 `FGCObject` 注册
  - `FActorPoolManager` / `FUObjectPoolManager` / `TPoolBucket`：两类池的存储与借还
  - `FPooledActorResetter`：单次遍历组件重置与 `IGamePoolable` 回调派发
  - `FPoolAsyncLoader` / `FPoolDeferredDispatcher` / `FPoolEvictionScheduler` / `FPoolResilienceController`：预热、延迟归还、淘汰与容灾
  - `TPooledObjectHandle` / `TScopedPoolRental` / `TStructPool` / `TScopedStructRental`：句柄、租约与结构体池

<details>
<summary><b>展开错误码（Game::PoolErrorCodes）</b></summary>

全部错误结果归属 `Game::ModuleId::Pool`，定义于 [PoolErrorCodes.h](Source/GamePool/Public/Core/PoolErrorCodes.h)：

| 错误码 | 标识符 | 含义与典型触发场景 |
|:---:|---|---|
| `5001` | `InvalidClass` | 类型为空、抽象类，或向纯 `UObject` 池传入 `AActor` 派生类 |
| `5002` | `TypeMismatch` | 传入的 `UClass` 不是模板目标类型 `T` 的子类（前置拦截零泄漏） |
| `5003` | `InvalidContext` | `World`、`Outer` 或 `UObjectPoolSubsystem` 尚未初始化或已失效 |
| `5004` | `SpawnFailed` | `SpawnActor` 或 `NewObject` 返回 `nullptr` |
| `5005` | `InvalidatedDuringAcquire` | 实例在 `OnAcquireFromPool` / `OnAcquireFromPoolWithPayload` 回调内自归还或自销毁 |
| `5006` | `InvalidInstance` | 待归还实例为空、已销毁或已标记垃圾回收 |
| `5007` | `NotOwnedActiveInstance` | 实例不属于当前池的活跃借出集合（拦截外部对象与重复归还） |
| `5008` | `InvalidHandle` | 受管安全句柄无效、世代过期或归属其他 `UObjectPoolSubsystem` 实例 |
| `5009` | `CapacityLimitReached` | 空闲池已达 `MaxIdleCount` 上限，拒绝继续预热填充 |
| `5010` | `DeferredQueueFull` | 跨线程无锁延迟归还环形缓冲（1024 槽位）已满或未初始化 |

</details>

## 性能与边界

- 借出、归还、预热与淘汰需在 GameThread 调用；工作线程只能用 `ReleaseActorDeferred` / `ReleaseObjectDeferred` 入队，再由 GameThread 调 `ProcessDeferredReleases` 收割。
- 子系统为 `UWorldSubsystem`，`Deinitialize` 清理全部池实例、延迟队列、调度器与容灾委托；池对象不跨世界存活。
- 预热受 `MaxIdleCount` 约束，越界返回 `CapacityLimitReached`；延迟归还队列满时返回 `DeferredQueueFull`。
- 低内存泄洪过滤 GC 与增量清理期间的 `Trim()`，非主线程触发时经弱宿主守卫切回 GameThread。
- 在 `OnAcquireFromPool` / `OnAcquireFromPoolWithPayload` 内自归还或自销毁会被判为 `InvalidatedDuringAcquire`，不形成空闲与活跃双重归属。

<details>
<summary>展开性能基准（历史 Development 构建参考值）</summary>

实测平台：Apple Silicon（macOS arm64，UE 5.8 Development 构建，Clang 21.1.6，5 轮采样中位数，来自 `GamePoolPerformanceTest.cpp`）：

| 基准项 | 中位数耗时 | 离散度 (CV%) | 吞吐量 | 对比基线与加速比 |
|---|---:|---:|---:|---|
| `TStructPool` Raw Acquire/Release | `6.80 ns/op` | `1.98%` | `147.09 Mops/s` | 对比 `FMemory::Malloc/Free` (`8.14 ns/op`) **1.20x**（零碎片） |
| `TStructPool` Scoped RAII | `5.16 ns/op` | `1.08%` | `193.80 Mops/s` | 零额外抽象惩罚 |
| `UObjectPool` Acquire/Release | `145.2 ns/op` | `0.68%` | `6.89 Mops/s` | 对比原生 `NewObject` (`542.5 ns/op`) **3.74x**（零 GC 压力） |
| `UObjectPool` Scoped + `FPoolAcquirePayload` | `199.6 ns/op` | `0.61%` | `5.01 Mops/s` | 含 RAII 租约 + 类型指纹载荷分发 |
| `ActorPool` Acquire/Release | `625.8 ns/op` | `0.76%` | `1.60 Mops/s` | 对比 `SpawnActor`/`DestroyActor` (`58,981.7 ns/op`) **94.25x** |
| 4 组件 Actor 1-Pass `ResetFlags::All` | `923.4 ns/op` | `3.27%` | `1.08 Mops/s` | 对比 `ResetFlags::None` (`747.5 ns/op`) 仅增加 `+175.9 ns` |
| `TPooledObjectHandle::IsValid()` | `8.28 ns/op` | `0.19%` | `120.82 Mops/s` | 对比裸指针判空 (`0.28 ns/op`) 仅增加 `+7.99 ns` |
| 无锁延迟归还 (Batch 512 全链路) | `260.6 ns/op` | `0.31%` | `3.84 Mops/s` | 含借出 + MPMC 无锁入队 + GameThread 批量收割 |
| `TDeque` FIFO 批量冷淘汰 (Batch 256) | `15.5 ns/op` | — | `64.52 Mops/s` | $O(1)$ 队首逐出，Min `14.8 ns` / Max `16.1 ns` |

</details>

## 文档导航

| 文档 | 内容 |
| :--- | :--- |
| [完整公共 API 契约手册](Docs/API/ObjectPoolSubsystem.md) | 借还、句柄、租约、预热、淘汰、遥测与容灾接口签名 |
| [实战接入与调优指南](Docs/Guides/ObjectPoolingGuide.md) | `IGamePoolable` 接入、三种借还模式、协程预热与最佳实践 |

<details>
<summary><b>展开源码目录</b></summary>

```text
Plugins/GamePool/
├── Docs/
│   ├── API/
│   │   └── ObjectPoolSubsystem.md          # 全量公共 API 参考手册
│   └── Guides/
│       └── ObjectPoolingGuide.md           # 接入与最佳实践实战指南
├── Source/
│   └── GamePool/
│       ├── Public/
│       │   ├── Core/
│       │   │   ├── FPoolAcquirePayload.h   # 16 字节带类型指纹零分配载荷视图
│       │   │   ├── IGamePoolable.h         # 池化对象生命周期契约接口
│       │   │   ├── ObjectPoolTypes.h       # EPoolResetFlags / FPoolStats / 遥测与淘汰配置
│       │   │   └── PoolErrorCodes.h        # Game::PoolErrorCodes (5001-5010)
│       │   ├── Handle/
│       │   │   ├── TPooledObjectHandle.h   # 世代防悬空安全句柄
│       │   │   └── TScopedPoolRental.h     # RAII 作用域自动归还租约
│       │   ├── Memory/
│       │   │   ├── TScopedStructRental.h   # 结构体池 RAII 作用域租约
│       │   │   └── TStructPool.h           # 原生 C++ 结构体定长分块内存池
│       │   ├── Subsystem/
│       │   │   └── UObjectPoolSubsystem.h  # 世界级对象池外观门面子系统 (Pimpl)
│       │   ├── Telemetry/
│       │   │   └── ObjectPoolStats.h       # STATGROUP_GamePool 统计宏声明
│       │   ├── Tests/
│       │   │   ├── AGamePoolableTestActor.h
│       │   │   └── UGamePoolableTestObject.h
│       │   └── GamePool.h                  # 公共聚合头文件
│       ├── Private/
│       │   ├── Handle/
│       │   │   ├── FPoolGenerationTracker.h
│       │   │   └── PooledObjectHandle.cpp
│       │   ├── Managers/
│       │   │   ├── FActorPoolManager.h / .cpp
│       │   │   ├── FPooledActorResetter.h / .cpp
│       │   │   ├── FUObjectPoolManager.h / .cpp
│       │   │   └── TPoolBucket.h
│       │   ├── Resilience/
│       │   │   └── FPoolResilienceController.h / .cpp
│       │   ├── Scheduling/
│       │   │   ├── FPoolAsyncLoader.h / .cpp
│       │   │   ├── FPoolDeferredDispatcher.h / .cpp
│       │   │   └── FPoolEvictionScheduler.h / .cpp
│       │   ├── Subsystem/
│       │   │   └── UObjectPoolSubsystem.cpp
│       │   ├── Telemetry/
│       │   │   └── ObjectPoolStats.cpp
│       │   ├── Tests/                      # 39 项自动化与微基准测试套件
│       │   │   ├── ActorDeepResetTest.cpp
│       │   │   ├── ActorPoolTest.cpp
│       │   │   ├── GamePoolPerformanceTest.cpp
│       │   │   ├── GamePoolTestUtils.h
│       │   │   ├── PoolAsyncAndDeferredTest.cpp
│       │   │   ├── PoolEvictionTest.cpp
│       │   │   ├── PoolHandleAndRentalTest.cpp
│       │   │   ├── PoolResilienceAndAsyncTest.cpp
│       │   │   ├── PoolTelemetryTest.cpp
│       │   │   ├── StructPoolTest.cpp
│       │   │   └── UObjectPoolTest.cpp
│       │   └── GamePool.cpp
│       └── GamePool.Build.cs
├── GamePool.uplugin
└── README.md
```

</details>
