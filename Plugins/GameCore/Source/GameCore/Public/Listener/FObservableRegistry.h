/**
 * @file FObservableRegistry.h
 * @brief 按 Key 注册任意类型观察者的泛型注册表（零耦合：注册方与订阅方互不感知）
 *
 * 设计思路：
 * - 每个 Key 对应一个 FValueObserver<T>，T 由注册方决定（暴击/闪避/冷却等任意数据类型）；
 * - 以 Game::Core::GetTypeFingerprint<T>() 做跨动态库类型守卫，类型不匹配时注册/查找返回 nullptr；
 * - 订阅统一走 SubscribeWithReplay：订阅成功即收到当前值，之后每次 Set 收到更新；
 * - 订阅可绑定 Owner：Owner 销毁后回调自动跳过，失效条目在下一次 Set 时惰性清理；
 * - 注册表只持观察者共享所有权，订阅守卫持弱引用——观察者先销毁时守卫析构安全跳过；
 * - 并发契约：注册/注销在游戏线程完成，订阅与读取可在任意线程（观察者内部读写锁保证）。
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeRWLock.h"

#include "Listener/FGameTypeFingerprint.h"
#include "Listener/FValueObserver.h"

namespace Game::Core {

GAMECORE_API DECLARE_LOG_CATEGORY_EXTERN(LogGameObservableRegistry, Log, All);

/**
 * @brief 任意类型观察者注册表（模板方法，无额外堆分配）
 */
class GAMECORE_API FObservableRegistry {
public:
    /**
     * @brief 注册（或复用）指定 Key 的类型化观察者
     * @tparam T 值类型
     * @param Key 数据键（全局唯一）
     * @return 观察者指针；Key 为空或已注册为其他类型时返回 nullptr
     */
    template<typename T>
    FValueObserver<T>* Register(const FName& Key);

    /**
     * @brief 查找指定 Key 的类型化观察者（线程安全，返回共享指针以保证脱锁生命周期安全）
     * @tparam T 值类型
     * @param Key 数据键
     * @return 观察者共享指针；未注册或类型不匹配返回空指针
     */
    template<typename T>
    [[nodiscard]] TSharedPtr<FValueObserver<T>> Find(const FName& Key) const;

    /**
     * @brief 设置指定 Key 的值并广播（未注册/类型不匹配返回 false）
     * @tparam T 值类型
     * @param Key 数据键
     * @param Value 新值
     * @return 是否成功写入并广播
     */
    template<typename T>
    bool Set(const FName& Key, const T& Value);

    /**
     * @brief 订阅指定 Key（不存在则自动注册）并立即回放当前值
     * @tparam T 值类型
     * @param Key 数据键
     * @param InListener 回调
     * @return RAII 订阅守卫；类型不匹配时返回空守卫（不接收任何回调）
     */
    template<typename T>
    TSharedRef<FObserverSubscription> Subscribe(const FName& Key, TFunction<void(const T&)> InListener);

    /**
     * @brief 订阅指定 Key 并绑定 Owner 生命周期（Owner 销毁后不再回调；非空 Owner 须在游戏线程调用）
     * @tparam T 值类型
     * @param Owner 订阅归属对象（为空表示不绑定生命周期）
     * @param Key 数据键
     * @param InListener 回调
     * @return RAII 订阅守卫；类型不匹配时返回空守卫（不接收任何回调）
     */
    template<typename T>
    TSharedRef<FObserverSubscription> Subscribe(UObject* Owner, const FName& Key, TFunction<void(const T&)> InListener);

    /**
     * @brief 注销指定 Key 的观察者
     * @param Key 数据键
     * @return 确实移除了已注册项返回 true
     */
    bool Unregister(const FName& Key);

    /**
     * @brief 查询指定 Key 是否已注册
     * @param Key 数据键
     * @return 已注册返回 true
     */
    [[nodiscard]] bool Contains(const FName& Key) const;

    /**
     * @brief 当前已注册键数量
     * @return 键数量
     */
    [[nodiscard]] int32 Num() const;

    /**
     * @brief 清空全部注册项（已持有的订阅守卫随观察者销毁自动失效）
     */
    void Reset();

private:
    /**
     * @brief 注册项结构（以 64 位编译期类型指纹与非模板基类持有观察者共享所有权）
     */
    struct FEntry {
        uint64 TypeFingerprint = 0;
        TSharedPtr<FObserverBase> Observer;
    };

    mutable FRWLock RegistryLock;
    TMap<FName, FEntry> Entries;
};

// ---------------------------------------------------------------------------
// 模板实现（定义全部置于类外，随头文件对使用方可见）
// ---------------------------------------------------------------------------

template<typename T>
FValueObserver<T>* FObservableRegistry::Register(const FName& Key) {
    if (Key.IsNone()) {
        return nullptr;
    }

    const uint64 Fingerprint = Game::Core::GetTypeFingerprint<T>();
    FWriteScopeLock WriteLock(RegistryLock);
    if (FEntry* Existing = Entries.Find(Key)) {
        if (Existing->TypeFingerprint != Fingerprint) {
            UE_LOG(LogGameObservableRegistry, Error,
                TEXT("【可观察注册表】键 [%s] 已注册为其他值类型，本次注册已忽略。"),
                *Key.ToString());
            return nullptr;
        }
        return static_cast<FValueObserver<T>*>(Existing->Observer.Get());
    }

    TSharedPtr<FValueObserver<T>> Created = MakeShared<FValueObserver<T>>(Key);
    Entries.Add(Key, FEntry{Fingerprint, Created});
    return Created.Get();
}

template<typename T>
TSharedPtr<FValueObserver<T>> FObservableRegistry::Find(const FName& Key) const {
    FReadScopeLock ReadLock(RegistryLock);
    const FEntry* Existing = Entries.Find(Key);
    if (Existing == nullptr || Existing->TypeFingerprint != Game::Core::GetTypeFingerprint<T>()) {
        return nullptr;
    }
    return StaticCastSharedPtr<FValueObserver<T>>(Existing->Observer);
}

template<typename T>
bool FObservableRegistry::Set(const FName& Key, const T& Value) {
    TSharedPtr<FValueObserver<T>> Observer = Find<T>(Key);
    if (!Observer.IsValid()) {
        return false;
    }
    Observer->Set(Value);
    return true;
}

template<typename T>
TSharedRef<FObserverSubscription> FObservableRegistry::Subscribe(const FName& Key, TFunction<void(const T&)> InListener) {
    return Subscribe<T>(nullptr, Key, MoveTemp(InListener));
}

template<typename T>
TSharedRef<FObserverSubscription> FObservableRegistry::Subscribe(UObject* Owner, const FName& Key,
    TFunction<void(const T&)> InListener) {
    FValueObserver<T>* Observer = Register<T>(Key);
    if (Observer == nullptr) {
        return MakeShared<FObserverSubscription>();
    }
    return Observer->SubscribeWithReplay(Owner, MoveTemp(InListener));
}

} // namespace Game::Core
