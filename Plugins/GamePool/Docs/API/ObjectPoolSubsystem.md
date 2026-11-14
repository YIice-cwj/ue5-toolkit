# GamePool 通用对象与内存池 API 参考

`UObjectPoolSubsystem` 是一个世界子系统（`UWorldSubsystem`），负责 `AActor`、非 Actor 纯 `UObject` 的复用、休眠、唤醒、分帧预热与生命周期管理，并配合 `TStructPool<T>` 覆盖非 UObject 纯 C++ 结构体的高性能内存池化，旨在彻底消除高频生成/销毁对象导致的 GC 顿卡、堆碎片与主线程尖峰。

头文件：
- `#include "ObjectPool/ObjectPoolSubsystem.h"`
- `#include "ObjectPool/ObjectPoolTypes.h"`
- `#include "ObjectPool/PooledObjectHandle.h"`
- `#include "ObjectPool/IGamePoolable.h"`
- `#include "MemoryPool/StructPool.h"`

---

## 1. 契约接口 `IGamePoolable`

需要接入对象池生命周期回调的 `AActor` 或纯 `UObject` 派生类均可继承并实现此接口：

```cpp
UINTERFACE(BlueprintType, Blueprintable)
class GAMEPOOL_API UGamePoolable : public UInterface {
    GENERATED_BODY()
};

class GAMEPOOL_API IGamePoolable {
    GENERATED_BODY()

public:
    /**
     * @brief 从对象池借出并激活时的生命周期回调（重置状态、刷新生命周期）
     */
    UFUNCTION(BlueprintNativeEvent, Category = "GamePool|ObjectPool")
    void OnAcquireFromPool();
    virtual void OnAcquireFromPool_Implementation() {}

    /**
     * @brief 从对象池借出并携带业务载荷时调用（纯 C++ 零堆分配接口，默认转发至无参虚方法）
     * @param InPayload 业务载荷不可变指针
     */
    virtual void OnAcquireFromPoolWithPayload(const void* InPayload) {
        (void)InPayload;
        OnAcquireFromPool_Implementation();
    }

    /**
     * @brief 归还至对象池休眠时的生命周期回调（清理引用、重置特效/声音）
     */
    UFUNCTION(BlueprintNativeEvent, Category = "GamePool|ObjectPool")
    void OnReleaseToPool();
    virtual void OnReleaseToPool_Implementation() {}
};
```

---

## 2. 安全代数句柄与 RAII 作用域租约

为彻底根治外部异步闭包、定时器与网络回调在归还后继续持有裸指针导致的 **Use-After-Return** 与野指针窜号覆写灾难，系统提供安全受管句柄与 RAII 租约：

```cpp
// 1. 安全代数句柄：内部记录当前借出轮次的 Generation
TGameStatusResult<TPooledObjectHandle<AMyProjectile>> HandleRes =
    Pool.AcquireActorHandle<AMyProjectile>(AMyProjectile::StaticClass(), SpawnTransform);
if (HandleRes.IsSuccess()) {
    TPooledObjectHandle<AMyProjectile> Handle = HandleRes.GetValue();
    // 归还前安全访问
    Handle->Fire();
    
    Pool.ReleaseActor(Handle);
    // 归还后自动失效，彻底杜绝悬空覆写
    check(!Handle.IsValid());
    check(Handle.Get() == nullptr);
}

// 2. RAII 作用域租约：离开大括号作用域自动归还回池
{
    TGameStatusResult<TScopedPoolRental<AMyProjectile>> RentalRes =
        Pool.AcquireScopedActor<AMyProjectile>(AMyProjectile::StaticClass(), SpawnTransform);
    if (RentalRes.IsSuccess()) {
        TScopedPoolRental<AMyProjectile> Rental = RentalRes.ExtractValue();
        Rental->Fire();
    } // 离开大括号，析构函数自动安全归还入池！
}
```

---

## 3. `UObjectPoolSubsystem` 类方法

```cpp
UCLASS()
class GAMEPOOL_API UObjectPoolSubsystem : public UWorldSubsystem {
    GENERATED_BODY()

public:
    static UObjectPoolSubsystem& Get(const UObject* WorldContextObject);
    static UObjectPoolSubsystem* GetPtr(const UObject* WorldContextObject);

    // ==================== 预热基础设施 ====================
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

    // ==================== 容量与策略配置 ====================
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void SetMaxPoolCapacity(TSubclassOf<AActor> ActorClass, int32 InMaxIdleCount);

    void SetPoolResetFlags(TSubclassOf<AActor> ActorClass, EPoolResetFlags InResetFlags);
    [[nodiscard]] EPoolResetFlags GetPoolResetFlags(TSubclassOf<AActor> ActorClass) const;

    // ==================== Actor 借还 ====================
    template <typename T = AActor>
    [[nodiscard]] TGameStatusResult<T*> AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform = FTransform::Identity);

    template <typename T = AActor, typename TPayload>
    [[nodiscard]] TGameStatusResult<T*> AcquireActorWithPayload(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform,
        const TPayload& Payload);

    template <typename T = AActor>
    [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireActorHandle(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform = FTransform::Identity);

    template <typename T = AActor>
    [[nodiscard]] TGameStatusResult<TScopedPoolRental<T>> AcquireScopedActor(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform = FTransform::Identity);

    TGameStatusResult<void> ReleaseActor(AActor* Actor);

    template <typename T>
    TGameStatusResult<void> ReleaseActor(const TPooledObjectHandle<T>& Handle);

    // ==================== 跨线程无锁延迟回收 ====================
    bool ReleaseActorDeferred(AActor* Actor);
    int32 ProcessDeferredReleases(int32 MaxProcessCount = 64);

    // ==================== 纯 UObject 池化 ====================
    void PrewarmObjectPool(TSubclassOf<UObject> ObjectClass, int32 PrewarmCount);
    void SetMaxObjectPoolCapacity(TSubclassOf<UObject> ObjectClass, int32 InMaxIdleCount);

    template <typename T = UObject>
    [[nodiscard]] TGameStatusResult<T*> AcquireObject(TSubclassOf<UObject> ObjectClass = T::StaticClass());

    template <typename T = UObject>
    [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireObjectHandle(TSubclassOf<UObject> ObjectClass = T::StaticClass());

    template <typename T = UObject>
    [[nodiscard]] TGameStatusResult<TScopedPoolRental<T>> AcquireScopedObject(TSubclassOf<UObject> ObjectClass = T::StaticClass());

    TGameStatusResult<void> ReleaseObject(UObject* Object);

    template <typename T>
    TGameStatusResult<void> ReleaseObject(const TPooledObjectHandle<T>& Handle);

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

    // ==================== 统计与清空 ====================
    UFUNCTION(BlueprintPure, Category = "GamePool|ObjectPool")
    [[nodiscard]] FPoolStats GetPoolStats(TSubclassOf<AActor> ActorClass) const;

    UFUNCTION(BlueprintPure, Category = "GamePool|ObjectPool")
    [[nodiscard]] FPoolStats GetObjectPoolStats(TSubclassOf<UObject> ObjectClass) const;

    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearPool(TSubclassOf<AActor> ActorClass);

    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearObjectPool(TSubclassOf<UObject> ObjectClass);

    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearAllPools();
};
```

---

## 4. 弹性缩容与冷数据淘汰数据结构 (`FPoolEvictionSettings`)

```cpp
USTRUCT(BlueprintType)
struct GAMEPOOL_API FPoolEvictionSettings {
    GENERATED_BODY()

    /** 是否启用闲置超时自动淘汰（默认关闭） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction")
    bool bEnableEviction = false;

    /** 实例闲置生存期（秒）。超过此时间未被借出的实例将被平滑淘汰（默认 60.0s） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction", meta = (ClampMin = "0.1"))
    float IdleTimeToLiveSeconds = 60.0f;

    /** 保底保留的最小空闲实例数（防止全部淘汰导致下次借出时冷启动，默认 0） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction", meta = (ClampMin = "0"))
    int32 MinPreservedCount = 0;

    /** 自动检测与平滑淘汰心跳周期（秒，默认 5.0s） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction", meta = (ClampMin = "0.1"))
    float CheckIntervalSeconds = 5.0f;

    /** 单步平滑销毁最大实例配额（默认每步销毁 2 个，防止单步 GC 尖刺） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction", meta = (ClampMin = "1"))
    int32 MaxEvictionsPerStep = 2;
};
```

---

## 5. 运行期可观测性与遥测监控 (`FPoolTelemetrySnapshot` & `stat GamePool`)

### 5.1 控制台命令与 UE Stats 监控组
在编辑器或非 Shipping 构建控制台中输入：
```bash
stat GamePool
```
可在屏幕上实时显示 GamePool 的运行期耗时（Cycle Stats）与实例数量/命中统计（Counter Stats）：

| 统计指标 | 类型 | 含义说明 |
|---|---|---|
| `Acquire Actor Total` | Cycle Stat | 借出 Actor 实例耗时（含复用唤醒与新生成） |
| `Release Actor Total` | Cycle Stat | 归还 Actor 实例耗时 |
| `Acquire UObject Total` | Cycle Stat | 借出纯 UObject 实例耗时 |
| `Release UObject Total` | Cycle Stat | 归还纯 UObject 实例耗时 |
| `Component Deep Reset` | Cycle Stat | 归还时执行的组件自动深层重置耗时 |
| `Prewarm Step Time` | Cycle Stat | 协程流式分帧预热单步耗时 |
| `Eviction Step Time` | Cycle Stat | TTL 冷实例平滑淘汰单步耗时 |
| `Active Actors` | Counter | 当前处于借出激活态的 Actor 实例总数 |
| `Idle Actors` | Counter | 当前处于池内休眠待复用的 Actor 实例总数 |
| `Active UObjects` | Counter | 当前处于借出激活态的 UObject 实例总数 |
| `Idle UObjects` | Counter | 当前处于池内休眠待复用的 UObject 实例总数 |
| `Pool Cache Hits` | Counter | 借出复用命中次数（直接复用池中闲置对象） |
| `Pool Cache Misses` | Counter | 借出穿透新生成次数（池空触发动态实例化） |

> **零开销设计**：所有 `STATGROUP_GamePool` 与 `TRACE_CPUPROFILER_EVENT_SCOPE` 宏在 Shipping 构建下均被引擎自动编译剔除，达成 0 额外 CPU/内存开销。

### 5.2 运行时遥测数据结构 (`FPoolTelemetrySnapshot`)
```cpp
USTRUCT(BlueprintType)
struct GAMEPOOL_API FPoolTelemetrySnapshot {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "GamePool|Telemetry")
    int64 TotalAcquires = 0;      // 累计借出请求总次数

    UPROPERTY(BlueprintReadOnly, Category = "GamePool|Telemetry")
    int64 CacheHits = 0;          // 累计命中复用次数

    UPROPERTY(BlueprintReadOnly, Category = "GamePool|Telemetry")
    int64 CacheMisses = 0;        // 累计穿透新生成次数

    UPROPERTY(BlueprintReadOnly, Category = "GamePool|Telemetry")
    int32 TotalActiveCount = 0;   // 当前全池活跃实例总数

    UPROPERTY(BlueprintReadOnly, Category = "GamePool|Telemetry")
    int32 TotalIdleCount = 0;     // 当前全池空闲实例总数

    /** 计算缓存命中率（百分比 0.0 ~ 100.0%） */
    [[nodiscard]] float GetHitRatePercentage() const noexcept;
};
```

### 5.3 获取与重置遥测快照接口
```cpp
// C++ 获取遥测快照
FPoolTelemetrySnapshot Snapshot = PoolSubsystem->GetTelemetrySnapshot();
float HitRate = Snapshot.GetHitRatePercentage();
UE_LOG(LogTemp, Log, TEXT("Pool Hit Rate: %.2f%%, Active: %d, Idle: %d"),
    HitRate, Snapshot.TotalActiveCount, Snapshot.TotalIdleCount);

// 重置计数器（测试基准或关卡切换时）
PoolSubsystem->ResetTelemetry();
```

---

## 6. 原生 C++ 结构体与高频数据内存池 `TStructPool<T>`

针对非 UObject 的高频事件、网络解包结构、几何射线查询结果等，采用基于 GameCore `TFixedBlockAllocator` 构建的泛型结构体池：

```cpp
struct FCombatEvent {
    int32 SourceId = 0;
    int32 TargetId = 0;
    float Damage = 0.0f;
};

// 构造单块 64 实例的大块内存池
TStructPool<FCombatEvent, 64> EventPool;

// 1. 原地构造借出
FCombatEvent* Evt = EventPool.Acquire();
Evt->Damage = 99.0f;
EventPool.Release(Evt); // 析构并纳秒级回收至空闲链表

// 2. RAII 作用域租约
{
    TScopedStructRental<FCombatEvent, 64> ScopedEvt = EventPool.AcquireScoped();
    ScopedEvt->Damage = 50.0f;
} // 自动析构并归还回池
```

---

## 7. 核心行为细节与安全保障

1. **按位可裁剪深层状态重置 (`EPoolResetFlags`)**：
   - `ReleaseActor` 时支持以下掩码位：
     - `Timers`：清理挂接在 Actor 上的全部定时器（`ClearAllTimersForObject`）；
     - `Physics`：清零根组件物理线速度与角速度；
     - `Movement`：停止移动组件惯性速度（`UMovementComponent::StopMovementImmediately`）；
     - `Audio`：经栈内联数组零堆分配停止播放中的音效；
     - `FXSystem`：统一调用 `DeactivateImmediate()`（覆盖 `UNiagaraComponent` 与 `UParticleSystemComponent`）；
     - `Animation`：重置所有骨骼网格组件，调用 `Stop()`、`ClearAnimScriptInstance()` 与 `InitAnim(true)`，杜绝借出首帧 T-Pose 闪烁；
     - `PawnAndAI`：对 APawn 实例自动执行 `Controller->UnPossess()`，解除控制附身。
2. **软引用流式分级预热 (`PrewarmAsync`)**：
   - 支持传入 `FSoftObjectPath`，通过 C++20 协程在后台发起异步流式加载，结合时间分片平滑分批实例化，并接入 `FCancellationToken`，在大世界跨区域或玩家撤离时安全提前熔断停机。
3. **多线程并发延迟回收 (`ReleaseActorDeferred`)**：
   - 工作线程（物理模拟、TaskGraph、网络解包）可无锁压入待归还 Actor 至 `TLockFreeRingBuffer`，主线程在帧首统一调用 `ProcessDeferredReleases()` 批量收割并安全完成组件重置。
4. **世代版本代数校验（Generational Handle Protection）**：
   - 每次借出生成唯一 Generation。当对象归还或池销毁时 Generation 递增。历史句柄若尝试访问已复用给其他逻辑的实例，自动判定为失效并返回 `nullptr`，彻底免疫野指针覆写。
