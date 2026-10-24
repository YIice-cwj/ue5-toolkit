# GameGameplay UMG 界面属性绑定指南

在传统的 GAS 界面制作中，UI 经常需要每帧 Tick 读取 `GetAttributeValue`，或者在 UI Widget 内自行注册 `GetGameplayAttributeValueChangeDelegate`，导致重复代码繁多、且容易发生委托泄漏。

`UGameplayAttributeValueMapping` 彻底简化了这一流程。

---

## 1. 在 UUserWidget 中集成

```cpp
#include "Blueprint/UserWidget.h"
#include "AbilitySystem/UGameplayAttributeValueMapping.h"
#include "MyHeroAttributeSet.h"
#include "Components/ProgressBar.h"
#include "MyHealthWidget.generated.h"

UCLASS()
class UMyHealthWidget : public UUserWidget {
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "UI")
    void BindToHeroASC(UAbilitySystemComponent* HeroASC) {
        if (!HeroASC) return;

        // 1. 创建生命属性映射（传入 this 作为 Outer，使映射对象随 UI 存活而非随 Pawn 析构）
        HealthMapping = UGameplayAttributeValueMapping::CreateAttributeValueMapping(
            HeroASC,
            UMyHeroAttributeSet::GetHealthAttribute(),
            true,
            this
        );
        // 原生 C++ 推荐直接通过 OnValueChangedNative 绑定 Lambda，消除 UFunction 反射开销
        HealthMapping->OnValueChangedNative.AddLambda([this](float NewValue, float OldValue) {
            HandleHealthChanged(NewValue, OldValue);
        });

        // 2. 创建最大生命属性映射
        MaxHealthMapping = UGameplayAttributeValueMapping::CreateAttributeValueMapping(
            HeroASC,
            UMyHeroAttributeSet::GetMaxHealthAttribute(),
            true,
            this
        );
        MaxHealthMapping->OnValueChangedNative.AddLambda([this](float NewValue, float OldValue) {
            HandleMaxHealthChanged(NewValue, OldValue);
        });
    }

protected:
    virtual void NativeDestruct() override {
        // UI 销毁时显式解绑
        if (HealthMapping) HealthMapping->Unbind();
        if (MaxHealthMapping) MaxHealthMapping->Unbind();
        Super::NativeDestruct();
    }

private:
    UFUNCTION()
    void HandleHealthChanged(float NewValue, float OldValue) {
        CurrentHealth = NewValue;
        RefreshBar();
    }

    UFUNCTION()
    void HandleMaxHealthChanged(float NewValue, float OldValue) {
        CurrentMaxHealth = NewValue;
        RefreshBar();
    }

    void RefreshBar() {
        if (CurrentMaxHealth > 0.0f && HealthProgressBar) {
            HealthProgressBar->SetPercent(CurrentHealth / CurrentMaxHealth);
        }
    }

    float CurrentHealth = 100.0f;
    float CurrentMaxHealth = 100.0f;

    UPROPERTY()
    TObjectPtr<UGameplayAttributeValueMapping> HealthMapping;

    UPROPERTY()
    TObjectPtr<UGameplayAttributeValueMapping> MaxHealthMapping;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UProgressBar> HealthProgressBar;
};
```

---

## 2. 蓝图可视化快速绑定

如果不写 C++ UI，直接在蓝图 Widget 的 `Construct` 事件中：
1. 调用 `Create Attribute Mapping` 节点，传入目标 ASC 和 `Health` 属性（`Outer` 参数默认自适应为当前 Widget `Self`，保证 Pawn 重生后 UI 绑定不失效）；
2. 拉出返回对象的 `On Value Changed` 委托，绑定自定义事件更新进度条；
3. 将返回对象提升为局部变量，需要时可通过 `GetBoundAttribute` 或 `IsBound` 检查状态，在 `Destruct` 事件中调用 `Unbind`。

