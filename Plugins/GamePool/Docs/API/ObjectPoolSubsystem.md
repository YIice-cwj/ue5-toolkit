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

## 4. 原生 C++ 结构体与高频数据内存池 `TStructPool<T>`

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

## 5. 核心行为细节与安全保障

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
