# GameCore 任意数据观察者注册表 API 参考

`FObservableRegistry` 提供「按 Key 注册任意类型数据、订阅即得当前值」的零耦合计能力：注册方与订阅方只共享 Key 与值类型，互不感知对方模块。

- 头文件：`#include "Listener/FObservableRegistry.h"`
- World 级入口：`#include "System/UObservableRegistrySubsystem.h"`

---

## 1. 设计定位

| 问题 | `FObservableRegistry` 的回答 |
| :--- | :--- |
| 暴击、闪避、冷却等数值是否要框架预定义？ | 不需要，业务自行 `Register<T>(Key)` |
| 订阅方是否要包含写入方头文件？ | 不需要，只依赖 Key 与类型 |
| 订阅时数据已就绪怎么办？ | `Subscribe` 内部走 `SubscribeWithReplay`，立即回放当前值 |
| 跨动态库类型是否安全？ | 以 `GetTypeFingerprint<T>()` 做类型守卫，不匹配即拒绝 |
| 关卡切换如何清理？ | 世界级子系统随 World 销毁自动回收 |

---

## 2. `FObservableRegistry`

```cpp
namespace Game::Core {

class GAMECORE_API FObservableRegistry {
public:
    template<typename T> FValueObserver<T>* Register(const FName& Key);
    template<typename T> [[nodiscard]] TSharedPtr<FValueObserver<T>> Find(const FName& Key) const;
    template<typename T> bool Set(const FName& Key, const T& Value);
    template<typename T> TSharedRef<FObserverSubscription> Subscribe(
        const FName& Key, TFunction<void(const T&)> InListener);
    template<typename T> TSharedRef<FObserverSubscription> Subscribe(
        UObject* Owner, const FName& Key, TFunction<void(const T&)> InListener);

    bool Unregister(const FName& Key);
    [[nodiscard]] bool Contains(const FName& Key) const;
    [[nodiscard]] int32 Num() const;
    void Reset();
};

}
```

### 2.1 `Register<T>(Key)`
- Key 为空返回 `nullptr`。
- 首次注册创建 `FValueObserver<T>` 并持有共享所有权。
- 同 Key 同类型重复注册返回既有观察者。
- 同 Key 不同类型返回 `nullptr` 并记录 Error，绝不覆盖。

### 2.2 `Find<T>(Key)`
- 未注册或类型指纹不匹配返回 `nullptr`。
- 返回 `TSharedPtr<FValueObserver<T>>` 共享指针以持有观察者所有权，杜绝多线程并发注销/重置时的 Use-After-Free 悬垂野指针。

### 2.3 `Set<T>(Key, Value)`
- 未注册或类型不匹配返回 `false`。
- 成功写入后由观察者广播；观察者被禁用时静默跳过。

### 2.4 `Subscribe<T>(Key, Listener)`
- Key 未注册时自动注册。
- 返回 `TSharedRef<FObserverSubscription>`；调用方持有即可，销毁即退订。
- 订阅成功立即回放当前值，之后每次 `Set` 收到更新。
- 类型不匹配时返回空守卫，不接收任何回调。

### 2.5 `Subscribe<T>(Owner, Key, Listener)`
- 在原签名基础上前置 Owner：订阅条目持有 `TWeakObjectPtr<UObject>`。
- Owner 销毁后广播自动跳过该条目；条目在下一次 `Set` 或 `NumListeners` 时惰性清理。
- 订阅时 Owner 已失效则不回放当前值。
- 传 `nullptr` 与旧签名等价（不绑定生命周期），旧 API 行为不变。

### 2.6 生命周期
- `Unregister(Key)` 移除注册项；已持有的订阅守卫随观察者销毁自动失效。
- `Reset()` 清空全部注册项。
- 注册表实现以 `FRWLock` 保护键表；注册/注销在游戏线程完成，订阅与读取可跨线程。

---

## 3. `UObservableRegistrySubsystem`

```cpp
UCLASS()
class GAMECORE_API UObservableRegistrySubsystem : public UWorldSubsystem {
public:
    static UObservableRegistrySubsystem* Get(const UObject* WorldContextObject);
    [[nodiscard]] Game::Core::FObservableRegistry& GetRegistry();
    [[nodiscard]] const Game::Core::FObservableRegistry& GetRegistry() const;

    virtual void Deinitialize() override;
};
```

- World 作用域：随世界创建/销毁自动隔离，在 `Deinitialize()` 中主动调用 `Registry.Reset()` 释放全部观察者，不跨关卡泄漏。
- `Get` 在无世界上下文时返回 `nullptr`。

---

## 4. 示例

```cpp
#include "System/UObservableRegistrySubsystem.h"

void UCombatHud::Initialize(UObject* WorldContext)
{
    UObservableRegistrySubsystem* Observables = UObservableRegistrySubsystem::Get(WorldContext);
    if (!Observables) return;

    Game::Core::FObservableRegistry& Registry = Observables->GetRegistry();
    Registry.Register<float>(TEXT("Combat.CritChance"));
    Registry.Set<float>(TEXT("Combat.CritChance"), 0.25f);

    CritSubscription = Registry.Subscribe<float>(
        TEXT("Combat.CritChance"),
        [this](const float& Chance) { UpdateCritLabel(Chance); });
}
```

`CritSubscription` 声明为 `TSharedPtr<FObserverSubscription>` 成员；控件销毁时自动退订。
控件本身即 `UObject` 时，可改用 `Registry.Subscribe<float>(this, TEXT("Combat.CritChance"), ...)` 绑定生命周期，避免控件销毁到守卫析构之间的窗口收到回调。

---

## 5. 约束

- 同一 Key 只能对应一种值类型；需要多类型时使用不同 Key。
- `Get()` 返回引用，跨线程读取需业务方同步；写入按单写线程契约在主线程完成。
- 注册表不做序列化与网络复制，持久化请走存档段，网络同步请在业务层完成。
