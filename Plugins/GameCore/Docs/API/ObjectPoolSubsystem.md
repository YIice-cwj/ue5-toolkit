# GameCore 通用对象池 API 参考

`UObjectPoolSubsystem` 是一个世界子系统（`UWorldSubsystem`），负责 `AActor` 与非 Actor 纯 `UObject` 的复用、休眠、唤醒、分帧预热与生命周期管理，旨在消除高频生成/销毁对象导致的 GC 顿卡与主线程尖峰。

头文件：
- `#include "Pool/ObjectPoolSubsystem.h"`
- `#include "Pool/ObjectPoolTypes.h"`
- `#include "Pool/IGamePoolable.h"`

---

## 1. 契约接口 `IGamePoolable`

需要接入对象池生命周期回调的 `AActor` 或纯 `UObject` 派生类均可继承并实现此接口：

```cpp
UINTERFACE(BlueprintType, Blueprintable)
class GAMECORE_API UGamePoolable : public UInterface {
    GENERATED_BODY()
};

class GAMECORE_API IGamePoolable {
    GENERATED_BODY()

public:
    /**
     * @brief 从对象池借出并激活时的生命周期回调（重置状态、刷新生命周期）
     */
    UFUNCTION(BlueprintNativeEvent, Category = "GameCore|Pool")
    void OnAcquireFromPool();
    virtual void OnAcquireFromPool_Implementation() {}

    /**
     * @brief 归还至对象池休眠时的生命周期回调（清理引用、重置特效/声音）
     */
    UFUNCTION(BlueprintNativeEvent, Category = "GameCore|Pool")
    void OnReleaseToPool();
    virtual void OnReleaseToPool_Implementation() {}
};
```

---

## 2. `UObjectPoolSubsystem` 类方法

```cpp
UCLASS()
class GAMECORE_API UObjectPoolSubsystem : public UWorldSubsystem {
    GENERATED_BODY()

public:
    static UObjectPoolSubsystem& Get(const UObject* WorldContextObject);
    static UObjectPoolSubsystem* GetPtr(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void Prewarm(TSubclassOf<AActor> ActorClass, int32 PrewarmCount);

    FTimeSlicedTaskHandle PrewarmTimeSliced(
        TSubclassOf<AActor> ActorClass,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 1);

    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void SetMaxPoolCapacity(TSubclassOf<AActor> ActorClass, int32 InMaxIdleCount);

    void SetPoolResetFlags(TSubclassOf<AActor> ActorClass, EPoolResetFlags InResetFlags);
    [[nodiscard]] EPoolResetFlags GetPoolResetFlags(TSubclassOf<AActor> ActorClass) const;

    template <typename T = AActor>
    [[nodiscard]] T* AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform = FTransform::Identity);

    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool", meta = (DeterminesOutputType = "ActorClass"))
    AActor* AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform);

    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ReleaseActor(AActor* Actor);

    UFUNCTION(BlueprintPure, Category = "GameCore|Pool")
    void GetPoolStats(TSubclassOf<AActor> ActorClass, int32& OutTotalCount, int32& OutActiveCount, int32& OutIdleCount) const;

    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ClearPool(TSubclassOf<AActor> ActorClass);

    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ClearAllPools();

    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void PrewarmObjectPool(TSubclassOf<UObject> ObjectClass, int32 PrewarmCount);

    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void SetMaxObjectPoolCapacity(TSubclassOf<UObject> ObjectClass, int32 InMaxIdleCount);

    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool", meta = (DeterminesOutputType = "ObjectClass"))
    UObject* AcquireObject(TSubclassOf<UObject> ObjectClass);

    template <typename T>
    [[nodiscard]] T* AcquireObject(TSubclassOf<UObject> ObjectClass = T::StaticClass());

    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ReleaseObject(UObject* Object);

    UFUNCTION(BlueprintPure, Category = "GameCore|Pool")
    [[nodiscard]] FPoolStats GetObjectPoolStats(TSubclassOf<UObject> ObjectClass) const;

    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ClearObjectPool(TSubclassOf<UObject> ObjectClass);

    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ClearAllObjectPools();
};
```

---

## 3. 核心行为细节与安全保障

1. **按位可裁剪自动状态重置 (`EPoolResetFlags`)**：
   - `ReleaseActor` 时，子系统先解绑 `OnDestroyed`、调用 `IGamePoolable::OnReleaseToPool`、解除附加并清理 `UTimeDilationSubsystem` 活跃顿帧；
   - 随后按该类配置的 `EPoolResetFlags`（默认 `EPoolResetFlags::All`）选择性重置：
     - `Timers`：清理挂接在 Actor 上的全部定时器（`ClearAllTimersForObject`）；
     - `Physics`：清零根组件物理线速度与角速度；
     - `Movement`：停止移动组件惯性速度（`UMovementComponent::StopMovementImmediately`）；
     - `Audio`：经栈内联数组 `TInlineComponentArray<UAudioComponent*>` 零堆分配停止播放中的音效；
     - `FXSystem`：经 `TInlineComponentArray<UFXSystemComponent*>` 统一调用 `DeactivateImmediate()`（同时覆盖 `UNiagaraComponent` 与 `UParticleSystemComponent`，保持 `GameCore` 零 `Niagara` 插件依赖）；
     - 对高频纯变换子弹可配置 `EPoolResetFlags::None` 直接跳过组件遍历；
   - 最后关闭渲染、碰撞与 Tick 进入池内休眠。
2. **分帧渐进预热 (`PrewarmTimeSliced`)**：
   - 自动委托 `UTimeSlicedTaskSubsystem` 在每帧毫秒预算内按批生成空闲实例，消除进入战斗关卡时的同步预热卡顿，且在触达 `MaxIdleCount` 上限或返回句柄调用 `Cancel()` 时立即停机。
3. **非 Actor 纯 `UObject` 双轨池化**：
   - 面向高频 Buff 实例、技能上下文、伤害计算包等纯 `UObject` 对象提供 `AcquireObject<T>()` / `ReleaseObject()`，内部通过 `UPROPERTY()` 强引用桶防止 GC 回收闲置对象，并在边界自动拒绝抽象类与 `AActor` 派生类误用。
4. **O(1) 归属校验、重入防护与外部销毁自愈**：
   - 维护活跃集合与空闲集合防止外部非池属实例污染；若 `OnAcquireFromPool` 回调内部立即自归还或自销毁，借出接口安全返回 `nullptr` 杜绝双重计数；借出 Actor 监听 `OnDestroyed` 实现外部销毁自动扣减活跃数。
