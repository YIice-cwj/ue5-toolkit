# GameCore 中央监听总线与观察者 API 参考

`FListenerHub`、`FValueObserver<T>`、`FObservableRegistry` 是 `GameCore` 提供的纯 C++ 事件与响应式基础设施。

头文件：

- `#include "Listener/FListenerHub.h"`
- `#include "Listener/FValueObserver.h"`
- `#include "Listener/FObservableRegistry.h"`
- `#include "Listener/FObserverSubscription.h"`

---

## 1. `FListenerHub`（中央事件总线）

函数局部静态单例，C++11 线程安全初始化。

```cpp
class GAMECORE_API FListenerHub {
public:
    static FListenerHub& Get();

    template<typename T>
    TSharedRef<FObserverSubscription> Subscribe(
        const FName& EventName,
        TFunction<void(const T&)> InListener);

    template<typename T>
    void Emit(const FName& EventName, const T& Payload);
};
```

### 1.1 `Subscribe<T>`
- 事件不存在时自动创建；已存在但载荷类型指纹或 `sizeof` 不一致时，返回空守卫并记录 Error。
- 返回 `TSharedRef<FObserverSubscription>`，业务方必须持有；守卫析构即注销。
- 订阅在写锁内以 Copy-on-Write 方式生成新回调快照。

### 1.2 `Emit<T>`
- 事件不存在、载荷类型指纹或 `sizeof` 不匹配时静默忽略。
- 仅在拷贝回调快照指针的瞬态持读锁，出锁后遍历，**回调全程零锁**。
- 支持回调内再次 `Subscribe` / `Emit`。

### 1.3 类型安全
- 载荷类型以 `GAME_TYPE_FINGERPRINT` 编译期签名计算 64 位稳定哈希，跨 DLL/dylib 一致（非 RTTI）。
- 类型指纹 + `sizeof` 双指纹校验，将哈希碰撞压至可忽略。

---

## 2. `FValueObserver<T>`（值观察者）

泛型响应式容器，值变化时通知订阅者。

```cpp
template<typename T>
class FValueObserver : public FObserverBase {
public:
    explicit FValueObserver(const FName& ObserverName);

    void Set(const T& InValue);
    [[nodiscard]] const T& Get() const noexcept;

    TSharedRef<FObserverSubscription> Subscribe(TFunction<void(const T&)> InListener);
    TSharedRef<FObserverSubscription> Subscribe(UObject* Owner, TFunction<void(const T&)> InListener);
    TSharedRef<FObserverSubscription> SubscribeWithReplay(TFunction<void(const T&)> InListener);
    TSharedRef<FObserverSubscription> SubscribeWithReplay(UObject* Owner, TFunction<void(const T&)> InListener);

    [[nodiscard]] int32 NumListeners();
};
```

- `ObserverName` 决定启停开关键 `Listener.{Name}.Enabled`；禁用时 `Set` 静默跳过广播。
- `Subscribe`：之后每次 `Set` 收到新值。
- `SubscribeWithReplay`：先注入监听再回放当前值，消除「订阅成功但错过当前值」的窗口；按单写线程契约在主线程调用。
- Owner 重载：条目持有 `TWeakObjectPtr<UObject>`，Owner 销毁后广播自动跳过；条目在下一次 `Set` 或 `NumListeners` 时惰性清理。传 `nullptr` 等同旧签名（不绑定生命周期）。
- `NumListeners()`：先清理失效条目再返回有效数量，便于观测订阅泄漏。
- 广播在读锁内拷贝监听快照后出锁派发，回调内增删监听不影响本次遍历。
- `Get()` 返回引用，值按单写线程契约更新；跨线程读取需调用方自行同步。
- `GetVersion()` 记录广播次数（继承自 `FObserverBase`）。

---

## 3. `FObserverSubscription`（RAII 订阅守卫）

```cpp
class [[nodiscard]] GAMECORE_API FObserverSubscription {
public:
    FObserverSubscription() = default;
    ~FObserverSubscription();

    FObserverSubscription(const FObserverSubscription&) = delete;
    FObserverSubscription& operator=(const FObserverSubscription&) = delete;
    FObserverSubscription(FObserverSubscription&& Other) noexcept;
    FObserverSubscription& operator=(FObserverSubscription&& Other) noexcept;

    void Reset();
};
```

- 守卫持 `TWeakPtr<FObserverStateBase>`；观察者先销毁时守卫析构安全跳过。
- 退订按唯一 `uint64` 订阅 ID 匹配，不依赖 `TFunction` 相等比较。
- 业务方必须持有 `TSharedRef<FObserverSubscription>`；临时对象立即析构会导致刚订阅即退订。

---

## 4. `FObservableRegistry`（任意类型观察者注册表）

```cpp
namespace Game::Core {
class GAMECORE_API FObservableRegistry {
public:
    template<typename T> FValueObserver<T>* Register(const FName& Key);
    template<typename T> [[nodiscard]] FValueObserver<T>* Find(const FName& Key) const;
    template<typename T> bool Set(const FName& Key, const T& Value);
    template<typename T> TSharedPtr<FObserverSubscription> Subscribe(
        const FName& Key, TFunction<void(const T&)> InListener);
    template<typename T> TSharedPtr<FObserverSubscription> Subscribe(
        UObject* Owner, const FName& Key, TFunction<void(const T&)> InListener);
    bool Unregister(const FName& Key);
    [[nodiscard]] bool Contains(const FName& Key) const;
    [[nodiscard]] int32 Num() const;
    void Reset();
};
}
```

- 每个 Key 对应一个 `FValueObserver<T>`，值类型由注册方决定。
- `Register` 幂等：同类型已注册时复用；类型不一致返回 `nullptr` 并记录 Error。
- `Subscribe` 不存在时自动注册，并立即回放当前值。
- `Subscribe(Owner, Key, ...)` 额外绑定 Owner 生命周期：Owner 销毁后不再回调，失效条目由观察者在 `Set` 时惰性清理。
- 注册表以读写锁保护键表，注册/注销在游戏线程完成；订阅与读取可在任意线程。
- 世界级入口：`UObservableRegistrySubsystem::Get(WorldContextObject)->GetRegistry()`。

---

## 5. 选型

| 需求 | 选择 |
| :--- | :--- |
| 事件广播，多个订阅者，无当前值 | `FListenerHub` |
| 单一数值变化感知 | `FValueObserver<T>` |
| 任意业务数据（暴击/闪避/冷却）按 Key 注册与订阅 | `FObservableRegistry` |
| 蓝图可监听、GameplayTag 寻址 | `UGameplayMessageSubsystem` |
