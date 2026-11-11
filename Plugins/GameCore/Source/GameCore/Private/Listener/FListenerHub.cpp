/**
 * @file FListenerHub.cpp
 * @brief 中央监听总线非模板实现（模板成员实现在 FListenerHub.h）
 */

#include "Listener/FListenerHub.h"

DEFINE_LOG_CATEGORY(LogGameListenerHub);

/**
 * @brief 获取全局单例
 * @return 单例引用
 */
FListenerHub& FListenerHub::Get() {
    static FListenerHub Instance;
    return Instance;
}

/**
 * @brief 清空重置所有已注册事件与监听器
 */
void FListenerHub::Reset() {
    FWriteScopeLock WriteLock(EventsLock);
    Events.Reset();
}

/**
 * @brief 获取当前已注册的事件通道总数
 * @return 活跃事件通道数量
 */
int32 FListenerHub::Num() const {
    FReadScopeLock ReadLock(EventsLock);
    return Events.Num();
}
