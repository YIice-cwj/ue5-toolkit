/**
 * @file FObserverBase.cpp
 * @brief 统一监听基类实现：以配置中心作为启停开关唯一真源
 */

#include "Listener/FObserverBase.h"

/**
 * @brief 构造监听基类并注册启停开关配置键，Config 为启用状态唯一真源
 * @param InObserverName 监听器唯一名称
 */
FObserverBase::FObserverBase(const FName& InObserverName)
    : ObserverName(InObserverName),
      EnableConfigKey(BuildEnableConfigKey(InObserverName)) {
    ensureMsgf(!InObserverName.IsNone(), TEXT("FObserverBase should be constructed with a valid, non-None ObserverName."));
    FConfigRegistry& Registry = FConfigRegistry::Get();
    Registry.RegisterConfig<bool>(EnableConfigKey, true);
    bCachedEnabled.store(Registry.GetConfig<bool>(EnableConfigKey, true), std::memory_order_relaxed);
    EnableWatchSubscription = Registry.WatchConfig<bool>(
        EnableConfigKey,
        [this](const bool& bNewEnabled) {
            bCachedEnabled.store(bNewEnabled, std::memory_order_relaxed);
        }
    );
}

/**
 * @brief 析构函数：自动注销配置项监听守卫
 */
FObserverBase::~FObserverBase() {
    if (EnableWatchSubscription.IsValid()) {
        EnableWatchSubscription->Reset();
        EnableWatchSubscription.Reset();
    }
}

/**
 * @brief 设置启停开关（同步写穿至 FConfigRegistry 对应键）
 * @param bInEnabled 目标启用状态
 */
void FObserverBase::SetEnabled(bool bInEnabled) {
    bCachedEnabled.store(bInEnabled, std::memory_order_relaxed);
    FConfigRegistry::Get().SetConfig<bool>(EnableConfigKey, bInEnabled);
}

/**
 * @brief 广播前读取唯一启用真源，启用时递增版本号
 * @return 本次值提交需要通知订阅者返回 true
 */
bool FObserverBase::TryPrepareBroadcast() {
    if (!IsEnabled()) {
        return false;
    }
    Version.fetch_add(1, std::memory_order_relaxed);
    return true;
}

/**
 * @brief 根据名称生成标准开关键（Listener.{Name}.Enabled）
 * @param InObserverName 监听器名称
 * @return 格式化后的完整配置键
 */
FName FObserverBase::BuildEnableConfigKey(const FName& InObserverName) {
    return FName(*FString::Printf(TEXT("Listener.%s.Enabled"), *InObserverName.ToString()));
}

