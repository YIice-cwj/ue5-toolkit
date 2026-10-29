/**
 * @file FConfigRegistry.h
 * @brief 通用配置注册表（零依赖，跨项目复用）：读写分离 + 纯值语义 + 响应式订阅
 *
 * 设计思路：
 * - 并发：FRWLock 读写分离；全部读接口在锁内完成值拷贝后立即出锁，调用方处于绝对零锁状态，
 *   从根上消灭跨帧持锁死锁与裸指针悬垂两类心智负担；
 * - 类型安全：键不存在按 Fallback 正常回退；键存在但类型不匹配在非 Shipping 下 ensureMsgf 强警告，
 *   Shipping 仅回退不炸，严禁静默吞错掩盖幽灵 Bug；
 * - 只读键：Env.* 恒只读，Shipping 下 Dev.* / Feature.GM 只读，SetConfig 统一入口拦截；
 * - 响应式：WatchConfig 返回 FObserverSubscription（RAII 守卫，析构即退订）；
 * 广播时序铁律——SetConfig 在写锁内改值后必须出锁再派发回调，持锁严禁调用任何用户回调。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeRWLock.h"

#include "Config/FConfigVariant.h"
#include "Listener/FObserverSubscription.h"

/**
 * @brief 全局配置注册表（函数局部静态单例：C++11 线程安全初始化，无堆分配）
 *
 * 功能：注册默认值（仅未注册时生效）、运行时覆盖（版本号自增并广播订阅）、
 * 强类型按值取值（未注册返回 Fallback）与响应式订阅（WatchConfig）。
 * 作用域边界：工程级单例，不做 World/PIE 实例隔离；世界相关数据严禁塞入本表。
 */
class GAMECORE_API FConfigRegistry {
public:
    /**
     * @brief 类型擦除配置变体别名
     */
    using FConfigVariant = Game::Config::FConfigVariant;

    /**
     * @brief 获取全局单例
     * @return 单例引用
     */
    static FConfigRegistry& Get();

    /**
     * @brief 注册默认值（仅未注册时写入，已有值不覆盖）
     * @tparam T 支持类型
     * @param Name 配置键
     * @param DefaultValue 默认值
     */
    template<typename T>
    void RegisterConfig(FName Name, T DefaultValue);

    /**
     * @brief 运行时覆盖（无则注册），版本号自增并广播 WatchConfig 订阅
     * @tparam T 支持类型
     * @param Name 配置键
     * @param NewValue 新值
     * @return 写入结果码（只读键返回 RejectedReadOnly）
     */
    template<typename T>
    ESetConfigResult SetConfig(FName Name, T NewValue);

    /**
     * @brief 按值取值（未注册或类型不匹配返回 Fallback，无悬垂风险）
     * @tparam T 支持类型
     * @param Name 配置键
     * @param FallbackValue 未命中时的回退值
     * @return 当前值或 Fallback
     */
    template<typename T>
    T GetConfig(FName Name, T FallbackValue = T{}) const;

    /**
     * @brief 按值取值（可选语义，未命中返回未设置）
     * @tparam T 支持类型
     * @param Name 配置键
     * @return 值可选对象
     */
    template<typename T>
    TOptional<T> GetConfigOptional(FName Name) const;

    /**
     * @brief 订阅键变更（响应式热调，零轮询）
     * @tparam T 支持类型
     * @param Name 配置键
     * @param OnChanged 变更回调（收到新值）
     * @return RAII 订阅守卫，业务方必须持有；析构守卫即退订
     */
    template<typename T>
    TSharedRef<FObserverSubscription> WatchConfig(FName Name, TFunction<void(const T&)> OnChanged);

    /**
     * @brief 查询配置键是否已注册
     * @param Name 配置键
     * @return 已注册返回 true
     */
    [[nodiscard]] bool IsRegistered(FName Name) const;

    /**
     * @brief 获取全局版本号（每次注册/覆盖自增）
     * @return 当前版本号
     */
    [[nodiscard]] uint32 GetVersion() const noexcept;

private:
    /**
     * @brief 注册默认值（仅未注册时写入）
     * @param Name 配置键
     * @param DefaultValue 默认值变体
     */
    void ApplyRegister(FName Name, FConfigVariant&& DefaultValue);

    /**
     * @brief 覆盖写入并广播订阅（只读键拒绝）
     * @param Name 配置键
     * @param NewValue 新值变体
     * @return 写入结果码
     */
    ESetConfigResult ApplySet(FName Name, FConfigVariant&& NewValue);

    /**
     * @brief 注册键订阅（懒创建订阅状态）
     * @param Name 配置键
     * @param InListener 类型擦除回调（变体入参，内部按类型提取）
     * @return RAII 订阅守卫
     */
    TSharedRef<FObserverSubscription> AddWatcher(FName Name, TFunction<void(const FConfigVariant&)> InListener);

    /**
     * @brief 组装类型不匹配告警文案
     * @tparam T 期望类型
     * @param Name 配置键
     * @param InValue 已注册值变体
     * @return 告警文案（含键名与实际/期望类型）
     */
    template<typename T>
    FString DescribeMismatch(FName Name, const FConfigVariant& InValue) const;

private:
    /**
     * @brief 取变体当前承载的类型名（诊断用）
     * @param InVariant 源变体
     * @return 类型名文案
     */
    static FString VariantTypeName(const FConfigVariant& InVariant);

private:
    /**
     * @brief 注册项数据结构（包含值变体、版本号及只读标记）
     */
    struct FEntry {
        FConfigVariant Value;
        uint32 Version = 0;
        bool bReadOnly = false;
    };

    /**
     * @brief 单键订阅状态（懒创建，随注册表生命周期常驻）
     */
    class FWatcherState;

    mutable FRWLock RwLock;
    TMap<FName, FEntry> Entries;
    TMap<FName, TSharedPtr<FWatcherState>> Watchers;
    uint32 GlobalVersion = 0;
};

template<typename T>
void FConfigRegistry::RegisterConfig(FName Name, T DefaultValue) {
    ApplyRegister(Name, FConfigVariant(TInPlaceType<T>{}, MoveTemp(DefaultValue)));
}

template<typename T>
ESetConfigResult FConfigRegistry::SetConfig(FName Name, T NewValue) {
    return ApplySet(Name, FConfigVariant(TInPlaceType<T>{}, MoveTemp(NewValue)));
}

template<typename T>
T FConfigRegistry::GetConfig(FName Name, T FallbackValue) const {
    return GetConfigOptional<T>(Name).Get(FallbackValue);
}

template<typename T>
TOptional<T> FConfigRegistry::GetConfigOptional(FName Name) const {
    FReadScopeLock ReadLock(RwLock);
    const FEntry* Entry = Entries.Find(Name);
    if (Entry == nullptr) {
        return TOptional<T>();
    }
    if (const T* Value = Game::Config::FindVariantValue<T>(Entry->Value)) {
        return TOptional<T>(*Value);
    }
    ensureAlwaysMsgf(false, TEXT("%s"), *DescribeMismatch<T>(Name, Entry->Value));
    return TOptional<T>();
}

template<typename T>
TSharedRef<FObserverSubscription> FConfigRegistry::WatchConfig(FName Name, TFunction<void(const T&)> OnChanged) {
    return AddWatcher(Name, [Callback = MoveTemp(OnChanged)](const FConfigVariant& InValue) {
        if (const T* Value = Game::Config::FindVariantValue<T>(InValue)) {
            Callback(*Value);
        }
    });
}

template<typename T>
FString FConfigRegistry::DescribeMismatch(FName Name, const FConfigVariant& InValue) const {
    const FConfigVariant ExpectedProbe(TInPlaceType<T>{}, T{});
    return FString::Printf(TEXT("【配置注册表】键 [%s] 类型不匹配：已注册类型 %s，本次期望类型 %s，已回退 Fallback。"),
        *Name.ToString(), *VariantTypeName(InValue), *VariantTypeName(ExpectedProbe));
}
