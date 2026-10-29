# GameGameplay 属性表现映射 API 参考

`UGameplayAttributeValueMapping` 将 ASC 复杂的底层复制委托封装为轻量、弱引用保护且对 UMG 和原生 C++ 友好的双轨多播映射器。

头文件：`#include "Attribute/UGameplayAttributeValueMapping.h"`  
基类：`UObject`

---

## 1. 委托声明

```cpp
/**
 * @brief 属性值发生变更时的蓝图动态多播（新值 / 旧值）
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAttributeValueChanged, float, NewValue, float, OldValue);

/**
 * @brief 属性值发生变更时的原生 C++ 多播（零反射开销，支持绑定 Lambda）
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAttributeValueChangedNative, float /* NewValue */, float /* OldValue */);
```

---

## 2. 类接口声明

```cpp
UCLASS(BlueprintType)
class GAMEGAMEPLAY_API UGameplayAttributeValueMapping : public UObject {
    GENERATED_BODY()

public:
    UGameplayAttributeValueMapping();

    /** 蓝图/动态多播委托，UI 蓝图可直接绑定 */
    UPROPERTY(BlueprintAssignable, Category = "Game|GAS")
    FOnAttributeValueChanged OnValueChanged;

    /** 原生 C++ 多播委托（零反射开销，支持 Lambda） */
    FOnAttributeValueChangedNative OnValueChangedNative;

    /**
     * @brief 静态工厂：创建并绑定指定属性变更监听对象
     * @param InAbilitySystemComponent 目标 ASC
     * @param InAttribute 目标属性（如 UMyHeroAttributeSet::GetHealthAttribute()）
     * @param bBroadcastImmediately 是否在绑定瞬间立即拉取当前值广播一次（默认 true，彻底消除 UI 初始空白）
     * @param Outer 所属 Outer 对象（默认指向调用者 self，推荐传入宿主 Widget 保证生命周期受控）
     * @return 映射对象指针
     */
    UFUNCTION(BlueprintCallable, Category = "Game|GAS", meta = (DefaultToSelf = "Outer"))
    static UGameplayAttributeValueMapping* CreateAttributeValueMapping(
        UAbilitySystemComponent* InAbilitySystemComponent,
        FGameplayAttribute InAttribute,
        bool bBroadcastImmediately = true,
        UObject* Outer = nullptr
    );

    static UGameplayAttributeValueMapping* CreateAttributeValueMapping(
        UAbilitySystemComponent* InAbilitySystemComponent,
        FGameplayAttribute InAttribute,
        bool bBroadcastImmediately
    );

    /**
     * @brief 主动绑定属性监听
     */
    UFUNCTION(BlueprintCallable, Category = "Game|GAS")
    void Bind(
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayAttribute& InAttribute,
        bool bBroadcastImmediately = true
    );

    /**
     * @brief 显式解绑并注销委托
     */
    UFUNCTION(BlueprintCallable, Category = "Game|GAS")
    void Unbind();

    /**
     * @brief 获取当前映射属性的数值（解绑或无效时返回 0.0f）
     */
    UFUNCTION(BlueprintCallable, Category = "Game|GAS")
    [[nodiscard]] float GetCurrentValue() const;

    /**
     * @brief 主动重新查询数值并广播给所有监听器（常用于 UI 重新唤起显示）
     */
    UFUNCTION(BlueprintCallable, Category = "Game|GAS")
    void QueryAndBroadcast();

    [[nodiscard]] const FGameplayAttribute& GetBoundAttribute() const noexcept;
    [[nodiscard]] bool IsBound() const noexcept;
};
```

---

## 3. 生命周期与最佳实践

- **即时广播机制（Immediate Broadcast）**：创建或手动 `Bind` 时默认 `bBroadcastImmediately = true`，绑定完成后瞬间自动读取 ASC 当前数值并通过 `OnValueChanged` 与 `OnValueChangedNative` 广播，彻底杜绝 UI 初次打开显示空白的顽疾。
- **双轨委托支持**：
  - 蓝图层可通过 `OnValueChanged` 拖拽节点直接响应；
  - C++ 表现层或 UI Presenter 推荐使用 `OnValueChangedNative.AddLambda(...)`，消除 UFunction 反射调用与装箱开销。
- **受控 Outer 生命周期**：工厂方法支持指定 `Outer`（蓝图调用端默认自动指向调用者 self Widget）。Mapping 随 Widget 存续，当玩家 Pawn 阵亡导致旧 ASC 销毁时，Mapping 不会作为其子对象连带被标记垃圾，重生后可继续复用。
- **防悬垂与 GC 析构保护**：映射对象内部持有 ASC 的弱引用（`TWeakObjectPtr<UAbilitySystemComponent>`），并在 `Unbind` 时校验句柄有效性及 `!ASC->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)`，杜绝垃圾回收阶段跨不可达对象调用的崩溃。

