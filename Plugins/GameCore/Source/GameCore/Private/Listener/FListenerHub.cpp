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
