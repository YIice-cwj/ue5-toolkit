# GameCore 通用对象池 API 参考

`UObjectPoolSubsystem` 是一个世界子系统（`UWorldSubsystem`），负责 Actor 的复用、休眠、唤醒与生命周期管理，旨在消除高频生成/销毁 Actor 导致的 GC 顿卡。

头文件：
- `#include "Pool/ObjectPoolSubsystem.h"`
- `#include "Pool/IGamePoolable.h"`

---

## 1. 契约接口 `IGamePoolable`

需要接入对象池生命周期回调的 Actor 需继承并实现此接口：

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

    /**
     * @brief 预热指定类别的对象池（提前批量生成进入空闲队列）
     * @param ActorClass 目标 Actor 类
     * @param PrewarmCount 预热生成的空闲实例数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void Prewarm(TSubclassOf<AActor> ActorClass, int32 PrewarmCount);

    /**
     * @brief 设置指定类别的最大空闲实例缓存容量（防长期闲置内存泄漏）
     * @param ActorClass 目标 Actor 类
     * @param InMaxIdleCount 最大空闲数（<=0 表示无限制）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void SetMaxPoolCapacity(TSubclassOf<AActor> ActorClass, int32 InMaxIdleCount);

    /**
     * @brief 从池中借出或生成 Actor 实例（C++ 模板版本，自动 Cast）
     * @tparam T 返回的 Actor 具体派生类型
     * @param ActorClass 目标类
     * @param SpawnTransform 初始空间变换
     * @return 激活的 Actor 实例指针
     */
    template <typename T = AActor>
    [[nodiscard]] T* AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform = FTransform::Identity);

    /**
     * @brief 蓝图借出接口
     * @param ActorClass 目标类
     * @param SpawnTransform 初始空间变换
     * @return 激活的 Actor 实例指针
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool", meta = (DeterminesOutputType = "ActorClass"))
    AActor* AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform);

    /**
     * @brief 将已借出的 Actor 归还回池休眠
     * @param Actor 待归还的实例
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ReleaseActor(AActor* Actor);

    /**
     * @brief 查询指定类别的池状态统计
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Pool")
    void GetPoolStats(TSubclassOf<AActor> ActorClass, int32& OutTotalCount, int32& OutActiveCount, int32& OutIdleCount) const;

    /**
     * @brief 释放指定类别的全部闲置对象
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ClearPool(TSubclassOf<AActor> ActorClass);

    /**
     * @brief 释放所有类别的对象池
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ClearAllPools();
};
```

---

## 3. 核心行为细节与安全保障

1. **自动状态深度重置**：
   - `ReleaseActor` 时，子系统自动对 Actor 执行：
     - 先解绑 `OnDestroyed` 委托（避免正常归还被外部销毁逻辑误扣活跃计数）；
     - 调用 `IGamePoolable::OnReleaseToPool` 接口通知；
     - 解除层级附加关系（`DetachFromActor`）；
     - 清理该 Actor 在 `UTimeDilationSubsystem` 上的活跃顿帧（`ClearHitStop`）；
     - 清理挂接在 Actor 身上的全部定时器（`ClearAllTimersForObject`）；
     - 清零根组件物理线速度与角速度；
     - 停止移动组件惯性速度（`UMovementComponent::StopMovementImmediately`）；
     - 停止播放中的音频组件与发射中的粒子系统；
     - 关闭渲染（`SetActorHiddenInGame(true)`）、物理碰撞（`SetActorEnableCollision(false)`）与 Tick（`SetActorTickEnabled(false)`）。
   - `AcquireActor` 时，子系统先更新其 `ActorTransform`，唤醒渲染、物理与 Tick，并自动触发 `IGamePoolable::OnAcquireFromPool`。
   - `Prewarm` 对空 `ActorClass` 或 `PrewarmCount <= 0` 直接返回，不做任何生成。
2. **O(1) 快速重复归还拦截**：
   - 每个池存储桶同时维护 `TArray` 与 `TSet`，归还时以 $O(1)$ 速度判定是否已在闲置列表中，避免高频遍历耗时。
3. **容量保护**：
   - 当池内闲置对象数量达到 `MaxIdleCount` 时，继续归还的对象将直接被销毁（调用 `AActor::Destroy`），防止内存无限膨胀。
4. **外部意外销毁自愈**：
   - 借出实例时自动监听 `OnDestroyed`，即便外界绕过 `ReleaseActor` 直接调用 `DestroyActor`，对象池也会自动同步扣减活跃计数，确保统计恒定准确。
5. **安全清空闲置对象（Idle-Only 清理）**：
   - `ClearPool` 与 `ClearAllPools` 仅销毁缓存中的空闲实例（`IdleActors`），绝不清零借出计数（`ActiveCount`）或擦除类型映射表；
   - 外部仍在使用的借出实例不受影响：稍后正常归还时会重新进入空闲队列（若已超容量则按容量规则销毁），不会出现下溢或反注册崩溃。
