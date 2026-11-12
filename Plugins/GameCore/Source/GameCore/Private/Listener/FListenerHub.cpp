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

/**
 * @brief 检查总线中是否没有任何已注册事件通道
 * @return 为空返回 true
 */
bool FListenerHub::IsEmpty() const {
    FReadScopeLock ReadLock(EventsLock);
    return Events.IsEmpty();
}

/**
 * @brief 检查指定名称的事件通道是否已存在
 * @param EventName 事件名
 * @return 存在返回 true
 */
bool FListenerHub::Contains(const FName& EventName) const {
    if (EventName.IsNone()) {
        return false;
    }
    FReadScopeLock ReadLock(EventsLock);
    return Events.Contains(EventName);
}

/**
 * @brief 检查指定事件通道当前是否有活跃监听者（供发射端在打包昂贵载荷前做短路优化）
 * @param EventName 事件名
 * @return 存在且有活跃监听者返回 true
 */
bool FListenerHub::HasListeners(const FName& EventName) const {
    if (EventName.IsNone()) {
        return false;
    }
    TSharedPtr<FEventBase> FoundEvent;
    {
        FReadScopeLock ReadLock(EventsLock);
        if (const TSharedPtr<FEventBase>* Found = Events.Find(EventName)) {
            FoundEvent = *Found;
        }
    }
    return FoundEvent.IsValid() && FoundEvent->NumListeners() > 0;
}

/**
 * @brief 获取指定事件通道的当前活跃监听者数量
 * @param EventName 事件名
 * @return 活跃监听者数量；通道不存在返回 0
 */
int32 FListenerHub::NumListeners(const FName& EventName) const {
    if (EventName.IsNone()) {
        return 0;
    }
    TSharedPtr<FEventBase> FoundEvent;
    {
        FReadScopeLock ReadLock(EventsLock);
        if (const TSharedPtr<FEventBase>* Found = Events.Find(EventName)) {
            FoundEvent = *Found;
        }
    }
    return FoundEvent.IsValid() ? FoundEvent->NumListeners() : 0;
}
