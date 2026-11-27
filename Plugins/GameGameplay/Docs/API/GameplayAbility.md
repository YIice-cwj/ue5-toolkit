# GameGameplay 通用能力基类 API 参考

`UGameGameplayAbility` 是全项目所有 GameplayAbility 的通用父类，规范了激活前状态阻断守卫、本地触发来源记账与结束自动复位，默认采用 `InstancedPerActor + ReplicateNo` 消除全项目多余 Subobject 网络复制开销。

头文件：`#include "Ability/UGameGameplayAbility.h"`  
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
    UGameGameplayAbility();

    bool CanActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags = nullptr,
        const FGameplayTagContainer* TargetTags = nullptr,
        OUT FGameplayTagContainer* OptionalRelevantTags = nullptr
    ) const override;

    void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData
    ) override;

    void EndAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        bool bReplicateEndAbility,
        bool bWasCancelled
    ) override;

    [[nodiscard]] FORCEINLINE EGameAbilityTriggerSource GetTriggerSource() const noexcept {
        return TriggerSource;
    }

protected:
    UPROPERTY(EditDefaultsOnly, Category = "Game|Ability")
    FGameplayTagContainer BlockingStateTags;

    UPROPERTY(EditDefaultsOnly, Category = "Game|Ability")
    TMap<FGameplayTag, EGameAbilityTriggerSource> TriggerSourceTagMap;
};
```

---

## 3. 核心机制

- **零多余 Subobject 复制**：默认 `InstancingPolicy = InstancedPerActor`、`ReplicationPolicy = ReplicateNo`，网络同步沿用 GAS 原生预测管线。
- **零临时容器拷贝阻断过滤**：`CanActivateAbility` 命中 `BlockingStateTags` 时直接遍历 `BlockingStateTags` 并逐项检测 `ASC->HasMatchingGameplayTag`，避免整份拷贝 `OwnedGameplayTags`，精准将命中的具体状态标签回填至 `OptionalRelevantTags`。
- **触发来源记账与复位**：`ActivateAbility` 解析 `TriggerEventData` 记录本次触发来源；`EndAbility` 先转交基类完成 `K2_OnEndAbility` 与 `OnGameplayAbilityEnded` 通知（回调期内仍可读取本次触发来源），确认能力已不再活跃后再复位为 `Input`，杜绝 `InstancedPerActor` 跨次激活脏残留（子类覆写 `ActivateAbility` / `EndAbility` 时须调用 `Super`）。
