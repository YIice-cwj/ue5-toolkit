# GamePool 通用对象与内存池 API 参考

`UObjectPoolSubsystem` 是一个世界子系统（`UWorldSubsystem`），经由 Pimpl（`TSharedPtr<FImpl>`）将所有底层管理器、调度器与容灾组件完整隐藏至 `Private/`，统一编排 `AActor` 池、非 Actor 纯 `UObject` 池、世代防悬空安全句柄、RAII 租约、事件驱动 C++20 协程预热、TTL 弹性冷淘汰与低内存容灾泄洪（日志统一通过 `GameLogging` 插件以 `LogGamePool` 分类分发）；并配合 `TStructPool<T, BlocksPerChunk>` 覆盖非 UObject 原生 C++ 结构体的高性能内存池化。

公共头文件：
- `#include "GamePool.h"`（推荐，一站式汇聚全部公共池化契约接口）
- `#include "Subsystem/UObjectPoolSubsystem.h"`（世界级对象池外观门面子系统）
- `#include "Core/IGamePoolable.h"`（生命周期回调与载荷安全解包接口）
- `#include "Core/FPoolAcquirePayload.h"`（16 字节栈值类型指纹载荷视图）
- `#include "Core/ObjectPoolTypes.h"`（`EPoolResetFlags`、`FPoolStats`、`FPoolTelemetrySnapshot`、`FPoolEvictionSettings`）
- `#include "Core/PoolErrorCodes.h"`（`Game::PoolErrorCodes` 强类型错误码常量）
- `#include "Handle/TPooledObjectHandle.h"`（世代防悬空安全句柄）
- `#include "Handle/TScopedPoolRental.h"`（池化对象 RAII 作用域自动归还租约）
- `#include "Memory/TStructPool.h"`（原生 C++ 结构体定长分块内存池）
- `#include "Memory/TScopedStructRental.h"`（结构体池 RAII 作用域租约）
- `#include "Telemetry/ObjectPoolStats.h"`（`STATGROUP_GamePool` 引擎统计宏）

---

## 1. 载荷视图 `FPoolAcquirePayload` 与契约接口 `IGamePoolable`

### 1.1 `FPoolAcquirePayload`
16 字节栈值非拥有只读视图，内部保存 `const void* Data` 与 `Game::Core::FGameTypeFingerprint TypeFingerprint`（基于 `GameCore` 编译期 FNV-1a 类型指纹），借出期零堆分配传递只读参数：

```cpp
struct FPoolAcquirePayload {
    template <typename TPayload>
    [[nodiscard]] static FPoolAcquirePayload From(const TPayload& InPayload) noexcept;

    [[nodiscard]] constexpr bool IsValid() const noexcept;

    template <typename TPayload>
    [[nodiscard]] bool IsType() const noexcept;

    template <typename TPayload>
    [[nodiscard]] const TPayload* Extract() const noexcept;

    [[nodiscard]] constexpr uint64 GetTypeFingerprint() const noexcept;
};
```

### 1.2 `IGamePoolable`
需要接入对象池生命周期回调的 `AActor` 或纯 `UObject` 派生类均可继承并实现此接口：

```cpp
UINTERFACE(BlueprintType, Blueprintable)
class GAMEPOOL_API UGamePoolable : public UInterface {
    GENERATED_BODY()
};

class GAMEPOOL_API IGamePoolable {
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, Category = "GamePool|ObjectPool")
    void OnAcquireFromPool();
    virtual void OnAcquireFromPool_Implementation() {}

    virtual void OnAcquireFromPoolWithPayload(FPoolAcquirePayload InPayload) {
        (void)InPayload;
    }

    UFUNCTION(BlueprintNativeEvent, Category = "GamePool|ObjectPool")
    void OnReleaseToPool();
    virtual void OnReleaseToPool_Implementation() {}

    template <typename TPayload>
    [[nodiscard]] static const TPayload* ExtractPayload(FPoolAcquirePayload InPayload) noexcept {
        return InPayload.Extract<TPayload>();
    }
};
```

> **回调顺序契约**：
> - 无载荷借出时仅触发一次 `OnAcquireFromPool`；
> - 携带有效载荷借出时，先触发 `OnAcquireFromPool`，确认对象仍在活跃集且有效后再触发 `OnAcquireFromPoolWithPayload(Payload)`，两次回调互不重复调用；
> - 原生 C++ 类走零反射开销快路径（`OnAcquireFromPool_Implementation` / `OnReleaseToPool_Implementation`），蓝图派生类自动派发 `Execute_OnAcquireFromPool` / `Execute_OnReleaseToPool` 以确保蓝图事件节点生效。

---

## 2. 安全代数句柄与 RAII 作用域租约

### 2.1 `TPooledObjectHandle<T>` (`Handle/TPooledObjectHandle.h`)
直接持有 `FWeakObjectPtr`、弱子系统引用与 `uint32 Generation`（零 `GUObjectArray` 反向索引开销），彻底杜绝 **Use-After-Return** 与跨轮次复用窜号覆写：
- `bool IsValid() const` / `explicit operator bool() const`
- `T* Get() const` / `T* operator->() const` / `T& operator*() const`
- `uint32 GetGeneration() const noexcept`
- `UObjectPoolSubsystem* GetSubsystem() const`
- `void Reset() noexcept`
- `bool operator==(const TPooledObjectHandle& Other) const noexcept`（C++20 自动合成 `!=`）

### 2.2 `TScopedPoolRental<T>` (`Handle/TScopedPoolRental.h`)
不可拷贝、支持移动语义的 RAII 作用域租约卫士，离开作用域自动根据 `std::is_base_of_v<AActor, T>` 归还至 `ReleaseActor` 或 `ReleaseObject`：
- `bool IsValid() const` / `explicit operator bool() const`
- `T* Get() const` / `T* operator->() const` / `T& operator*() const`
- `const TPooledObjectHandle<T>& GetHandle() const noexcept`
- `[[nodiscard]] T* ReleaseAndDetach() noexcept`（放弃自动归还并返回底层对象指针，转由调用方手动归还）
- `void EarlyRelease()`（单次校验直接归还并重置句柄，避免重复句柄查表）

```cpp
// 1. 安全代数句柄
TGameStatusResult<TPooledObjectHandle<AMyProjectile>> HandleRes =
    Pool.AcquireActorHandle<AMyProjectile>(AMyProjectile::StaticClass(), SpawnTransform);
if (HandleRes.IsSuccess()) {
    TPooledObjectHandle<AMyProjectile> Handle = HandleRes.GetValue();
    Handle->Fire();

    (void)Pool.ReleaseActor(Handle);
    check(!Handle.IsValid());
    check(Handle.Get() == nullptr);
}

// 2. RAII 作用域租约
{
    TGameStatusResult<TScopedPoolRental<AMyProjectile>> RentalRes =
        Pool.AcquireScopedActor<AMyProjectile>(AMyProjectile::StaticClass(), SpawnTransform);
    if (RentalRes.IsSuccess()) {
        TScopedPoolRental<AMyProjectile> Rental = RentalRes.ExtractValue();
        Rental->Fire();
    } // 离开作用域自动归还入池
}
```

---

## 3. `UObjectPoolSubsystem` 公共接口

```cpp
UCLASS()
class GAMEPOOL_API UObjectPoolSubsystem : public UWorldSubsystem {
    GENERATED_BODY()

public:
    // ==================== 单例获取与 GC 追踪 ====================
    [[nodiscard]] static UObjectPoolSubsystem& Get(const UObject* WorldContextObject);
    [[nodiscard]] static UObjectPoolSubsystem* GetPtr(const UObject* WorldContextObject);
    static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

    // ==================== Actor 预热与策略配置 ====================
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void Prewarm(TSubclassOf<AActor> ActorClass, int32 PrewarmCount);

    TGameCoroTask<int32> PrewarmAsync(
        TSubclassOf<AActor> ActorClass,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 1,
        FCancellationToken CancellationToken = FCancellationToken::None());

    TGameCoroTask<int32> PrewarmAsync(
        const FSoftObjectPath& ActorClassPath,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 1,
        FCancellationToken CancellationToken = FCancellationToken::None());

    TGameCoroTask<int32> PrewarmAsync(
        const TSoftClassPtr<AActor>& ActorSoftClass,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 1,
        FCancellationToken CancellationToken = FCancellationToken::None());

    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void SetMaxPoolCapacity(TSubclassOf<AActor> ActorClass, int32 InMaxIdleCount);

    void SetPoolResetFlags(TSubclassOf<AActor> ActorClass, EPoolResetFlags InResetFlags);
    [[nodiscard]] EPoolResetFlags GetPoolResetFlags(TSubclassOf<AActor> ActorClass) const;

    // ==================== Actor 借出与归还 ====================
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool", meta = (DisplayName = "Acquire Actor", DeterminesOutputType = "ActorClass"))
    AActor* K2_AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform);

    template <typename T = AActor>
    [[nodiscard]] TGameStatusResult<T*> AcquireActor(
        TSubclassOf<AActor> ActorClass = T::StaticClass(),
        const FTransform& SpawnTransform = FTransform::Identity);

    template <typename T = AActor, typename TPayload>
    [[nodiscard]] TGameStatusResult<T*> AcquireActorWithPayload(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform,
        const TPayload& Payload);

    template <typename T = AActor>
    [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireActorHandle(
        TSubclassOf<AActor> ActorClass = T::StaticClass(),
        const FTransform& SpawnTransform = FTransform::Identity);

    template <typename T = AActor, typename TPayload>
    [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireActorHandleWithPayload(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform,
        const TPayload& Payload);

    template <typename T = AActor>
    [[nodiscard]] TGameStatusResult<TScopedPoolRental<T>> AcquireScopedActor(
        TSubclassOf<AActor> ActorClass = T::StaticClass(),
        const FTransform& SpawnTransform = FTransform::Identity);

    template <typename T = AActor, typename TPayload>
    [[nodiscard]] TGameStatusResult<TScopedPoolRental<T>> AcquireScopedActorWithPayload(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform,
        const TPayload& Payload);

    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool", meta = (DisplayName = "Release Actor"))
    void K2_ReleaseActor(AActor* Actor);

    TGameStatusResult<void> ReleaseActor(AActor* Actor);

    template <typename T>
    TGameStatusResult<void> ReleaseActor(const TPooledObjectHandle<T>& Handle);

    TGameStatusResult<void> ReleaseActorDeferred(AActor* Actor);

    // ==================== 纯 UObject 预热、借出与归还 ====================
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void PrewarmObjectPool(TSubclassOf<UObject> ObjectClass, int32 PrewarmCount);

    TGameCoroTask<int32> PrewarmObjectPoolAsync(
        TSubclassOf<UObject> ObjectClass,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 4,
        FCancellationToken CancellationToken = FCancellationToken::None());

    TGameCoroTask<int32> PrewarmObjectPoolAsync(
        const FSoftObjectPath& ObjectClassPath,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 4,
        FCancellationToken CancellationToken = FCancellationToken::None());

    TGameCoroTask<int32> PrewarmObjectPoolAsync(
        const TSoftClassPtr<UObject>& ObjectSoftClass,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 4,
        FCancellationToken CancellationToken = FCancellationToken::None());

    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void SetMaxObjectPoolCapacity(TSubclassOf<UObject> ObjectClass, int32 InMaxIdleCount);

    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool", meta = (DisplayName = "Acquire Object", DeterminesOutputType = "ObjectClass"))
    UObject* K2_AcquireObject(TSubclassOf<UObject> ObjectClass);

    template <typename T = UObject>
    [[nodiscard]] TGameStatusResult<T*> AcquireObject(TSubclassOf<UObject> ObjectClass = T::StaticClass());

    template <typename T = UObject, typename TPayload>
    [[nodiscard]] TGameStatusResult<T*> AcquireObjectWithPayload(
        TSubclassOf<UObject> ObjectClass,
        const TPayload& Payload);

    template <typename T = UObject>
    [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireObjectHandle(
        TSubclassOf<UObject> ObjectClass = T::StaticClass());

    template <typename T = UObject, typename TPayload>
    [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireObjectHandleWithPayload(
        TSubclassOf<UObject> ObjectClass,
        const TPayload& Payload);

    template <typename T = UObject>
    [[nodiscard]] TGameStatusResult<TScopedPoolRental<T>> AcquireScopedObject(
        TSubclassOf<UObject> ObjectClass = T::StaticClass());

    template <typename T = UObject, typename TPayload>
    [[nodiscard]] TGameStatusResult<TScopedPoolRental<T>> AcquireScopedObjectWithPayload(
        TSubclassOf<UObject> ObjectClass,
        const TPayload& Payload);

    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool", meta = (DisplayName = "Release Object"))
    void K2_ReleaseObject(UObject* Object);

    TGameStatusResult<void> ReleaseObject(UObject* Object);

    template <typename T>
    TGameStatusResult<void> ReleaseObject(const TPooledObjectHandle<T>& Handle);

    TGameStatusResult<void> ReleaseObjectDeferred(UObject* Object);

    // ==================== 无锁延迟归还收割 ====================
    UFUNCTION(BlueprintCallable, Category = "GamePool|Deferred")
    int32 ProcessDeferredReleases(int32 MaxProcessCount = 64);

    UFUNCTION(BlueprintPure, Category = "GamePool|Deferred")
    [[nodiscard]] int32 GetPendingDeferredReleaseCount() const noexcept;

    // ==================== 弹性缩容与冷数据淘汰 (TTL) ====================
    UFUNCTION(BlueprintCallable, Category = "GamePool|Eviction")
    void SetActorPoolEvictionSettings(TSubclassOf<AActor> ActorClass, const FPoolEvictionSettings& Settings);

    UFUNCTION(BlueprintPure, Category = "GamePool|Eviction")
    [[nodiscard]] FPoolEvictionSettings GetActorPoolEvictionSettings(TSubclassOf<AActor> ActorClass) const;

    UFUNCTION(BlueprintCallable, Category = "GamePool|Eviction")
    void SetObjectPoolEvictionSettings(TSubclassOf<UObject> ObjectClass, const FPoolEvictionSettings& Settings);

    UFUNCTION(BlueprintPure, Category = "GamePool|Eviction")
    [[nodiscard]] FPoolEvictionSettings GetObjectPoolEvictionSettings(TSubclassOf<UObject> ObjectClass) const;

    UFUNCTION(BlueprintCallable, Category = "GamePool|Eviction")
    int32 ForceEvictExpired(int32 MaxEvictionsPerStep = 32, double CustomNowSeconds = 0.0);

    // ==================== 遥测监控与容灾泄洪 ====================
    UFUNCTION(BlueprintPure, Category = "GamePool|Telemetry")
    [[nodiscard]] FPoolTelemetrySnapshot GetTelemetrySnapshot() const;

    UFUNCTION(BlueprintCallable, Category = "GamePool|Telemetry")
    void ResetTelemetry();

    UFUNCTION(BlueprintCallable, Category = "GamePool|Resilience")
    int32 FlushAllIdleInstances(bool bPreserveMinCount = false);

    UFUNCTION(BlueprintCallable, Category = "GamePool|Resilience")
    void SetLowMemoryAutoFlushEnabled(bool bEnable);

    UFUNCTION(BlueprintPure, Category = "GamePool|Resilience")
    [[nodiscard]] bool IsLowMemoryAutoFlushEnabled() const noexcept;

    // ==================== 统计与清空 ====================
    UFUNCTION(BlueprintPure, Category = "GamePool|ObjectPool")
    [[nodiscard]] FPoolStats GetPoolStats(TSubclassOf<AActor> ActorClass) const;

    UFUNCTION(BlueprintPure, Category = "GamePool|ObjectPool")
    [[nodiscard]] FPoolStats GetObjectPoolStats(TSubclassOf<UObject> ObjectClass) const;

    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearPool(TSubclassOf<AActor> ActorClass);

    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearAllActorPools();

    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearObjectPool(TSubclassOf<UObject> ObjectClass);

    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearAllObjectPools();

    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearAllPools();
};
```

---

## 4. 错误码契约 (`Game::PoolErrorCodes`)

全部错误均绑定 `Game::ModuleId::Pool`（定义于 `Public/Core/PoolErrorCodes.h`）：

| 错误码 | 名称 | 说明 |
|:---:|---|---|
| `5001` | `InvalidClass` | 类型为空、抽象类，或向纯 UObject 池传入 `AActor` 派生类 |
| `5002` | `TypeMismatch` | 传入类与强类型模板参数 `T` 不兼容 |
| `5003` | `InvalidContext` | `World` / `Outer` / `UObjectPoolSubsystem` 未初始化或已销毁 |
| `5004` | `SpawnFailed` | `SpawnActor` 或 `NewObject` 返回空指针 |
| `5005` | `InvalidatedDuringAcquire` | 实例在借出回调（`OnAcquireFromPool` / `OnAcquireFromPoolWithPayload`）中自归还或自销毁 |
| `5006` | `InvalidInstance` | 待归还实例为空或已标记垃圾回收 |
| `5007` | `NotOwnedActiveInstance` | 实例不属于当前对象池的活跃借出成员 |
| `5008` | `InvalidHandle` | 安全句柄无效、世代过期或归属其他子系统实例 |
| `5009` | `CapacityLimitReached` | 空闲池已达 `MaxIdleCount` 上限 |
| `5010` | `DeferredQueueFull` | 跨线程无锁延迟归还环形缓冲（1024 槽位）已满或未初始化 |

---

## 5. 弹性淘汰配置 (`FPoolEvictionSettings`) 与重置掩码 (`EPoolResetFlags`)

```cpp
USTRUCT(BlueprintType)
struct GAMEPOOL_API FPoolEvictionSettings {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction")
    bool bEnableEviction = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction", meta = (ClampMin = "0.1"))
    float IdleTimeToLiveSeconds = 60.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction", meta = (ClampMin = "0"))
    int32 MinPreservedCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction", meta = (ClampMin = "0.1"))
    float CheckIntervalSeconds = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction", meta = (ClampMin = "1"))
    int32 MaxEvictionsPerStep = 2;
};
```

`EPoolResetFlags`（`uint16` 位掩码，默认 `EPoolResetFlags::All`）：
- `None`：跳过全部组件自动重置
- `Timers`：清理挂接在 Actor 上的全部 World 定时器（`ClearAllTimersForObject`）
- `Physics`：仅对 `IsSimulatingPhysics()` 的 `UPrimitiveComponent` 清零线/角速度
- `Movement`：停止 `UMovementComponent` 惯性速度（`StopMovementImmediately()`）
- `Audio`：单次遍历直接停止活跃 `UAudioComponent`（零中间数组堆分配）
- `FXSystem`：单次遍历直接对 `UFXSystemComponent` 调用 `DeactivateImmediate()`（覆盖 Niagara 与 Cascade）
- `Animation`：仅在存在有效 `SkeletalMesh` 或 `AnimInstance` 时重置 `USkeletalMeshComponent`（`Stop()` + `ClearAnimScriptInstance()` + `InitAnim(true)`）
- `PawnAndAI`：若为 `APawn` 且绑定 `Controller`，执行 `Controller->UnPossess()`

---

## 6. 运行期可观测性 (`stat GamePool`、Unreal Insights 与 `FPoolTelemetrySnapshot`)

### 6.1 引擎 Stats 组 (`stat GamePool`)
| 统计指标 | 类型 | 含义说明 |
|---|---|---|
| `Acquire Actor Total` | Cycle Stat | 借出 Actor 总耗时 |
| `Release Actor Total` | Cycle Stat | 归还 Actor 总耗时 |
| `Acquire UObject Total` | Cycle Stat | 借出纯 UObject 总耗时 |
| `Release UObject Total` | Cycle Stat | 归还纯 UObject 总耗时 |
| `Component Deep Reset` | Cycle Stat | 1-Pass 组件深层重置耗时 |
| `Prewarm Step Time` | Cycle Stat | 协程分帧预热单步耗时 |
| `Eviction Step Time` | Cycle Stat | TTL 冷实例平滑淘汰单步耗时 |
| `Active Actors` / `Idle Actors` | Counter | 当前活跃与空闲 Actor 总数（`Deinitialize` 自动清零平衡） |
| `Active UObjects` / `Idle UObjects` | Counter | 当前活跃与空闲 UObject 总数（`Deinitialize` 自动清零平衡） |
| `Pool Cache Hits` / `Pool Cache Misses` | Counter | 借出复用命中与穿透新生成计数 |

### 6.2 Unreal Insights CPU Trace 作用域与统一日志
- `GamePool_AcquireActor` / `GamePool_ReleaseActor`
- `GamePool_AcquireObject` / `GamePool_ReleaseObject`
- `GamePool_ResetActorComponents`
- `GamePool_PrewarmStep` / `GamePool_PrewarmObjectStep`
- `GamePool_EvictActors` / `GamePool_EvictObjects`
- `GamePool_ProcessDeferredReleases`
- 模块日志统一通过 `GameLogging` 插件注册分类 `LogGamePool` 并经 `GAME_LOG` 输出（Shipping 下编译期自动零开销剥离）。

### 6.3 遥测快照 (`FPoolTelemetrySnapshot`)
包含 `TotalAcquires`、`CacheHits`、`CacheMisses`、`TotalActiveCount`、`TotalIdleCount` 与 `GetHitRatePercentage()`。仅在成功实例化后递增统计（`SpawnActor` / `NewObject` 失败不污染命中率）。

---

## 7. 原生 C++ 结构体内存池 (`TStructPool<T, BlocksPerChunk>` & `TScopedStructRental<T, BlocksPerChunk>`)

基于 `GameCore::TFixedSizePool<T, BlocksPerChunk>` 实现（头文件 `Memory/TStructPool.h` 与 `Memory/TScopedStructRental.h`）：
- `Acquire(TArgs&&... Args)`：分配定长内存块并原地构造 `T`
- `AcquireScoped(TArgs&&... Args)`：返回移动语义 RAII 租约 `TScopedStructRental<T, BlocksPerChunk>`
- `Release(T* Instance)`：原地析构并归还至自由链表（允许安全传入 `nullptr`）
- `ReserveChunks(size_t TargetChunkCount)`：预分配整块连续内存
- `Clear()` / `IsEmpty()`：清空底层 Chunk 与判空检查
- `GetAllocatedCount()` / `GetFreeCount()` / `GetChunkCount()` / `GetTotalCapacity()`：实时活跃、空闲、Chunk 数与总槽位统计
- `TScopedStructRental<T, BlocksPerChunk>`：支持 `IsValid()`、`operator bool()`、`Get()`、`operator->()`、`operator*()`、`EarlyRelease()` 与 `[[nodiscard]] T* ReleaseAndDetach()`

> **生命周期契约**：在所有借出的指针及 `TScopedStructRental` 归还或析构前，`TStructPool` 宿主不得移动或销毁；`TStructPool` 自身支持移动构造与移动赋值，但移动时要求 `IsEmpty() == true`（`GetAllocatedCount() == 0`）。
