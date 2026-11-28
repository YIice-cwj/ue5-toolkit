# GameCore 事件与消息通信体系架构

在大型游戏项目中，跨模块、跨对象的通信需求极其多样。`GameCore` 针对不同维度的通信场景，提供了职责清晰、定位互补的三层通信设施：

```mermaid
graph TD
    A["通信需求产生"] --> B{"通信特点与场景"}

    B -- "纯 C++ 零 UObject 依赖<br>纳秒级极速单例总线" --> C["FListenerHub<br>(中央监听总线)"]
    B -- "单值变更感知<br>响应式容器/初始回放" --> D["FValueObserver<T><br>(值观察者)"]
    B -- "异步解耦/消除递归调用栈<br>严格 FIFO 单消费 Drainer" --> E["FNotificationDispatcher<br>(单向事件排空器)"]
    B -- "蓝图/C++ 强类型解耦<br>GameplayTag 树形寻址/跨线程安全入队" --> F["UGameplayMessageSubsystem<br>(业务层 Gameplay 消息总线)"]
```

---

## 1. 通信机制详细对比与技术选型

| 特性 | `FListenerHub` | `FValueObserver<T>` | `FNotificationDispatcher` | `UGameplayMessageSubsystem` (Tier 1) |
| :--- | :--- | :--- | :--- | :--- |
| **存在形态** | 纯 C++ 静态单例 | 独立 C++ 实例 (可内嵌到任意类) | 纯 C++ 实例 / 单例 | `UGameInstanceSubsystem` (伴随游戏生命周期) |
| **推荐定位** | **引擎底层纯 C++ 高频内部通信** | **实体/组件内部响应式属性** | **异步解耦、削平调用栈深度** | **业务层/跨插件 Gameplay 解耦通信骨干** |
| **寻址方式** | `FName` 事件名 | 变量直接持有绑定 | 无（闭包直接入队，无键寻址） | `FGameplayTag` 标签 (支持层级匹配与通配) |
| **载荷类型** | 任意 C++ 结构体/类 (非 UObject) | 单一泛型类型 `T` | 任意可调用闭包 `TFunction<void()>` | `UScriptStruct` 反射结构体 (支持动态多态) |
| **类型安全机制** | 64 位编译期指纹 + `sizeof` 校验 | 模板静态强类型 | C++ 模板闭包类型（无运行时指纹校验） | `UScriptStruct` 强类型 RTTI 校验 |
| **当前值回放** | 否（事件无状态） | 支持 `SubscribeWithReplay` | 否（队列消费） | 否 |
| **蓝图支持** | 否 (专为高性能 C++ 设计) | 否 | 否 | 是 (`UAsyncAction_ListenForGameplayMessage`) |
| **并发保证** | Copy-on-Write 瞬态读锁快照，出锁派发 | 内部锁保护，出锁通知 | 互斥锁队列保护，单 Drainer 消费 | 主线程派发 + `EnqueueMessage` 跨线程安全入队 |
| **退订管理** | `FObserverSubscription` (RAII) | `FObserverSubscription` (RAII) | 无（一次性入队执行，无退订语义） | `FGameplayMessageListenerHandle` |

---

## 2. FListenerHub 架构与类型指纹安全

### 2.1 跨动态库类型指纹 (Type Fingerprint)
在动态链接库（DLL / dylib）环境下，C++ 原生 RTTI（`typeid`）可能因各模块符号独立而产生跨模块指针不一致问题。
`FListenerHub` 提取函数特征签名，并在首次调用时静态初始化 64 位稳定哈希（CityHash64，见 `Types/FGameTypeFingerprint.h`）：
```cpp
#if defined(_MSC_VER)
    const char* TypeSignature = __FUNCSIG__;
#elif defined(__clang__) || defined(__GNUC__)
    const char* TypeSignature = __PRETTY_FUNCTION__;
#endif
    static const uint64 TypeId = CityHash64(TypeSignature, static_cast<uint32>(FCStringAnsi::Strlen(TypeSignature)));
```
在订阅时，事件结构体通过 `GetPayloadTypeId()` 固化该类型的 64 位哈希指纹；在 `Emit` 派发时，校验入参类型的指纹与 `sizeof` 是否一致。不匹配时静默忽略并返回 `false`（仅订阅期载荷类型冲突记录 Error 日志），避免在预期失败路径上触发断言。

### 2.2 Copy-on-Write 回调快照模型
```mermaid
sequenceDiagram
    participant Emitter as 派发者 (Emit)
    participant Hub as FListenerHub (事件总线)
    participant Lock as FRWLock 读写锁
    participant Callback as 订阅者回调 (User Code)

    Emitter->>Hub: Emit<FPayload>(EventName, Data)
    Hub->>Lock: 获取瞬态读锁 (~20ns)
    Hub->>Hub: 浅拷贝 TSharedPtr<const TArray<FHandlerEntry>>
    Hub->>Lock: 释放读锁 (退出临界区)
    Note over Hub: 此时已完全处于零锁状态
    Hub->>Callback: 遍历快照依次执行 Callback(Data)
    opt 回调内再次 Subscribe / Emit
        Callback->>Hub: Subscribe(EventName, NewCallback)
        Note over Hub: 获取写锁创建新快照，原遍历快照不受影响
    end
```

---

## 3. FNotificationDispatcher 防重入与单 Drainer 机制

在深度嵌套的业务逻辑中，如果在回调过程中又触发新的事件派发，调用栈会急剧膨胀，极易引发爆栈或状态不同步。
`FNotificationDispatcher` 引入了 **FIFO 消息队列 + 互斥锁保护的唯一 Drainer** 消费机制：
- 任意线程调用 `Enqueue` 将通知闭包推入内部队列；
- 当前正在执行 `Drain()` 的线程成为唯一 Drainer；
- 回调中新产生的入队操作仅向队尾追加，不会打断当前消费循环，杜绝递归重入；
- 消费完全按先进先出（FIFO）顺序展开，彻底抹平调用栈。
