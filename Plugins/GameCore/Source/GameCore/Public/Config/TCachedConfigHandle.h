/**
 * @file TCachedConfigHandle.h
 * @brief 零哈希高性能响应式配置句柄（热重载自同步 · 零哈希开销 · 线程安全）
 *
 * 设计定位：
 * - 痛点：高频热点链路（如物理更新、移动计算、战斗计算）每帧读取配置时，传统按键查找需频繁进行 FName 哈希查找与全局读写锁竞争；
 * - 解决方案：通过响应式订阅（FObserverSubscription）将配置值直接缓存于局部内存中，读取操作消灭哈希查找与字典遍历；
 * - 自同步保证：配置项发生注册、覆盖或热更时，底层响应式管道自动刷新缓存值并通知用户回调，杜绝脏读；
 * - 跨环境兼容：支持 World 上下文绑定与 PIE 多实例隔离，支持动态重绑定（Rebind）。
 */

#pragma once

#include "CoreMinimal.h"
#include <atomic>
#include <type_traits>

#include "Config/FConfigRegistry.h"
#include "Config/FConfigVariant.h"
#include "Listener/FObserverSubscription.h"
#include "Misc/ScopeRWLock.h"

/**
 * @brief 响应式缓存配置句柄
 * @tparam T 配置值类型（必须为 FConfigVariant 支持的类型之一）
 */
template<typename T>
class TCachedConfigHandle {
    static_assert(
        std::is_same_v<T, bool> ||
        std::is_same_v<T, int32> ||
        std::is_same_v<T, int64> ||
        std::is_same_v<T, float> ||
        std::is_same_v<T, double> ||
        std::is_same_v<T, FString> ||
        std::is_same_v<T, FName> ||
        std::is_same_v<T, FVector> ||
        std::is_same_v<T, FRotator> ||
        std::is_same_v<T, FGameplayTag>,
        "TCachedConfigHandle<T> only supports types compatible with Game::Config::FConfigVariant."
    );

private:
    struct FEmptyAtomicStorage {};

    /**
     * @brief 句柄内部共享状态（支持句柄浅拷贝共享同一底层订阅；标量类型维护原子镜像实现零锁热路径读取）
     */
    struct FState {
        using FAtomicMirror = std::conditional_t<std::is_arithmetic_v<T>, std::atomic<T>, FEmptyAtomicStorage>;

        FName KeyName;
        T FallbackValue{};
        mutable FRWLock DataLock;
        T CachedValue{};
        FAtomicMirror AtomicCachedValue{};
        bool bIsBound = false;
        TSharedPtr<FObserverSubscription> Subscription;
        TFunction<void(const T&)> OnChangedCallback;

        /**
         * @brief 在持有 DataLock 写锁的前提下同步更新缓存值与标量原子镜像
         * @param InValue 新配置值
         */
        void StoreCachedValueLocked(const T& InValue) {
            CachedValue = InValue;
            if constexpr (std::is_arithmetic_v<T>) {
                AtomicCachedValue.store(InValue, std::memory_order_release);
            }
        }
    };

public:
    /**
     * @brief 默认构造函数（未绑定空句柄）
     */
    TCachedConfigHandle() = default;

    /**
     * @brief 构造并立即绑定到目标配置注册表
     * @param InKeyName 配置键名
     * @param InFallbackValue 配置未就绪时的回退值
     * @param WorldContextObject 关联的世界上下文对象（为空表示全局根注册表）
     */
    explicit TCachedConfigHandle(FName InKeyName, T InFallbackValue = T{}, const UObject* WorldContextObject = nullptr) {
        InitAndBind(InKeyName, MoveTemp(InFallbackValue), WorldContextObject);
    }

    /**
     * @brief 拷贝构造函数（共享底层订阅状态）
     * @param Other 源句柄
     */
    TCachedConfigHandle(const TCachedConfigHandle& Other) = default;

    /**
     * @brief 拷贝赋值运算符
     * @param Other 源句柄
     * @return 自身引用
     */
    TCachedConfigHandle& operator=(const TCachedConfigHandle& Other) = default;

    /**
     * @brief 移动构造函数
     * @param Other 源句柄
     */
    TCachedConfigHandle(TCachedConfigHandle&& Other) noexcept = default;

    /**
     * @brief 移动赋值运算符
     * @param Other 源句柄
     * @return 自身引用
     */
    TCachedConfigHandle& operator=(TCachedConfigHandle&& Other) noexcept = default;

    /**
     * @brief 析构函数
     */
    ~TCachedConfigHandle() = default;

    /**
     * @brief 重新绑定到目标世界上下文的配置注册表
     * @param WorldContextObject 新的世界上下文对象
     */
    void Rebind(const UObject* WorldContextObject = nullptr) {
        const FName CurrentKey = GetKey();
        if (CurrentKey.IsNone()) {
            return;
        }
        BindInternal(CurrentKey, WorldContextObject);
    }

    /**
     * @brief 重新设置配置键并绑定
     * @param InKeyName 新配置键名
     * @param InFallbackValue 新回退值
     * @param WorldContextObject 关联的世界上下文对象
     */
    void Reset(FName InKeyName, T InFallbackValue = T{}, const UObject* WorldContextObject = nullptr) {
        InitAndBind(InKeyName, MoveTemp(InFallbackValue), WorldContextObject);
    }

    /**
     * @brief 重置并清空当前句柄所有绑定与缓存
     */
    void Reset() {
        State.Reset();
    }

    /**
     * @brief 获取当前缓存的配置值（标量类型走零锁原子读，复合类型经读锁按值返回，无哈希表查找开销）
     * @return 当前有效配置值或回退值
     */
    [[nodiscard]] T Get() const {
        if (!State.IsValid()) {
            return T{};
        }
        if constexpr (std::is_arithmetic_v<T>) {
            return State->AtomicCachedValue.load(std::memory_order_acquire);
        } else {
            FReadScopeLock ReadLock(State->DataLock);
            return State->CachedValue;
        }
    }

    /**
     * @brief 隐式类型转换运算符（便于与原始类型自然交互）
     * @return 当前有效配置值或回退值
     */
    operator T() const {
        return Get();
    }

    /**
     * @brief 解引用运算符
     * @return 当前有效配置值
     */
    T operator*() const {
        return Get();
    }

    /**
     * @brief 局部值代理对象（支持 -> 成员直连访问，完全杜绝多线程锁逃逸与悬垂指针）
     */
    struct FArrowProxy {
        T Value;

        /**
         * @brief 代理内部值指针获取
         * @return 常量指针
         */
        const T* operator->() const noexcept {
            return &Value;
        }
    };

    /**
     * @brief 箭头运算符（支持对结构体配置成员的安全直连访问）
     * @return 局部只读代理对象
     */
    FArrowProxy operator->() const {
        return FArrowProxy{Get()};
    }

    /**
     * @brief 与同类型配置值的值相等性比较
     * @param Other 比较目标值
     * @return 值相等返回 true
     */
    bool operator==(const T& Other) const {
        return Get() == Other;
    }

    /**
     * @brief 获取配置键名
     * @return 配置键名
     */
    [[nodiscard]] FName GetKey() const {
        if (!State.IsValid()) {
            return NAME_None;
        }
        FReadScopeLock ReadLock(State->DataLock);
        return State->KeyName;
    }

    /**
     * @brief 获取配置未命中时的回退值
     * @return 回退值
     */
    [[nodiscard]] T GetFallback() const {
        if (!State.IsValid()) {
            return T{};
        }
        FReadScopeLock ReadLock(State->DataLock);
        return State->FallbackValue;
    }

    /**
     * @brief 查询句柄是否已成功从注册表获取到有效配置（而非处于回退态）
     * @return 存在已注册配置返回 true
     */
    [[nodiscard]] bool IsBound() const {
        if (!State.IsValid()) {
            return false;
        }
        FReadScopeLock ReadLock(State->DataLock);
        return State->bIsBound;
    }

    /**
     * @brief 查询当前句柄是否已初始化有效键名
     * @return 键名非空返回 true
     */
    [[nodiscard]] bool IsValid() const {
        return !GetKey().IsNone();
    }

    /**
     * @brief 注册配置热更监听回调（支持在默认构造后先设置回调再调用 Reset 绑定键名，且重绑定时保留已有回调）
     * @param InCallback 值变更时的通知闭包
     */
    void SetOnChanged(TFunction<void(const T&)> InCallback) {
        if (!State.IsValid()) {
            State = MakeShared<FState>();
        }
        FWriteScopeLock WriteLock(State->DataLock);
        State->OnChangedCallback = MoveTemp(InCallback);
    }

private:
    /**
     * @brief 初始化或复用内部状态并执行绑定（保留已注册的 OnChangedCallback 与共享句柄引用）
     * @param InKeyName 配置键名
     * @param InFallbackValue 回退值
     * @param WorldContextObject 世界上下文
     */
    void InitAndBind(FName InKeyName, T&& InFallbackValue, const UObject* WorldContextObject) {
        if (!State.IsValid()) {
            State = MakeShared<FState>();
        }

        TSharedPtr<FObserverSubscription> OldSubscription;
        {
            FWriteScopeLock WriteLock(State->DataLock);
            OldSubscription = MoveTemp(State->Subscription);
            State->KeyName = InKeyName;
            State->FallbackValue = MoveTemp(InFallbackValue);
            State->StoreCachedValueLocked(State->FallbackValue);
            State->bIsBound = false;
        }
        if (OldSubscription.IsValid()) {
            OldSubscription->Reset();
        }

        if (!InKeyName.IsNone()) {
            BindInternal(InKeyName, WorldContextObject);
        }
    }

    /**
     * @brief 执行底层订阅与初始值拉取
     * @param TargetKeyName 目标配置键名
     * @param WorldContextObject 世界上下文
     */
    void BindInternal(FName TargetKeyName, const UObject* WorldContextObject) {
        FConfigRegistry& Registry = FConfigRegistry::Get(WorldContextObject);

        TWeakPtr<FState> WeakState = State;
        TSharedPtr<FObserverSubscription> OldSubscription;
        {
            FWriteScopeLock WriteLock(State->DataLock);
            OldSubscription = MoveTemp(State->Subscription);
            State->bIsBound = false;
        }
        if (OldSubscription.IsValid()) {
            OldSubscription->Reset();
        }

        TSharedRef<FObserverSubscription> NewSubscription = Registry.WatchConfig<T>(
            TargetKeyName,
            [WeakState, TargetKeyName](const T& NewValue) {
                if (TSharedPtr<FState> Pinned = WeakState.Pin()) {
                    TFunction<void(const T&)> UserCallbackToInvoke;
                    {
                        FWriteScopeLock WriteLock(Pinned->DataLock);
                        if (Pinned->KeyName != TargetKeyName) {
                            return;
                        }
                        Pinned->StoreCachedValueLocked(NewValue);
                        Pinned->bIsBound = true;
                        UserCallbackToInvoke = Pinned->OnChangedCallback;
                    }
                    if (UserCallbackToInvoke) {
                        UserCallbackToInvoke(NewValue);
                    }
                }
            }
        );

        const TOptional<T> InitialValue = Registry.GetConfigOptional<T>(TargetKeyName);
        TSharedPtr<FObserverSubscription> SupersededSubscription;
        {
            FWriteScopeLock WriteLock(State->DataLock);
            if (State->KeyName != TargetKeyName) {
                SupersededSubscription = NewSubscription;
            } else {
                SupersededSubscription = MoveTemp(State->Subscription);
                State->Subscription = MoveTemp(NewSubscription);
                if (!State->bIsBound) {
                    if (InitialValue.IsSet()) {
                        State->StoreCachedValueLocked(*InitialValue);
                        State->bIsBound = true;
                    } else {
                        State->StoreCachedValueLocked(State->FallbackValue);
                        State->bIsBound = false;
                    }
                }
            }
        }
        if (SupersededSubscription.IsValid()) {
            SupersededSubscription->Reset();
        }
    }

private:
    TSharedPtr<FState> State;
};
