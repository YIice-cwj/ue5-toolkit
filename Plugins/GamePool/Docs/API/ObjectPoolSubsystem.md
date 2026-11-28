# GamePool API 参考

> **一句话**：Actor / 纯 UObject 双轨对象池、世代防悬空句柄、RAII 租约、协程预热、TTL 冷淘汰与低内存泄洪的统一门面。
> **头文件**：`GamePool.h`（聚合入口），或按子域包含 `Subsystem/UObjectPoolSubsystem.h`、`Core/*`、`Handle/*`、`Memory/*`
> **线程**：借出、归还、预热、淘汰与清空仅限 GameThread；工作线程只能 `ReleaseActorDeferred` / `ReleaseObjectDeferred` 入队，再由 GameThread 执行 `ProcessDeferredReleases`
> **失败语义**：`TGameStatusResult<T>` + `Game::PoolErrorCodes`（归属 `Game::ModuleId::Pool`）

```cpp
#include "GamePool.h"                             // 聚合入口
#include "Core/IGamePoolable.h"                   // 生命周期回调与载荷解包
#include "Core/FPoolAcquirePayload.h"             // 16 字节类型指纹载荷视图
#include "Core/ObjectPoolTypes.h"                 // EPoolResetFlags / FPoolStats / FPoolTelemetrySnapshot / FPoolEvictionSettings
#include "Core/PoolErrorCodes.h"                  // Game::PoolErrorCodes
#include "Handle/TPooledObjectHandle.h"           // 世代防悬空句柄
#include "Handle/TScopedPoolRental.h"             // RAII 作用域租约
#include "Memory/TStructPool.h"                   // 原生 C++ 结构体内存池
#include "Memory/TScopedStructRental.h"           // 结构体池 RAII 租约
#include "Telemetry/ObjectPoolStats.h"            // STATGROUP_GamePool
```

## 30 秒速览

- **双轨池**：`AActor` 与纯 `UObject` 各自独立预热、借出、归还、容量控制与统计，互不串用。
- **防悬空**：`TPooledObjectHandle<T>` 以 `FWeakObjectPtr + uint32 Generation` 校验，拒绝 Use-After-Return 与跨轮次窜号。
- **自动归还**：`TScopedPoolRental<T>` 离开作用域按类型自动归还；`TStructPool` 为原生 C++ 结构体提供同构的 `TScopedStructRental`。
- **弹性治理**：协程分帧预热、无锁延迟归还、TTL 冷淘汰与低内存自动泄洪；统计经 `stat GamePool` 与 Unreal Insights 暴露。
- **契约错误**：类无效、类型不匹配、重复归还、句柄过期、容量超限与队列满均可判定，统一返回 `TGameStatusResult`。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `UObjectPoolSubsystem::Prewarm` | 同步预热 Actor 池 | 仅 GameThread；超过 `MaxIdleCount` 返回 `5009` |
| `UObjectPoolSubsystem::PrewarmAsync` | 协程分帧预热（含软路径） | 仅 GameThread；`MaxSpawnsPerStep` 分帧防尖刺 |
| `UObjectPoolSubsystem::AcquireActor` | 从 Actor 池借出实例 | 仅 GameThread；类无效 `5001`、类型不匹配 `5002` |
| `UObjectPoolSubsystem::AcquireActorHandle` | 借出并返回世代校验句柄 | 归还后句柄立即失效（`5008`） |
| `UObjectPoolSubsystem::AcquireScopedActor` | 借出 RAII 租约，离开作用域自动归还 | `ReleaseAndDetach()` 可放弃自动归还 |
| `UObjectPoolSubsystem::ReleaseActor` | 将 Actor 归还入池 | 仅 GameThread；重复归还 `5007`、实例无效 `5006` |
| `UObjectPoolSubsystem::ReleaseActorDeferred` | 无锁入队延迟归还 | 任意线程可调用；队列 1024 槽，满返回 `5010` |
| `UObjectPoolSubsystem::ProcessDeferredReleases` | GameThread 收割延迟归还队列 | 仅 GameThread；单次默认处理 64 条 |

## 1. 载荷与生命周期契约

### `FPoolAcquirePayload`

```cpp
struct GAMEPOOL_API FPoolAcquirePayload {
    template <typename TPayload> [[nodiscard]] static FPoolAcquirePayload From(const TPayload& InPayload) noexcept;
    [[nodiscard]] constexpr bool IsValid() const noexcept;
    template <typename TPayload> [[nodiscard]] bool IsType() const noexcept;
    template <typename TPayload> [[nodiscard]] const TPayload* Extract() const noexcept;
    [[nodiscard]] constexpr uint64 GetTypeFingerprint() const noexcept;
};
```

- 16 字节栈值非拥有只读视图：`const void* Data` + 编译期 `uint64 TypeFingerprint`（GameCore CityHash64）。
- 借出期零堆分配传递参数；`Extract<T>()` 校验指纹，类型不符返回 `nullptr`，避免裸 `void*` 误解包。

### `IGamePoolable`

```cpp
UINTERFACE(BlueprintType, Blueprintable) class GAMEPOOL_API UGamePoolable : public UInterface { GENERATED_BODY() };

class GAMEPOOL_API IGamePoolable {
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintNativeEvent, Category = "GamePool|ObjectPool")
    void OnAcquireFromPool();
    virtual void OnAcquireFromPoolWithPayload(FPoolAcquirePayload InPayload);
    UFUNCTION(BlueprintNativeEvent, Category = "GamePool|ObjectPool")
    void OnReleaseToPool();
    template <typename TPayload>
    [[nodiscard]] static const TPayload* ExtractPayload(FPoolAcquirePayload InPayload) noexcept;
};
```

| 回调 | 触发时机 | 说明 |
| :--- | :--- | :--- |
| `OnAcquireFromPool` | 借出时 | 无载荷借出仅触发一次；带载荷借出先触发此回调 |
| `OnAcquireFromPoolWithPayload` | 带载荷借出时 | 确认实例仍在活跃集后触发，与上一回调不重复 |
| `OnReleaseToPool` | 归还入池时 | 执行业务态清理 |

- 原生 C++ 派生类直调 `*_Implementation` 快路径；蓝图派生类由子系统派发 `Execute_*`，保证事件节点生效。
- 回调内自归还或自销毁会使本次借出失败（`5005`），不会形成空闲与活跃双重归属。

## 2. 安全句柄与 RAII 租约

### `TPooledObjectHandle<T>`

```cpp
template <typename T> class TPooledObjectHandle {
public:
    bool IsValid() const;
    explicit operator bool() const;
    T* Get() const;
    T* operator->() const;
    T& operator*() const;
    uint32 GetGeneration() const noexcept;
    UObjectPoolSubsystem* GetSubsystem() const;
    void Reset() noexcept;
    bool operator==(const TPooledObjectHandle& Other) const noexcept; // C++20 合成 !=
};
```

### `TScopedPoolRental<T>`

```cpp
template <typename T> class TScopedPoolRental {
public:
    bool IsValid() const;
    explicit operator bool() const;
    T* Get() const;
    T* operator->() const;
    T& operator*() const;
    const TPooledObjectHandle<T>& GetHandle() const noexcept;
    [[nodiscard]] T* ReleaseAndDetach() noexcept;
    void EarlyRelease();
};
```

- 不可拷贝、可移动；析构与 `EarlyRelease()` 依据 `std::is_base_of_v<AActor, T>` 自动走 `ReleaseActor` 或 `ReleaseObject`。
- `ReleaseAndDetach()` 放弃自动归还并返回裸指针，转由调用方手动归还。
- 句柄只保存弱对象、弱子系统与世代号，不做 `GUObjectArray` 反向索引；`Release` 后立即失效。

```cpp
TGameStatusResult<TPooledObjectHandle<AMyProjectile>> HandleRes =
    Pool.AcquireActorHandle<AMyProjectile>(AMyProjectile::StaticClass(), SpawnTransform);
if (HandleRes.IsSuccess()) {
    TPooledObjectHandle<AMyProjectile> Handle = HandleRes.GetValue();
    Handle->Fire();
    (void)Pool.ReleaseActor(Handle);
    check(!Handle.IsValid() && Handle.Get() == nullptr);
}

{
    TGameStatusResult<TScopedPoolRental<AMyProjectile>> RentalRes =
        Pool.AcquireScopedActor<AMyProjectile>(AMyProjectile::StaticClass(), SpawnTransform);
    if (RentalRes.IsSuccess()) {
        TScopedPoolRental<AMyProjectile> Rental = RentalRes.ExtractValue();
        Rental->Fire();
    } // 离开作用域自动归还
}
```

## 3. `UObjectPoolSubsystem` 公共接口

`UWorldSubsystem` 门面，`TSharedPtr<FImpl>` 以 Pimpl 隐藏全部管理器、调度器与容灾组件；CDO 零堆分配、零 `FGCObject` 注册，日志统一经 `LogGamePool` 输出。

| 参数 / 返回 | 语义 |
| :--- | :--- |
| `ActorClass` / `ObjectClass` | 目标类；纯 UObject 池拒绝 `AActor` 派生类（`5001`） |
| `SpawnTransform` | 借出 Actor 的初始空间变换；默认 `FTransform::Identity` |
| `Payload` | 借出期只读载荷，经 `FPoolAcquirePayload` 传递 |
| `MaxIdleCount` | 空闲缓存上限，`<= 0` 表示无限制 |
| `TPooledObjectHandle<T>` | 带世代校验的受管句柄 |
| `TScopedPoolRental<T>` | 作用域自动归还租约 |
| `TGameStatusResult<T>` | 失败携带 `Game::ModuleId::Pool` 与 5001-5010 错误码 |

```cpp
UCLASS()
class GAMEPOOL_API UObjectPoolSubsystem : public UWorldSubsystem {
    GENERATED_BODY()
public:
    static UObjectPoolSubsystem& Get(const UObject* WorldContextObject);
    static UObjectPoolSubsystem* GetPtr(const UObject* WorldContextObject);
    static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

    // Actor 池：预热、容量与重置策略
    void Prewarm(TSubclassOf<AActor> ActorClass, int32 PrewarmCount);
    TGameCoroTask<int32> PrewarmAsync(TSubclassOf<AActor> ActorClass, int32 PrewarmCount, int32 MaxSpawnsPerStep = 1, FCancellationToken CancellationToken = FCancellationToken::None());
    TGameCoroTask<int32> PrewarmAsync(const FSoftObjectPath& ActorClassPath, int32 PrewarmCount, int32 MaxSpawnsPerStep = 1, FCancellationToken CancellationToken = FCancellationToken::None());
    TGameCoroTask<int32> PrewarmAsync(const TSoftClassPtr<AActor>& ActorSoftClass, int32 PrewarmCount, int32 MaxSpawnsPerStep = 1, FCancellationToken CancellationToken = FCancellationToken::None());
    void SetMaxPoolCapacity(TSubclassOf<AActor> ActorClass, int32 InMaxIdleCount);
    void SetPoolResetFlags(TSubclassOf<AActor> ActorClass, EPoolResetFlags InResetFlags);
    [[nodiscard]] EPoolResetFlags GetPoolResetFlags(TSubclassOf<AActor> ActorClass) const;

    // Actor 池：借出与归还（K2_* 为蓝图外观）
    AActor* K2_AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform);
    template <typename T = AActor> [[nodiscard]] TGameStatusResult<T*> AcquireActor(TSubclassOf<AActor> ActorClass = T::StaticClass(), const FTransform& SpawnTransform = FTransform::Identity);
    template <typename T = AActor, typename TPayload> [[nodiscard]] TGameStatusResult<T*> AcquireActorWithPayload(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform, const TPayload& Payload);
    template <typename T = AActor> [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireActorHandle(TSubclassOf<AActor> ActorClass = T::StaticClass(), const FTransform& SpawnTransform = FTransform::Identity);
    template <typename T = AActor, typename TPayload> [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireActorHandleWithPayload(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform, const TPayload& Payload);
    template <typename T = AActor> [[nodiscard]] TGameStatusResult<TScopedPoolRental<T>> AcquireScopedActor(TSubclassOf<AActor> ActorClass = T::StaticClass(), const FTransform& SpawnTransform = FTransform::Identity);
    template <typename T = AActor, typename TPayload> [[nodiscard]] TGameStatusResult<TScopedPoolRental<T>> AcquireScopedActorWithPayload(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform, const TPayload& Payload);
    void K2_ReleaseActor(AActor* Actor);
    TGameStatusResult<void> ReleaseActor(AActor* Actor);
    template <typename T> TGameStatusResult<void> ReleaseActor(const TPooledObjectHandle<T>& Handle);
    TGameStatusResult<void> ReleaseActorDeferred(AActor* Actor);

    // 纯 UObject 池：预热、容量、借出与归还
    void PrewarmObjectPool(TSubclassOf<UObject> ObjectClass, int32 PrewarmCount);
    TGameCoroTask<int32> PrewarmObjectPoolAsync(TSubclassOf<UObject> ObjectClass, int32 PrewarmCount, int32 MaxSpawnsPerStep = 4, FCancellationToken CancellationToken = FCancellationToken::None());
    TGameCoroTask<int32> PrewarmObjectPoolAsync(const FSoftObjectPath& ObjectClassPath, int32 PrewarmCount, int32 MaxSpawnsPerStep = 4, FCancellationToken CancellationToken = FCancellationToken::None());
    TGameCoroTask<int32> PrewarmObjectPoolAsync(const TSoftClassPtr<UObject>& ObjectSoftClass, int32 PrewarmCount, int32 MaxSpawnsPerStep = 4, FCancellationToken CancellationToken = FCancellationToken::None());
    void SetMaxObjectPoolCapacity(TSubclassOf<UObject> ObjectClass, int32 InMaxIdleCount);
    UObject* K2_AcquireObject(TSubclassOf<UObject> ObjectClass);
    template <typename T = UObject> [[nodiscard]] TGameStatusResult<T*> AcquireObject(TSubclassOf<UObject> ObjectClass = T::StaticClass());
    template <typename T = UObject, typename TPayload> [[nodiscard]] TGameStatusResult<T*> AcquireObjectWithPayload(TSubclassOf<UObject> ObjectClass, const TPayload& Payload);
    template <typename T = UObject> [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireObjectHandle(TSubclassOf<UObject> ObjectClass = T::StaticClass());
    template <typename T = UObject, typename TPayload> [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireObjectHandleWithPayload(TSubclassOf<UObject> ObjectClass, const TPayload& Payload);
    template <typename T = UObject> [[nodiscard]] TGameStatusResult<TScopedPoolRental<T>> AcquireScopedObject(TSubclassOf<UObject> ObjectClass = T::StaticClass());
    template <typename T = UObject, typename TPayload> [[nodiscard]] TGameStatusResult<TScopedPoolRental<T>> AcquireScopedObjectWithPayload(TSubclassOf<UObject> ObjectClass, const TPayload& Payload);
    void K2_ReleaseObject(UObject* Object);
    TGameStatusResult<void> ReleaseObject(UObject* Object);
    template <typename T> TGameStatusResult<void> ReleaseObject(const TPooledObjectHandle<T>& Handle);
    TGameStatusResult<void> ReleaseObjectDeferred(UObject* Object);

    // 无锁延迟归还收割
    int32 ProcessDeferredReleases(int32 MaxProcessCount = 64);
    [[nodiscard]] int32 GetPendingDeferredReleaseCount() const noexcept;

    // 弹性缩容与 TTL 冷淘汰
    void SetActorPoolEvictionSettings(TSubclassOf<AActor> ActorClass, const FPoolEvictionSettings& Settings);
    [[nodiscard]] FPoolEvictionSettings GetActorPoolEvictionSettings(TSubclassOf<AActor> ActorClass) const;
    void SetObjectPoolEvictionSettings(TSubclassOf<UObject> ObjectClass, const FPoolEvictionSettings& Settings);
    [[nodiscard]] FPoolEvictionSettings GetObjectPoolEvictionSettings(TSubclassOf<UObject> ObjectClass) const;
    int32 ForceEvictExpired(int32 MaxEvictionsPerStep = 32, double CustomNowSeconds = 0.0);

    // 遥测、容灾与统计
    [[nodiscard]] FPoolTelemetrySnapshot GetTelemetrySnapshot() const;
    void ResetTelemetry();
    int32 FlushAllIdleInstances(bool bPreserveMinCount = false);
    void SetLowMemoryAutoFlushEnabled(bool bEnable);
    [[nodiscard]] bool IsLowMemoryAutoFlushEnabled() const noexcept;
    [[nodiscard]] FPoolStats GetPoolStats(TSubclassOf<AActor> ActorClass) const;
    [[nodiscard]] FPoolStats GetObjectPoolStats(TSubclassOf<UObject> ObjectClass) const;
    void ClearPool(TSubclassOf<AActor> ActorClass);
    void ClearAllActorPools();
    void ClearObjectPool(TSubclassOf<UObject> ObjectClass);
    void ClearAllObjectPools();
    void ClearAllPools();
};
```

- **线程**：除 `Release*Deferred` 与 `GetPendingDeferredReleaseCount` 外的写操作均限 GameThread；延迟归还队列为 1024 槽无锁环形缓冲。
- **生命周期**：`UWorldSubsystem` 随世界创建/销毁；`AddReferencedObjects` 统一追踪全部类键与空闲/活跃实例；`Deinitialize` 清理全部池实例、延迟队列、调度器与容灾委托，池对象不跨世界存活。
- **预热**：`Prewarm` / `PrewarmObjectPool` 同步生成；`Prewarm*Async` 为 C++20 协程，软路径由流式加载事件驱动，`MaxSpawnsPerStep` 分帧防尖刺，受 `MaxIdleCount` 约束。
- **容灾**：`FlushAllIdleInstances(true)` 保留各池 `MinPreservedCount`；低内存自动泄洪默认开启。
- **失败语义**：类无效 `5001`、模板类型不匹配 `5002`、上下文失效 `5003`、实例化失败 `5004`、借出期自销毁 `5005`、实例无效 `5006`、非本池活跃成员 `5007`、句柄过期 `5008`、容量超限 `5009`、延迟队列满 `5010`。

```cpp
UObjectPoolSubsystem& Pool = UObjectPoolSubsystem::Get(this);
Pool.Prewarm(AMyProjectile::StaticClass(), 32);

TGameStatusResult<AMyProjectile*> Res = Pool.AcquireActor<AMyProjectile>(AMyProjectile::StaticClass(), SpawnTransform);
if (Res.IsSuccess()) {
    AMyProjectile* Projectile = Res.GetValue();
    // 工作线程可安全改为 Pool.ReleaseActorDeferred(Projectile)
    Pool.ReleaseActor(Projectile);
}
```

## 4. 错误码契约

全部错误绑定 `Game::ModuleId::Pool`，定义于 `Core/PoolErrorCodes.h`。

| 错误码 | 名称 | 触发条件 |
| :---: | :--- | :--- |
| `5001` | `InvalidClass` | 类型为空、抽象类，或向纯 UObject 池传入 `AActor` 派生类 |
| `5002` | `TypeMismatch` | 传入类不是强类型模板参数 `T` 的子类 |
| `5003` | `InvalidContext` | `World` / `Outer` / 子系统未初始化或已销毁 |
| `5004` | `SpawnFailed` | `SpawnActor` 或 `NewObject` 返回空指针 |
| `5005` | `InvalidatedDuringAcquire` | 借出回调内自归还或自销毁 |
| `5006` | `InvalidInstance` | 待归还实例为空、已销毁或已标记 GC |
| `5007` | `NotOwnedActiveInstance` | 实例不属于当前池的活跃借出集合 |
| `5008` | `InvalidHandle` | 句柄无效、世代过期或归属其他子系统 |
| `5009` | `CapacityLimitReached` | 空闲池已达 `MaxIdleCount` 上限 |
| `5010` | `DeferredQueueFull` | 延迟归还环形缓冲已满或未初始化 |

## 5. 淘汰配置与重置掩码

`FPoolEvictionSettings`（`Core/ObjectPoolTypes.h`）：

| 字段 | 默认值 | 语义 |
| :--- | :---: | :--- |
| `bEnableEviction` | `false` | 是否启用闲置超时自动淘汰 |
| `IdleTimeToLiveSeconds` | `60.0f` | 冷实例闲置生存期（秒，`ClampMin=0.1`） |
| `MinPreservedCount` | `0` | TTL 淘汰与保底泄洪时优先保留的空闲实例数 |
| `CheckIntervalSeconds` | `5.0f` | 自动检测心跳周期（秒，多池并存取活跃池最小周期） |
| `MaxEvictionsPerStep` | `2` | 单步平滑销毁配额，防单帧 GC 尖刺 |

- `ForceEvictExpired` 单次总配额默认 `32`，同时受各池 `MaxEvictionsPerStep` 限流；`CustomNowSeconds <= 0` 使用硬件单调时钟。
- 淘汰调度仅在启用 `bEnableEviction` 的池上运行；停用后自动同步停机。

`EPoolResetFlags`（`uint16` 位掩码，默认 `All`）：

- `None`：跳过全部自动重置。
- `Timers`：`ClearAllTimersForObject` 清理 World 定时器。
- `Physics`：仅对 `IsSimulatingPhysics()` 的 `UPrimitiveComponent` 清零线/角速度。
- `Movement`：`StopMovementImmediately()` 停止惯性。
- `Audio`：单次遍历停止活跃 `UAudioComponent`（零中间数组分配）。
- `FXSystem`：单次遍历对 `UFXSystemComponent` 调用 `DeactivateImmediate()`，覆盖 Niagara 与 Cascade。
- `Animation`：存在有效 `SkeletalMesh` 或 `AnimInstance` 时执行 `Stop()` + `ClearAnimScriptInstance()` + `InitAnim(true)`。
- `PawnAndAI`：`APawn` 且绑定 `Controller` 时执行 `UnPossess()`。

## 6. 可观测性

### 引擎 Stats 与 Insights

`stat GamePool` 提供 Cycle Stat：`Acquire Actor Total`、`Release Actor Total`、`Acquire UObject Total`、`Release UObject Total`、`Component Deep Reset`、`Prewarm Step Time`、`Eviction Step Time`；Counter：`Active/Idle Actors`、`Active/Idle UObjects`、`Pool Cache Hits/Misses`（`Deinitialize` 自动清零平衡）。

Unreal Insights 作用域：`GamePool_AcquireActor`、`GamePool_ReleaseActor`、`GamePool_AcquireObject`、`GamePool_ReleaseObject`、`GamePool_ResetActorComponents`、`GamePool_PrewarmStep`、`GamePool_PrewarmObjectStep`、`GamePool_EvictActors`、`GamePool_EvictObjects`、`GamePool_ProcessDeferredReleases`。

### `FPoolTelemetrySnapshot`

包含 `TotalAcquires`、`CacheHits`、`CacheMisses`、`TotalActiveCount`、`TotalIdleCount` 与 `GetHitRatePercentage()`（无借出时返回 `0`）。仅成功实例化后递增，`SpawnActor` / `NewObject` 失败不污染命中率。单池快照 `FPoolStats` 提供 `TotalCount` / `ActiveCount` / `IdleCount`；`ResetTelemetry` 只清计数器，不影响当前实例数。

## 7. 原生 C++ 结构体内存池

`TStructPool<T, BlocksPerChunk = 64>` 基于 GameCore `TFixedSizePool`，提供非 UObject 类型的高性能分块池化。

| 方法 | 说明 |
| :--- | :--- |
| `Acquire(TArgs&&... Args)` | 定长内存块原地构造 `T`，返回指针或 `nullptr` |
| `AcquireScoped(TArgs&&... Args)` | 返回 `TScopedStructRental<T, BlocksPerChunk>` |
| `Release(T* Instance)` | 原地析构并归还自由链表，允许 `nullptr` |
| `ReserveChunks(size_t)` | 预分配连续 Chunk，消除热路径首次扩容抖动 |
| `Clear()` / `IsEmpty()` | 清空底层 Chunk / 判断无活跃借出 |
| `GetAllocatedCount()` / `GetFreeCount()` / `GetChunkCount()` / `GetTotalCapacity()` | 活跃、空闲、Chunk 与总槽位统计 |

`TScopedStructRental` 支持 `IsValid()`、`operator bool()`、`Get()`、`operator->()`、`operator*()`、`EarlyRelease()` 与 `ReleaseAndDetach()`。

- **生命周期**：所有借出指针与租约归还/析构前，宿主 `TStructPool` 不得移动或销毁；池自身可移动，但要求 `IsEmpty()`（`GetAllocatedCount() == 0`）。

## 常见错误与边界

- **跨线程误用**：工作线程直接调用 `Acquire*` / `Release*` / `Prewarm*` 会破坏 GameThread 契约；跨线程归还只能走 `Release*Deferred`。
- **重复归还**：同一实例二次归还返回 `NotOwnedActiveInstance (5007)`；空或已销毁实例返回 `InvalidInstance (5006)`。
- **句柄跨池**：句柄归属其他子系统或世代已过期返回 `InvalidHandle (5008)`；归还后不得再解引用。
- **容量上限**：预热超过 `MaxIdleCount` 返回 `CapacityLimitReached (5009)`；`SetMax*Capacity(..., <=0)` 表示无限制。
- **延迟队列**：队列 1024 槽，满时返回 `DeferredQueueFull (5010)`；需在 GameThread 定期 `ProcessDeferredReleases`。
- **借出期自毁**：在 `OnAcquireFromPool*` 内自归还或自销毁直接失败（`5005`），避免双重归属。
- **低内存泄洪**：自动泄洪默认开启，过滤 GC 与增量清理期间的 `Trim()`；非主线程触发时经弱宿主守卫切回 GameThread。

## 相关文档

- [插件 README](../../README.md)
- [架构参考：GameCore 缓存与内存治理](../../../GameCore/Docs/Architecture/CachingAndMemory.md)
- [对象池接入与调优指南](../Guides/ObjectPoolingGuide.md)
