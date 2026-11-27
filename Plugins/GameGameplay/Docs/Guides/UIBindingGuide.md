# GameGameplay UMG 界面属性绑定指南

在传统的 GAS 界面制作中，UI 经常需要每帧 Tick 读取 `GetAttributeValue`，或者在 UI Widget 内自行注册 `GetGameplayAttributeValueChangeDelegate`，导致重复代码繁多、且容易发生委托泄漏。

`UGameplayAttributeValueMapping` 彻底简化了这一流程，并支持 `IGamePoolable` 对象池复用。

---

## 1. 在 UUserWidget 中集成

```cpp
#include "Blueprint/UserWidget.h"
#include "Attribute/UGameplayAttributeValueMapping.h"
#include "MyHeroAttributeSet.h"
#include "Components/ProgressBar.h"
#include "MyHealthWidget.generated.h"

UCLASS()
class UMyHealthWidget : public UUserWidget {
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "UI")
    void BindToHeroASC(UAbilitySystemComponent* HeroASC) {
        if (HeroASC == nullptr) {
            return;
        }

        // 1. 创建生命属性映射（Outer 传入 this，使映射随 UI 存活）
        HealthMapping = UGameplayAttributeValueMapping::CreateAttributeValueMapping(
            this,
            HeroASC,
            UMyHeroAttributeSet::GetHealthAttribute()
        );
        HealthMapping->OnValueChangedNative.AddLambda([this](float NewValue, float OldValue) {
            HandleHealthChanged(NewValue, OldValue);
        });
        HealthMapping->QueryAndBroadcast();

        // 2. 创建最大生命属性映射
        MaxHealthMapping = UGameplayAttributeValueMapping::CreateAttributeValueMapping(
            this,
            HeroASC,
            UMyHeroAttributeSet::GetMaxHealthAttribute()
        );
        MaxHealthMapping->OnValueChangedNative.AddLambda([this](float NewValue, float OldValue) {
            HandleMaxHealthChanged(NewValue, OldValue);
        });
        MaxHealthMapping->QueryAndBroadcast();
    }

protected:
    void NativeDestruct() override {
        if (HealthMapping != nullptr) {
            HealthMapping->Unbind();
        }
        if (MaxHealthMapping != nullptr) {
            MaxHealthMapping->Unbind();
        }
        Super::NativeDestruct();
    }

private:
    void HandleHealthChanged(float NewValue, float OldValue) {
        (void)OldValue;
        CurrentHealth = NewValue;
        RefreshBar();
    }

    void HandleMaxHealthChanged(float NewValue, float OldValue) {
        (void)OldValue;
        CurrentMaxHealth = NewValue;
        RefreshBar();
    }

    void RefreshBar() {
        if (CurrentMaxHealth > 0.0f && HealthProgressBar != nullptr) {
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

在蓝图 Widget 的 `Construct` 事件中：
1. 调用 `Create Attribute Value Mapping` 节点，传入目标 ASC 和 `Health` 属性（`Outer` 默认自动绑定当前 Widget `Self`）；
2. 拉出返回对象的 `On Value Changed` 委托，绑定自定义事件更新进度条；
3. 调用 `Query And Broadcast` 立即刷新初始数值（若之后更换角色只需对同一映射对象调用 `Bind(NewASC, Attr, true)` 即可自动切换并刷新）；
4. 在 `Destruct` 事件中调用 `Unbind`（对象销毁或归还对象池时亦会自动解绑）。
