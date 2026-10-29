# GameGameplayCore 可受击接口 API 参考

`IDamageable` 是所有能够接收伤害并产生打击反馈的实体契约。

头文件：`#include "Damage/IDamageable.h"`  
反射类：`UDamageable`  
接口类：`IDamageable`

---

## 1. 接口声明

```cpp
UINTERFACE(MinimalAPI, BlueprintType, Blueprintable)
class UDamageable : public UInterface {
    GENERATED_BODY()
};

class GAMEGAMEPLAYCORE_API IDamageable {
    GENERATED_BODY()

public:
    /**
     * @brief 接收一次伤害分发并同步返回打击反馈
     * @param Context 伤害上下文
     * @return 实际伤害、致死标记与打击感建议
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Game|Damage")
    FGameDamageResult ApplyDamageContext(const FGameDamageContext& Context);

    virtual FGameDamageResult ApplyDamageContext_Implementation(const FGameDamageContext& Context) {
        return FGameDamageResult();
    }
};
```

---

## 2. C++ 与蓝图调用规范

### 2.1 C++ 端安全调用
Unreal Engine 对 `BlueprintNativeEvent` 的调用需要通过 `Execute_` 静态辅助函数：
```cpp
if (TargetActor && TargetActor->Implements<UDamageable>())
{
    FGameDamageResult Result = IDamageable::Execute_ApplyDamageContext(TargetActor, DamageContext);
    // 处理结果
}
```

### 2.2 蓝图端调用
在蓝图事件图表中，直接拉出目标 Actor 的引脚，搜索并调用 `Apply Damage Context` 节点即可：
- 节点入参：`Context` (FGameDamageContext)
- 节点返回值：`Return Value` (FGameDamageResult)

---

## 3. 实现者编写规范

受击方在重写 `ApplyDamageContext_Implementation` 时，应遵循以下准则：
1. **纯同步结算**：所有受击减免、护甲计算、血量扣除必须在此函数内同步执行完毕，并填入 `FGameDamageResult`；
2. **纯表现异步分离**：受击动画蒙太奇、死亡布娃娃物理或音效，可在函数内通过委托（如 `OnStateChanged`）异步广播，但不应阻塞返回；
3. **免疫与未命中**：若处于无敌状态，返回 `TotalActualDamage = 0.0f`，并可通过 `Result.AddFeedbackTag(...)` 注入如 `Damage.Feedback.Immune` 或特定受击表现标签。

