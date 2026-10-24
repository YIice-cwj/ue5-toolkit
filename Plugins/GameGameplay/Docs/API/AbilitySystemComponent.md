# GameGameplay ASC 与 Globals API 参考

头文件：
- `#include "AbilitySystem/UGameAbilitySystemComponent.h"`
- `#include "AbilitySystem/UGameAbilitySystemGlobals.h"`
- `#include "AbilitySystem/FGameGameplayEffectContext.h"`

---

## 1. `UGameAbilitySystemComponent`

通用能力系统组件基类。

```cpp
UCLASS()
class GAMEGAMEPLAY_API UGameAbilitySystemComponent : public UAbilitySystemComponent {
    GENERATED_BODY()

public:
    UGameAbilitySystemComponent();

    virtual void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) override;
};
```

### 核心特性
- 在构造函数中强制开启网络复制，并提前注册容器拥有者：
  ```cpp
  SetIsReplicatedByDefault(true);
  ActiveGameplayEffects.RegisterWithOwner(this);
  ActivatableAbilities.RegisterWithOwner(this);
  ```
- 解决 UE 引擎原生 ASC 默认 `bReplicates = false` 导致新手经常遗漏开启网络复制、进而导致 SpawnedAttributes 和技能状态在客户端丢失的经典隐患。
- 重写 `InitAbilityActorInfo`：在执行初始化前自动检测并分配 `AbilityActorInfo`，杜绝在单测环境或组件注册完成前执行握手时的断言崩溃。
- 是所有玩家、怪物和可交互实体的通用 ASC 基类。

---

## 2. `UGameAbilitySystemGlobals`

全局能力系统单例。

```cpp
UCLASS()
class GAMEGAMEPLAY_API UGameAbilitySystemGlobals : public UAbilitySystemGlobals {
    GENERATED_BODY()

public:
    virtual FGameplayEffectContext* AllocGameplayEffectContext() const override;
};
```

### 核心职责
- 重写 `AllocGameplayEffectContext()`，在引擎底层为所有 GameplayEffect 自动分配扩展的 `FGameGameplayEffectContext` 结构体实例。
- **项目启用配置**：在工程的 `Config/DefaultGame.ini` 中配置：
  ```ini
  [/Script/GameplayAbilities.AbilitySystemGlobals]
  AbilitySystemGlobalsClassName=/Script/GameGameplay.GameAbilitySystemGlobals
  ```
  *(注：UE 5.5+ 虽将配置归入 `UGameplayAbilitiesDeveloperSettings`，但引擎内部通过 `OverrideConfigSection` 保持对 `DefaultGame.ini` 下 `[/Script/GameplayAbilities.AbilitySystemGlobals]` 的完全兼容)*

---

## 3. `FGameGameplayEffectContext`

全 Tag 驱动的泛型打击反馈上下文结构体，继承自引擎 `FGameplayEffectContext`，与 `FGameDamageResult` 1:1 对齐。

```cpp
USTRUCT()
struct GAMEGAMEPLAY_API FGameGameplayEffectContext : public FGameplayEffectContext {
    GENERATED_BODY()

public:
    // 实际总伤害
    [[nodiscard]] float GetActualDamage() const noexcept;
    void SetActualDamage(float InActualDamage) noexcept;

    // 通道实际伤害追踪
    [[nodiscard]] float GetActualChannelDamage(const FGameplayTag& ChannelTag) const noexcept;
    void SetActualChannelDamage(const FGameplayTag& ChannelTag, float Magnitude);
    [[nodiscard]] const TArray<FGameDamageChannel>& GetActualChannels() const noexcept;

    // 反馈状态标签（如 Hit, Fatal, Critical, Block, Stagger）
    [[nodiscard]] bool HasFeedbackTag(const FGameplayTag& InTag) const noexcept;
    void AddFeedbackTag(const FGameplayTag& InTag) noexcept;
    void RemoveFeedbackTag(const FGameplayTag& InTag) noexcept;
    [[nodiscard]] const FGameplayTagContainer& GetFeedbackTags() const noexcept;

    // 动态数值参数（如 HitStop, PoiseDamage, ShieldAbsorb, Knockback）
    [[nodiscard]] float GetDynamicValue(const FGameplayTag& InTag, float DefaultValue = 0.0f) const noexcept;
    void SetDynamicValue(const FGameplayTag& InTag, float InValue);
    [[nodiscard]] bool HasDynamicValue(const FGameplayTag& InTag) const noexcept;
    [[nodiscard]] const TArray<FGameDamageChannel>& GetDynamicValues() const noexcept;

    // 统一转换为打击反馈结果
    [[nodiscard]] FGameDamageResult ToDamageResult() const;

    virtual UScriptStruct* GetScriptStruct() const override;
    virtual FGameplayEffectContext* Duplicate() const override;
    virtual bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) override;

    [[nodiscard]] static FGameGameplayEffectContext* ExtractEffectContext(const FGameplayEffectContextHandle& InHandle);
};
```
- 支持紧凑网络序列化（`NetSerialize`），权威端与客户端同步保留全套打击反馈数据。

