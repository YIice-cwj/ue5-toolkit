# GameGameplay GAS 引导组件 API 参考

`UGameAbilitySystemBootstrap` 作为一个 ActorComponent，将复杂的网络握手时序收敛在框架内部，使业务角色的初始化代码保持极致精简。

头文件：`#include "AbilitySystem/UGameAbilitySystemBootstrap.h"`  
基类：`UActorComponent`

---

## 1. 委托声明

```cpp
/**
 * @brief 解析并返回宿主 ASC 指针的委托（游戏层注入）
 */
DECLARE_DELEGATE_RetVal(UAbilitySystemComponent*, FResolveAbilitySystemComponent);

/**
 * @brief 注册属性集并写入初始数值的委托（仅权威端执行）
 */
DECLARE_DELEGATE_OneParam(FWriteInitialAttributes, UAbilitySystemComponent*);

/**
 * @brief 查询静态配置数据是否已就绪的委托（未注入默认视为恒就绪）
 */
DECLARE_DELEGATE_RetVal(bool, FQueryDataReady);
```

---

## 2. 组件类声明

```cpp
UCLASS(ClassGroup = (Game), meta = (BlueprintSpawnableComponent))
class GAMEGAMEPLAY_API UGameAbilitySystemBootstrap : public UActorComponent {
    GENERATED_BODY()

public:
    UGameAbilitySystemBootstrap();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // --- 委托注入方法 ---
    void SetResolveAbilitySystemComponentDelegate(const FResolveAbilitySystemComponent& InDelegate);
    void SetWriteInitialAttributesDelegate(const FWriteInitialAttributes& InDelegate);
    void SetDataReadyQueryDelegate(const FQueryDataReady& InDelegate);

    /**
     * @brief 统一幂等初始化入口（服务器与本地客户端均可调用，宿主握手钩子驱动）
     * @param InAvatar 当前附身的 Avatar（作为 ASC 的 AvatarActor）
     */
    void EnsureInitialized(AActor* InAvatar);

    /**
     * @brief 数据就绪通知（游戏层订阅静态数据版本后回调，唤醒数据未就绪时挂起的引导）
     */
    void NotifyDataReady();

    /**
     * @brief 兜底自愈入口（仅极端离线 / 重连脱节时触发，宿主 BeginPlay 自动调用）
     */
    void TryInitializeFromOwner();

    /**
     * @brief 查询状态接口
     */
    [[nodiscard]] bool IsInitialAttributesWritten() const noexcept;
    [[nodiscard]] bool IsDefaultAbilitiesGranted() const noexcept;

protected:
    /** 初始默认授予的能力类列表（权威端自动授予） */
    UPROPERTY(EditDefaultsOnly, Category = "Game|GAS")
    TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

    /** 复制就绪重试上限（默认 8 帧） */
    UPROPERTY(EditDefaultsOnly, Category = "Game|GAS", meta = (ClampMin = "0"))
    int32 MaxReplicationRetries = 8;

    /** 兜底握手重试上限（默认 60 帧） */
    UPROPERTY(EditDefaultsOnly, Category = "Game|GAS", meta = (ClampMin = "0"))
    int32 MaxInitRetries = 60;
};
```

---

## 3. 核心机制

1. **时序与幂等门**：
   - 内部缓存弱指针 `CurrentAvatar` 与 `CurrentASC`。
   - 当同一 Avatar 重复调用 `EnsureInitialized` 时，自动短路忽略。
   - 维护已授予技能句柄表 `GrantedAbilityHandles`；在 Pawn 阵亡触发 `EndPlay` 或 ASC 变更（`bAscChanged`）时，自动调用 `ClearAbility` 清除已授予能力，杜绝旧技能残留在 PlayerState ASC 中。
   - 当 PlayerController 或 Pawn 重新生成导致 ASC 发生变更（如重生、重新附身）时，自动触发重入自愈，清空初值置位并重新执行初始化。
2. **权威端分流与技能去重**：
   - `InitAbilityActorInfo` 在双端执行。
   - `WriteInitialAttributes` 与 `DefaultAbilities` 仅在权威端（`HasAuthority()`）执行，严格杜绝客户端虚假初值和网络复制冲突。
   - 在授予默认技能前先通过 `FindAbilitySpecFromClass` 检查，若已存在相同技能 Spec 则不重复赋予，彻底杜绝多次重生导致的技能实例堆积。
3. **复制就绪重试**：
   - 若权威端授予能力时组件尚未进入网络就绪态（`IsReadyForReplication()` 返回 false），则按帧定时器重试，并在达到 `MaxReplicationRetries` 阈值后强制赋予，确保高延迟与复杂加载时能力百分之百可用。

