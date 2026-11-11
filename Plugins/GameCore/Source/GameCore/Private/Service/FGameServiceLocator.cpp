/**
 * @file FGameServiceLocator.cpp
 * @brief 纯 C++ 线程安全服务定位器实现
 */

#include "Service/FGameServiceLocator.h"

/**
 * @brief 获取全局单例服务定位器
 * @return 引用实例
 */
FGameServiceLocator& FGameServiceLocator::Get() {
    static FGameServiceLocator Instance;
    return Instance;
}

/**
 * @brief 清空重置所有服务（供会话结束或单元测试清理）
 */
void FGameServiceLocator::Reset() {
    FWriteScopeLock WriteLock(Lock);
    TypeServices.Reset();
    NamedServices.Reset();
}

/**
 * @brief 获取当前注册的服务总数（类型服务 + 具名服务）
 * @return 服务数量
 */
int32 FGameServiceLocator::Num() const {
    FReadScopeLock ReadLock(Lock);
    return TypeServices.Num() + NamedServices.Num();
}
