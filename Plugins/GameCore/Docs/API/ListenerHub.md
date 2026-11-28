# GameCore 监听总线 API 参考

> **一句话**：`FListenerHub` 事件总线、`FValueObserver<T>` 响应式值观察与 `FNotificationDispatcher` 全局 FIFO 派发的公共 API。
> **头文件**：`Listener/FListenerHub.h` · `Listener/FValueObserver.h` · `Listener/FObserverSubscription.h` · `Listener/FNotificationDispatcher.h`
> **线程**：以上设施均线程安全；`FListenerHub::Emit` 在调用线程同步直派，`FValueObserver` 经全局 FIFO 在调用线程排空；绑定非空 `UObject* Owner` 的订阅须在 GameThread 调用
> **失败语义**：无错误码；`Emit` 事件不存在或载荷类型不匹配返回 `false`；`Subscribe` 类型冲突返回 `IsActive() == false` 的空守卫

## 30 秒速览

- **事件总线**：`FListenerHub::Subscribe<T>` 按 `FName` 注册并在事件不存在时自动创建；`Emit<T>` 以 64 位类型指纹 + `sizeof` 双校验后广播。
- **零锁回调**：`Emit` 仅在拷贝 Copy-on-Write 回调快照的瞬态持读锁，出锁后遍历，回调全程零锁，可在回调内再次 `Subscribe` / `Emit`。
- **值观察**：`FValueObserver<T>` 提供 `Set` / `Update` / `Get`、可选初始值回放与 Owner 弱引用生命周期绑定。
- **订阅守卫**：`FObserverSubscription` 析构即退订，业务方必须持有 `TSharedRef`；临时对象会立即退订。
- **延迟派发**：`FNotificationDispatcher::Enqueue` 选举唯一 Drainer，返回 `true` 时必须在释放源状态锁后调用 `Drain()`。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FListenerHub::Get` | 获取事件总线单例 | 函数局部静态单例，C++11 线程安全初始化 |
| `FListenerHub::Subscribe<T>` | 按事件名注册回调 | 事件自动创建；空名 / 空回调 / 类型冲突返回空守卫 |
| `FListenerHub::Emit<T>` | 同步广播事件载荷 | 事件不存在或载荷指纹不匹配返回 `false`；回调全程零锁 |
| `FListenerHub::Reset` | 清空全部事件通道与监听器 | 使全部既有订阅失效 |
| `FValueObserver<T>::Set` | 更新观测值并广播 | 禁用时静默跳过；出锁后调用 `Drain` 派发 |
| `FValueObserver<T>::Update` | 经闭包变换观测值 | 非原子读-改-写 |
| `FValueObserver<T>::Get` | 取当前值深拷贝 | 读锁内拷贝后立即出锁 |
| `FValueObserver<T>::SubscribeWithReplay` | 注册回调并立即回放当前值 | 传入非空 `Owner` 须在 GameThread 调用 |
| `FObserverSubscription::Reset` | 注销订阅并清空句柄 | 析构即退订；不可拷贝，仅可移动 |
| `FNotificationDispatcher::Enqueue` | 入队通知闭包 | 当选 Drainer 返回 `true`；须释放源状态锁后再调 `Drain()` |

## 1. 事件总线 `FListenerHub`

```cpp
class GAMECORE_API FListenerHub {
public:
    static FListenerHub& Get();

    template<typename T>
    TSharedRef<FObserverSubscription> Subscribe(
        const FName& EventName,
        TFunction<void(const T&)> InListener);

    template<typename T>
    bool Emit(const FName& EventName, const T& Payload);

    void Reset();
    [[nodiscard]] int32 Num() const;
    [[nodiscard]] bool IsEmpty() const;
    [[nodiscard]] bool Contains(const FName& EventName) const;
    [[nodiscard]] bool HasListeners(const FName& EventName) const;
    [[nodiscard]] int32 NumListeners(const FName& EventName) const;
};
```

| 方法 | 参数 | 返回 / 说明 |
| :--- | :--- | :--- |
| `Get` | 无 | 函数局部静态单例引用，C++11 线程安全初始化 |
| `Subscribe<T>` | 事件名、`TFunction<void(const T&)>` 回调 | RAII 订阅守卫；事件自动创建，类型冲突返回空守卫 |
| `Emit<T>` | 事件名、载荷 | 事件存在且载荷双指纹匹配完成广播返回 `true`，否则 `false` |
| `Reset` | 无 | 清空全部事件通道与监听器，供单测、PIE 重置与模块清理 |
| `Num` / `IsEmpty` | 无 | 当前活跃事件通道数量 / 是否为空 |
| `Contains` / `HasListeners` / `NumListeners` | 事件名 | 通道存在性 / 是否有活跃监听者 / 监听者数量 |

- **线程**：线程安全；事件表以 `FRWLock` 保护，回调在 `Emit` 调用线程同步执行。
- **生命周期**：`Subscribe` 返回的守卫必须由业务方持有，析构即按唯一 `SubscriptionId` 注销；`Reset` 会使全部既有订阅失效。
- **失败语义**：`EventName.IsNone()` 或传入空 `TFunction` 时 `Subscribe` 返回空守卫；已存在同名事件但载荷类型指纹或 `sizeof` 不一致时，`Subscribe` 记录 Error 日志并返回空守卫；`Emit` 对事件名无效、事件未创建或载荷类型不匹配返回 `false`。
- **类型安全**：载荷类型以 `Game::Core::GetTypeFingerprint<T>()`（编译器类型签名 + CityHash64）计算 64 位稳定哈希，跨 DLL/dylib 一致而非依赖 RTTI；叠加 `sizeof` 双指纹校验。
- **重入**：使用 Copy-on-Write 回调快照，回调内新增或退订的监听自下一轮 `Emit` 起生效。

### 最小示例

```cpp
TSharedRef<FObserverSubscription> HitSubscription = FListenerHub::Get().Subscribe<FVector>(
    TEXT("Combat.Hit"),
    [](const FVector& HitLocation) {
        UE_LOG(LogTemp, Log, TEXT("Hit at %s"), *HitLocation.ToString());
    });

FListenerHub::Get().Emit(TEXT("Combat.Hit"), FVector(100.0f, 0.0f, 0.0f));
```

## 2. 值观察者 `FValueObserver<T>`

```cpp
class GAMECORE_API FObserverBase {
public:
    explicit FObserverBase(const FName& InObserverName);

    [[nodiscard]] FName GetObserverName() const noexcept;
    void SetEnabled(bool bInEnabled);
    [[nodiscard]] bool IsEnabled() const noexcept;
    [[nodiscard]] uint32 GetVersion() const noexcept;
};

template<typename T>
class FValueObserver : public FObserverBase {
public:
    using ValueType = T;
    using ConstReference = const T&;
    using FListener = TFunction<void(ConstReference)>;

    explicit FValueObserver(const FName& ObserverName, ConstReference InitialValue = ValueType{});

    void Set(ConstReference InValue);
    void Update(TFunctionRef<void(ValueType&)> Mutator);
    [[nodiscard]] ValueType Get() const;

    TSharedRef<FObserverSubscription> Subscribe(FListener InListener);
    TSharedRef<FObserverSubscription> Subscribe(UObject* Owner, FListener InListener);
    TSharedRef<FObserverSubscription> SubscribeWithReplay(FListener InListener);
    TSharedRef<FObserverSubscription> SubscribeWithReplay(UObject* Owner, FListener InListener);

    [[nodiscard]] int32 NumListeners() const;
    [[nodiscard]] bool HasListeners() const;
};
```

| 方法 | 参数 | 返回 / 说明 |
| :--- | :--- | :--- |
| 构造函数 | 观察者名称、可选初始值 | 注册开关配置键 `Listener.{Name}.Enabled` |
| `Set` | 新值常量引用 | 更新当前值并广播；禁用时静默跳过 |
| `Update` | 修改值副本的闭包 | 先 `Get` 副本、闭包变换后 `Set`；非原子读-改-写 |
| `Get` | 无 | 当前值的深拷贝副本 |
| `Subscribe` | 回调（可选 `UObject* Owner`） | RAII 订阅守卫 |
| `SubscribeWithReplay` | 回调（可选 `UObject* Owner`） | 先注册再回放当前值，消除错过当前值的窗口 |
| `NumListeners` | 无 | GameThread 上跳过已失效宿主后的有效监听数 |
| `GetVersion` | 无 | 启用状态下每次值提交自增的版本号 |

- **线程**：内部状态以读写锁保护；`Get` 在读锁内拷贝后立即出锁，返回独立副本；`Set` 在写锁内改值并将监听快照入队，出锁后调用 `FNotificationDispatcher::Drain` 派发。
- **生命周期**：`ObserverName` 决定启停键 `Listener.{Name}.Enabled`，配置为唯一真源；禁用时 `Set` 静默跳过广播。
- **Owner 绑定**：条目持 `TWeakObjectPtr<UObject>`，Owner 销毁后广播自动跳过；失效条目在下一次主线程 `Set` 时惰性清理，GameThread 上的 `NumListeners` 仅跳过失效条目计数、不做移除，非 GameThread 直接返回原始条目数。
- **线程约束**：传入非空 `Owner` 的 `Subscribe` / `SubscribeWithReplay` 必须在 GameThread 调用（内部 `ensureMsgf` 校验）；`SubscribeWithReplay` 按单写线程契约在主线程调用。
- **失败语义**：传入空 `TFunction` 时 `Subscribe` 返回订阅 ID 为 0 的空守卫；宿主失效的监听被跳过而非报错。

### 最小示例

```cpp
FValueObserver<int32> HealthObserver(TEXT("Combat.Health"), 100);

TSharedRef<FObserverSubscription> HealthSubscription =
    HealthObserver.Subscribe([](const int32& NewHealth) {
        UE_LOG(LogTemp, Log, TEXT("Health: %d"), NewHealth);
    });

HealthObserver.Set(80);
```

## 3. RAII 订阅守卫 `FObserverSubscription`

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
    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] uint64 GetSubscriptionId() const noexcept;
    explicit operator bool() const noexcept;
};
```

| 方法 | 返回 / 说明 |
| :--- | :--- |
| `Reset` | 注销监听并清空订阅句柄 |
| `IsActive` / `operator bool` | 状态指针有效且订阅 ID 非零返回 `true` |
| `GetSubscriptionId` | 64 位订阅 ID，`0` 表示未初始化或已注销 |

- **线程**：守卫本身可移动、不可拷贝；退订按唯一 `uint64` 订阅 ID 匹配，不依赖 `TFunction` 相等比较。
- **生命周期**：内部持 `TWeakPtr<FObserverStateBase>`，观察者先销毁时守卫析构安全跳过；守卫析构自动注销。
- **失败语义**：移动后源守卫变为未初始化状态；未 `Initialize` 的空守卫 `IsActive() == false`，`Reset` 无副作用。

### 最小示例

```cpp
TSharedRef<FObserverSubscription> Subscription = Observer.Subscribe(
    [](const int32& NewValue) { /* ... */ });

Subscription->Reset(); // 立即退订
```

## 4. 全局通知派发器 `FNotificationDispatcher`

```cpp
class GAMECORE_API FNotificationDispatcher {
public:
    static FNotificationDispatcher& Get();

    [[nodiscard]] bool Enqueue(TFunction<void()> InNotification);
    void Drain();
};
```

| 方法 | 参数 | 返回 / 说明 |
| :--- | :--- | :--- |
| `Get` | 无 | 进程级唯一调度器引用 |
| `Enqueue` | 锁外执行的通知闭包 | 当前线程当选为 Drainer 返回 `true` |
| `Drain` | 无 | 由当选 Drainer 按 FIFO 顺序执行通知 |

- **线程**：任意线程可 `Enqueue`；同一时刻仅一个线程当选 Drainer，回调执行期间完全无锁。
- **生命周期**：通知闭包在队列中存活至被执行；`Drain` 返回后本批通知已全部执行。
- **失败语义**：`Enqueue` 传入空闭包返回 `false`；已有其他 Drainer 在排空时同样返回 `false`，此时调用者不得再调用 `Drain`；重入产生的次级通知追加到队尾，由当前 Drainer 顺次排空。
- **使用铁律**：必须在完全释放源状态锁后再调用 `Drain()`，否则会在持锁状态下执行用户代码。

### 最小示例

```cpp
bool bMustDrain = false;
{
    FWriteScopeLock Lock(StateLock);
    State = NewState;
    bMustDrain = FNotificationDispatcher::Get().Enqueue(
        [Snapshot = CopyListeners, NewState]() {
            for (auto& Listener : Snapshot) {
                Listener(NewState);
            }
        });
}
if (bMustDrain) {
    FNotificationDispatcher::Get().Drain();
}
```

## 5. 通信选型

| 需求场景 | 推荐选型 | 核心特性 |
| :--- | :--- | :--- |
| 底层纯 C++ 高频事件广播（多订阅者、瞬时事件、无当前值） | `FListenerHub` | 64 位类型指纹、Copy-on-Write 零锁派发 |
| 实体内部响应式属性（单一数值变化、带当前值回放） | `FValueObserver<T>` | 强类型值语义、内聚于持有者实体或组件、自动回放当前值 |
| Gameplay 跨模块与蓝图通信（GameplayTag 寻址） | `UGameplayMessageSubsystem` | 树状分层寻址、原生蓝图异步节点、跨线程安全入队 |
| 全局配置项与运行时开关覆盖 | `FConfigRegistry` | 读写分离出锁派发、INI / 命令行自动注入与只读保护 |

## 常见错误与边界

- **丢弃订阅守卫**：`Subscribe` 返回的临时 `TSharedRef` 在语句结束即析构，等于刚订阅就退订；必须存入成员或长生命周期变量。
- **同名异类型**：同一 `FName` 先用 `T1` 订阅再用 `T2` 订阅，第二次 `Subscribe` 返回空守卫并记录 Error；`Emit<T2>` 返回 `false`。
- **未注册事件**：`Emit` 对从未 `Subscribe` 的事件静默返回 `false`，不会自动创建通道。
- **忘记排空**：`Enqueue` 返回 `true` 后未调用 `Drain()`，通知会滞留在队列中直到下一次有线程排空。
- **持锁排空**：在 `FWriteScopeLock` 等源状态锁作用域内调用 `Drain()` 会持锁执行用户回调，违背出锁派发铁律。
- **跨线程绑定 Owner**：在非 GameThread 传入非空 `UObject* Owner` 会触发 `ensureMsgf` 并与 GC 竞态。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [事件与消息架构](../Architecture/EventAndMessaging.md)
- [两层事件模型](../Architecture/TwoTierEventModel.md)
- [配置注册中心 API](ConfigRegistry.md)
- [快速上手](../Guides/QuickStart.md)
