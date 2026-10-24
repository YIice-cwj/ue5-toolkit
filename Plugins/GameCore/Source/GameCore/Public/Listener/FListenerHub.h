/**
 * @file FListenerHub.h
 * @brief 中央监听总线（单例）：跨动态库稳定类型指纹 + Copy-on-Write 回调快照
 *
 * 设计思路：
 * - 事件按 FName 存储于 TSharedPtr<FEventBase>（共享所有权），复用 FObserverSubscription RAII 守卫；
 * - 类型安全：以 GAME_TYPE_FINGERPRINT 编译期类型签名的 64 位稳定哈希作 TypeID（跨 DLL/dylib 一致，
 *   非 RTTI），叠加载荷 sizeof 一致性双指纹断言，将哈希碰撞从理论可能压至可忽略；
 * - 并发：每个事件持 COW 回调快照，Emit 仅在拷贝快照指针的瞬态持读锁（~20ns）、出锁后遍历，
 *   **回调与遍历全程零锁**，天然支持回调内再次 Subscribe / Emit；条目显式携带唯一 SubscriptionId
 *   （TFunction 无可靠 ==，退订只能按 ID 匹配）；
 * - 铁律：**永不在任何锁内调用用户代码**——快照拷贝点一律
 *   "瞬态读锁拷贝指针、立即出锁"；
 * - 未注册事件：Subscribe 自动创建；Emit 静默忽略。
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeRWLock.h"

#include "Listener/FGameTypeFingerprint.h"
#include "Listener/FObserverSubscription.h"

GAMECORE_API DECLARE_LOG_CATEGORY_EXTERN(LogGameListenerHub, Log, All);

/**
 * @brief 中央监听总线（函数局部静态单例：C++11 线程安全初始化，无堆分配）
 */
class GAMECORE_API FListenerHub {
private:
    /** 类型擦除事件基类（继承观察者状态基类以复用 RAII 订阅守卫） */
    class FEventBase : public FObserverStateBase {
    public:
        FEventBase() = default;
        virtual ~FEventBase() = default;

        /**
         * @brief 获取载荷类型指纹（Emit 类型匹配校验，防类型不匹配强转）
         * @return 跨动态库一致的 64 位类型哈希
         */
        [[nodiscard]] virtual uint64 GetPayloadTypeId() const noexcept = 0;

        /**
         * @brief 获取载荷字节数（与类型指纹叠加的双指纹兜底）
         * @return 载荷 sizeof
         */
        [[nodiscard]] virtual uint32 GetPayloadSize() const noexcept = 0;
    };

    /** 类型化事件（持 COW 回调快照） */
    template<typename T>
    class FEvent : public FEventBase {
    public:
        using FListener = TFunction<void(const T&)>;

        /** 监听条目：唯一订阅 ID + 回调 */
        struct FHandlerEntry {
            uint64 SubscriptionId = 0;
            FListener Callback;
        };

        FEvent() = default;

        /**
         * @brief 追加监听（写路径：拷贝快照 → 增删 → 原子替换）
         * @param InListener 回调
         * @return 唯一订阅 ID
         */
        uint64 AddListener(FListener InListener);

        /**
         * @brief 触发广播（仅拷贝快照瞬态持读锁，回调遍历零锁）
         * @param Payload 载荷
         */
        void Broadcast(const T& Payload) const;

    private:
        void RemoveListener(uint64 SubscriptionId) override;

        [[nodiscard]] uint64 GetPayloadTypeId() const noexcept override;

        [[nodiscard]] uint32 GetPayloadSize() const noexcept override;

    private:
        mutable FRWLock HandlersLock;
        TSharedPtr<const TArray<FHandlerEntry>> Handlers;
        uint64 NextId = 0;
    };

public:
    /**
     * @brief 注册事件监听（事件不存在则自动创建）
     * @tparam T 载荷类型
     * @param EventName 事件名
     * @param InListener 回调（收到载荷）
     * @return RAII 订阅守卫，业务方必须持有；析构守卫即注销
     */
    template<typename T>
    TSharedRef<FObserverSubscription> Subscribe(const FName& EventName, TFunction<void(const T&)> InListener);

    /**
     * @brief 触发事件广播（事件不存在或载荷双指纹不匹配时静默忽略）
     * @tparam T 载荷类型
     * @param EventName 事件名
     * @param Payload 载荷
     */
    template<typename T>
    void Emit(const FName& EventName, const T& Payload);

private:
    /**
     * @brief 获取事件（不存在则创建，类型指纹不一致时返回空并记录错误）
     * @tparam T 载荷类型
     * @param EventName 事件名
     * @return 事件共享指针；类型不匹配返回空
     */
    template<typename T>
    TSharedPtr<FEvent<T>> GetOrCreateEvent(const FName& EventName);

public:
    /**
     * @brief 获取全局单例
     * @return 单例引用
     */
    static FListenerHub& Get();

private:
    mutable FRWLock EventsLock;
    TMap<FName, TSharedPtr<FEventBase>> Events;
};

// ---------------------------------------------------------------------------
// 模板实现（定义全部置于类外，顺序与类内声明 1:1 对齐）
// ---------------------------------------------------------------------------

template<typename T>
uint64 FListenerHub::FEvent<T>::AddListener(FListener InListener) {
    if (!InListener) {
        return 0;
    }
    FWriteScopeLock WriteLock(HandlersLock);
    TArray<FHandlerEntry> Updated;
    if (Handlers.IsValid()) {
        Updated = *Handlers;
    }
    const uint64 SubscriptionId = ++NextId;
    Updated.Add(FHandlerEntry{SubscriptionId, MoveTemp(InListener)});
    Handlers = MakeShared<const TArray<FHandlerEntry>>(MoveTemp(Updated));
    return SubscriptionId;
}

template<typename T>
void FListenerHub::FEvent<T>::Broadcast(const T& Payload) const {
    TSharedPtr<const TArray<FHandlerEntry>> Snapshot;
    {
        FReadScopeLock ReadLock(HandlersLock);
        Snapshot = Handlers;
    }
    if (!Snapshot.IsValid()) {
        return;
    }
    for (const FHandlerEntry& Entry : *Snapshot) {
        Entry.Callback(Payload);
    }
}

template<typename T>
void FListenerHub::FEvent<T>::RemoveListener(uint64 SubscriptionId) {
    FWriteScopeLock WriteLock(HandlersLock);
    if (!Handlers.IsValid()) {
        return;
    }
    TArray<FHandlerEntry> Updated = *Handlers;
    const int32 RemovedCount = Updated.RemoveAll([SubscriptionId](const FHandlerEntry& Entry) {
        return Entry.SubscriptionId == SubscriptionId;
    });
    if (RemovedCount > 0) {
        Handlers = MakeShared<const TArray<FHandlerEntry>>(MoveTemp(Updated));
    }
}

template<typename T>
uint64 FListenerHub::FEvent<T>::GetPayloadTypeId() const noexcept {
    return Game::Core::GetTypeFingerprint<T>();
}

template<typename T>
uint32 FListenerHub::FEvent<T>::GetPayloadSize() const noexcept {
    return static_cast<uint32>(sizeof(T));
}

template<typename T>
TSharedRef<FObserverSubscription> FListenerHub::Subscribe(const FName& EventName, TFunction<void(const T&)> InListener) {
    const TSharedRef<FObserverSubscription> Subscription = MakeShared<FObserverSubscription>();
    const TSharedPtr<FEvent<T>> Event = GetOrCreateEvent<T>(EventName);
    if (Event.IsValid()) {
        Subscription->Initialize(Event.ToSharedRef(), Event->AddListener(MoveTemp(InListener)));
    }
    return Subscription;
}

template<typename T>
void FListenerHub::Emit(const FName& EventName, const T& Payload) {
    TSharedPtr<FEventBase> Event;
    {
        FReadScopeLock ReadLock(EventsLock);
        if (const TSharedPtr<FEventBase>* Found = Events.Find(EventName)) {
            Event = *Found;
        }
    }
    if (!Event.IsValid()
        || Event->GetPayloadTypeId() != Game::Core::GetTypeFingerprint<T>()
        || Event->GetPayloadSize() != static_cast<uint32>(sizeof(T))) {
        return;
    }
    static_cast<const FEvent<T>*>(Event.Get())->Broadcast(Payload);
}

template<typename T>
TSharedPtr<typename FListenerHub::FEvent<T>> FListenerHub::GetOrCreateEvent(const FName& EventName) {
    FWriteScopeLock WriteLock(EventsLock);
    if (TSharedPtr<FEventBase>* Existing = Events.Find(EventName)) {
        if ((*Existing)->GetPayloadTypeId() != Game::Core::GetTypeFingerprint<T>()
            || (*Existing)->GetPayloadSize() != static_cast<uint32>(sizeof(T))) {
            UE_LOG(LogGameListenerHub, Error,
                TEXT("【事件总线】事件 [%s] 已注册为其他载荷类型，本次订阅已忽略（返回空守卫）。"),
                *EventName.ToString());
            return nullptr;
        }
        return StaticCastSharedPtr<FEvent<T>>(*Existing);
    }
    const TSharedPtr<FEvent<T>> Created = MakeShared<FEvent<T>>();
    Events.Add(EventName, Created);
    return Created;
}
