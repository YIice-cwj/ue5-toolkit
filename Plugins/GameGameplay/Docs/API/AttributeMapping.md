# GameGameplay 属性表现映射 API 参考

`UGameplayAttributeValueMapping` 将 ASC 底层属性变更委托收敛为弱引用保护、支持对象池复用（`IGamePoolable`）且对 UMG 蓝图和原生 C++ 友好的双轨多播映射器。

头文件：`#include "Attribute/UGameplayAttributeValueMapping.h"`  
基类：`UObject`, `IGamePoolable`

---

## 1. 委托声明

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMappedAttributeChanged, float, NewValue, float, OldValue);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMappedAttributeChangedNative, float, float);
```

---

## 2. 类接口声明

```cpp
UCLASS(BlueprintType)
class GAMEGAMEPLAY_API UGameplayAttributeValueMapping : public UObject, public IGamePoolable {
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable, Category = "Game|GAS")
    FOnMappedAttributeChanged OnValueChanged;

    FOnMappedAttributeChangedNative OnValueChangedNative;

    UFUNCTION(BlueprintCallable, Category = "Game|GAS", meta = (DefaultToSelf = "Outer"))
    static UGameplayAttributeValueMapping* CreateAttributeValueMapping(
        UObject* Outer,
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayAttribute& InAttribute
    );

    static UGameplayAttributeValueMapping* CreateAttributeValueMapping(
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayAttribute& InAttribute
    );

    void BeginDestroy() override;
    void OnAcquireFromPool_Implementation() override;
    void OnReleaseToPool_Implementation() override;

    UFUNCTION(BlueprintCallable, Category = "Game|GAS")
    void Bind(
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayAttribute& InAttribute,
        bool bBroadcastImmediately = true
    );

    UFUNCTION(BlueprintCallable, Category = "Game|GAS")
    void Unbind();

    UFUNCTION(BlueprintPure, Category = "Game|GAS")
    [[nodiscard]] const FGameplayAttribute& GetBoundAttribute() const noexcept;

    UFUNCTION(BlueprintPure, Category = "Game|GAS")
    [[nodiscard]] UAbilitySystemComponent* GetBoundAbilitySystemComponent() const noexcept;

    UFUNCTION(BlueprintPure, Category = "Game|GAS")
    [[nodiscard]] bool IsBound() const noexcept;

    UFUNCTION(BlueprintPure, Category = "Game|GAS")
    [[nodiscard]] float GetCurrentValue() const;

    UFUNCTION(BlueprintCallable, Category = "Game|GAS")
    void QueryAndBroadcast();
};
```

---

## 3. 生命周期与契约说明

- **工厂创建与初值广播分离**：`CreateAttributeValueMapping` 返回新建实例（`ASC` 为空或 `InAttribute` 无效时返回 `nullptr`）；此时对象尚未注册任何监听者，调用方应先订阅 `OnValueChanged` / `OnValueChangedNative`，随后调用 `QueryAndBroadcast()` 同步拉取初值。
- **重绑语义**：对已有订阅者的映射对象调用 `Bind(NewASC, Attr, true)` 时，若参数有效则自动解绑旧源并即时广播新源当前值；若传入无效 `ASC` 或无效属性则安全忽略并保留原有绑定。
- **GC 失效与防虚假广播**：`IsBound()` 同时校验委托句柄与 `BoundAbilitySystemComponent.IsValid()`；当宿主 Pawn 销毁导致旧 ASC 被回收后，`IsBound()` 返回 `false`，且 `QueryAndBroadcast()` 静默跳过，避免向 UI 误播虚假的 `0.0f`。
- **对象池支持 (`IGamePoolable`)**：归还对象池（`OnReleaseToPool`）时自动解绑旧 ASC 并清空全部蓝图与原生多播订阅者，出库（`OnAcquireFromPool`）后可直接 `Bind` 复用。
