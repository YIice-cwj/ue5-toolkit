# GamePool 插件

**GamePool** 是面向 Unreal Engine 5（UE 5.8+）的高性能通用对象池与内存池基础设施插件（仅依赖底层 `GameCore`、`GameLogging` 与引擎原生模块）。它在世界维度统一提供 `AActor` 池、非 Actor 纯 `UObject` 池、世代防悬空安全句柄、RAII 作用域租约、类型指纹零分配载荷注入、事件驱动 C++20 协程流式预热、TTL 弹性冷淘汰、多线程无锁延迟归还与低内存容灾泄洪，统一通过 `GameLogging`（`LogGamePool`）输出可观测日志，并配套零 UObject 开销的原生 C++ 结构体内存池 `TStructPool<T>`。

---

## 一、 核心功能与架构设计

### 1. 世界级对象池外观门面 (`UObjectPoolSubsystem`)
- **Pimpl 零开销隔离**：所有底层管理器、调度器与容灾控制器均置于 `Private/` 并经 `TSharedPtr<FImpl>` 封装，公共头文件极简；CDO 实例零堆分配、零 `FGCObject` 注册。
- **双轨对象池管理**：
  - **Actor 池**：复用热实例（$O(1)$ LIFO 栈顶弹出最大化 CPU 缓存局部性），自动管理可见性/碰撞/Tick 休眠与唤醒、归还前 1-Pass 单次遍历组件深层重置（`EPoolResetFlags`）、外部 `Destroy()` 委托自愈与 `OnAcquire` 重入拦截；首次生成不覆写实例构造期字段。
  - **纯 UObject 池**：严格拦截 `AActor` 派生类与抽象类，通过 `UObjectPoolSubsystem::AddReferencedObjects` 统一向 UE GC 注册 `Outer`、全部 `UClass` 键、空闲与活跃实例强引用，彻底防止池内对象被 GC 误收。
- **强类型状态契约**：全面基于 `GameCore` 的 `TGameStatusResult<T>` 与 `Game::ModuleId::Pool`（错误码 `5001`–`5010`），在借出前前置校验 `ActorClass->IsChildOf(T::StaticClass())`，杜绝类型不匹配导致的活跃对象泄漏。

### 2. 类型安全零分配载荷注入 (`FPoolAcquirePayload` & `IGamePoolable`)
- 16 字节栈值视图（`const void*` + `Game::Core::FGameTypeFingerprint`），借出时零堆分配传递只读配置/伤害/投射物参数。
- 业务侧通过 `IGamePoolable::ExtractPayload<TPayload>(InPayload)` 以 $O(1)$ 编译期 FNV-1a 类型指纹安全解包，类型不匹配直接返回 `nullptr`，杜绝裸 `const void*` 强转导致的未定义行为。
- 原生类走零反射直调快路径，蓝图派生类自动走 `Execute_OnAcquireFromPool` / `Execute_OnReleaseToPool`，确保蓝图覆写 100% 生效。

### 3. 防悬空世代句柄与 RAII 租约 (`TPooledObjectHandle<T>` & `TScopedPoolRental<T>`)
- **防 Use-After-Return**：每次借出分配单调递增世代序号，内部以不可变 `FObjectKey` 作为追踪键杜绝已销毁对象哈希碰撞，归还或复用后旧句柄自动失效（`IsValid() == false`、`Get() == nullptr`）。
- **作用域自动归还**：`TScopedPoolRental<T>` 支持移动语义、解引用（`->` / `*`）、`EarlyRelease()` 单次校验提前归还与 `[[nodiscard]] ReleaseAndDetach()` 所有权分离。

### 4. 事件驱动协程预热、动态 TTL 淘汰与无锁延迟归还
- **事件驱动流式预热 (`PrewarmAsync` / `PrewarmObjectPoolAsync`)**：同时支持 `TSubclassOf`、`FSoftObjectPath` 与 `TSoftClassPtr`；基于 `FStreamableManager` 回调与 `GameCore` 的 `FGameCoroSuspensionGuard` 事件驱动唤醒（零逐帧轮询），结合 `UTimeSlicedTaskSubsystem` 分帧限流、触达 `MaxIdleCount` 自动短路与 `FCancellationToken` 协作取消。
- **双端队列 TTL 弹性淘汰 (`TPoolBucket` + `UThrottledTickerSubsystem`)**：底层采用 `TDeque<FIdleEntry>` 统一绑定对象引用与归还时间戳（消除平行数组失步），实现 $O(1)$ LIFO 热借出与 $O(1)$ FIFO 冷淘汰；后台心跳托管至 `GameCore::UThrottledTickerSubsystem`，自动收敛全部启用池的最小检测周期，全停时自动注销零空转，并交替调度 Actor/UObject 防止饥饿。
- **跨线程无锁延迟归还 (`ReleaseActorDeferred` / `ReleaseObjectDeferred`)**：基于 `GameCore::TLockFreeRingBuffer<FDeferredReleaseEntry, 1024>`，工作线程无锁入队，主线程帧首调用 `ProcessDeferredReleases(MaxProcessCount)` 批量收割；精确返回实际成功归还数并提供 `GetPendingDeferredReleaseCount()` 积压观测。

### 5. GC 安全的低内存应急泄洪 (`FPoolResilienceController`)
- 监听 `FCoreDelegates::GetMemoryTrimDelegate()`，在系统低内存警告或 `UEngine::TrimMemory()` 时按各池 `MinPreservedCount` 保底优先从冷端泄洪。
- **GC 安全守卫**：自动过滤常规 GC / 增量清理内部触发的 `FMemory::Trim()`（`IsGarbageCollecting() || IsIncrementalPurgePending()`），既杜绝在 GC 期间非法销毁 Actor/UObject，又防止周期性常规 GC 误清预热缓存；非主线程触发时自动经弱宿主守卫切回 GameThread。

### 6. 原生 C++ 结构体定长内存池 (`TStructPool<T, BlocksPerChunk>` & `TScopedStructRental<T, BlocksPerChunk>`)
- 基于 `GameCore::TFixedSizePool<T, BlocksPerChunk>` 构建，原地构造/析构（支持任意参数转发 `Acquire(Args...)` / `AcquireScoped(Args...)`），零堆碎片、纳秒级吞吐，提供 `GetAllocatedCount()` / `GetFreeCount()` / `GetChunkCount()` / `GetTotalCapacity()` 完整观测。

---

## 二、 目录结构

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

---

## 三、 快速使用示例

### 1. 实现 `IGamePoolable` 与类型安全载荷解包
```cpp
#include "Core/IGamePoolable.h"

struct FProjectileInitPayload {
    float InitialSpeed = 1200.0f;
    float BaseDamage = 35.0f;
};

UCLASS()
class AMyProjectile : public AActor, public IGamePoolable {
    GENERATED_BODY()

public:
    virtual void OnAcquireFromPool_Implementation() override {
        bIsActive = true;
    }

    virtual void OnAcquireFromPoolWithPayload(FPoolAcquirePayload InPayload) override {
        if (const FProjectileInitPayload* Config = IGamePoolable::ExtractPayload<FProjectileInitPayload>(InPayload)) {
            CurrentSpeed = Config->InitialSpeed;
            CurrentDamage = Config->BaseDamage;
        }
    }

    virtual void OnReleaseToPool_Implementation() override {
        bIsActive = false;
    }
};
```

### 2. Actor / UObject 借还（句柄、RAII 租约、载荷与协程预热）
```cpp
#include "Subsystem/UObjectPoolSubsystem.h"

void AMyWeapon::FireWithPool(const TSoftClassPtr<AActor>& BulletSoftClass) {
    UObjectPoolSubsystem& Pool = UObjectPoolSubsystem::Get(this);

    // 1. 携带类型指纹载荷借出 RAII 租约（作用域结束自动归还）
    const FProjectileInitPayload InitData{1800.0f, 50.0f};
    TGameStatusResult<TScopedPoolRental<AMyProjectile>> RentalRes =
        Pool.AcquireScopedActorWithPayload<AMyProjectile>(AMyProjectile::StaticClass(), GetActorTransform(), InitData);
    if (RentalRes.IsSuccess()) {
        TScopedPoolRental<AMyProjectile> Rental = RentalRes.ExtractValue();
        Rental->Launch();
    }

    // 2. 跨帧持有安全世代句柄（防 Use-After-Return）
    TGameStatusResult<TPooledObjectHandle<AMyProjectile>> HandleRes =
        Pool.AcquireActorHandle<AMyProjectile>(AMyProjectile::StaticClass(), GetActorTransform());
    if (HandleRes.IsSuccess()) {
        TPooledObjectHandle<AMyProjectile> Handle = HandleRes.GetValue();
        if (Handle.IsValid()) {
            (void)Pool.ReleaseActor(Handle);
        }
    }
}

TGameCoroTask<int32> AMyWeapon::PrewarmAmmoCoro(TSoftClassPtr<AActor> BulletSoftClass, FCancellationToken Token) {
    UObjectPoolSubsystem& Pool = UObjectPoolSubsystem::Get(this);
    co_return co_await Pool.PrewarmAsync(BulletSoftClass, 20, 2, MoveTemp(Token));
}
```

### 3. 原生 C++ 结构体内存池 (`TStructPool`)
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
} // 离开作用域原地析构并纳秒级回池
```

---

## 四、 错误码规范 (`Game::PoolErrorCodes`)

全部错误结果均归属 `Game::ModuleId::Pool`，定义于 [PoolErrorCodes.h](file:///Users/yiice/code/UE5/UEToolkit/Plugins/GamePool/Source/GamePool/Public/Core/PoolErrorCodes.h)：

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

---

## 五、 性能基准

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

---

## 六、 延伸文档

- 完整公共 API 契约手册：[ObjectPoolSubsystem.md](file:///Users/yiice/code/UE5/UEToolkit/Plugins/GamePool/Docs/API/ObjectPoolSubsystem.md)
- 实战接入与调优指南：[ObjectPoolingGuide.md](file:///Users/yiice/code/UE5/UEToolkit/Plugins/GamePool/Docs/Guides/ObjectPoolingGuide.md)
