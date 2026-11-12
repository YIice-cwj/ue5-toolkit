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

/**
 * @brief 查询当前是否未注册任何服务
 * @return 无服务返回 true
 */
bool FGameServiceLocator::IsEmpty() const {
    FReadScopeLock ReadLock(Lock);
    return TypeServices.IsEmpty() && NamedServices.IsEmpty();
}

/**
 * @brief 获取所有已注册的具名服务名称列表
 * @param OutNames 接收具名服务名称的输出数组
 * @return 具名服务数量
 */
int32 FGameServiceLocator::GetNamedServiceNames(TArray<FName>& OutNames) const {
    FReadScopeLock ReadLock(Lock);
    NamedServices.GenerateKeyArray(OutNames);
    return OutNames.Num();
}
