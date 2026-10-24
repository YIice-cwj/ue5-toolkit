# GameGameplay 通用能力基类 API 参考

`UGameGameplayAbility` 是全项目所有 GameplayAbility 的通用父类。它规范了激活前否决条件（Veto Guards）、触发来源记账与网络预测护栏。

头文件：`#include "AbilitySystem/UGameGameplayAbility.h"`  
基类：`UGameplayAbility`

---

## 1. 触发来源枚举 `EGameAbilityTriggerSource`

```cpp
UENUM(BlueprintType)
enum class EGameAbilityTriggerSource : uint8 {
    Input,  // 玩家按键/手柄输入触发
    Effect, // 外部 GameplayEffect 或事件标签被动触发
    AI,     // 行为树或 AI 控制器调用触发
};
```

---

## 2. `UGameGameplayAbility` 类接口

```cpp
UCLASS(Abstract)
class GAMEGAMEPLAY_API UGameGameplayAbility : public UGameplayAbility {
    GENERATED_BODY()

public:
    virtual bool CanActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags = nullptr,
        const FGameplayTagContainer* TargetTags = nullptr,
        OUT FGameplayTagContainer* OptionalRelevantTags = nullptr
    ) const override;

    virtual void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData
    ) override;

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** 最近一次激活的触发来源（已网络复制，子类可据此分叉表现） */
    [[nodiscard]] EGameAbilityTriggerSource GetTriggerSource() const noexcept;

protected:
    /** 激活否决标签：持有任一即短路激活（死亡 / 硬直等，具体由业务层配置） */
    UPROPERTY(EditDefaultsOnly, Category = "Game|Ability")
    FGameplayTagContainer BlockingStateTags;

    /** 触发事件标签 → 触发来源映射配置 */
    UPROPERTY(EditDefaultsOnly, Category = "Game|Ability")
    TMap<FGameplayTag, EGameAbilityTriggerSource> TriggerSourceTagMap;
};
```

