/**
 * @file FObservableRegistry.cpp
 * @brief 任意类型观察者注册表非模板方法实现
 */

#include "Listener/FObservableRegistry.h"

namespace Game::Core {

DEFINE_LOG_CATEGORY(LogGameObservableRegistry);

} // namespace Game::Core

/**
 * @brief 注销指定 Key 的观察者
 * @param Key 数据键
 * @return 确实移除了已注册项返回 true
 */
bool Game::Core::FObservableRegistry::Unregister(const FName& Key) {
    FWriteScopeLock WriteLock(RegistryLock);
    return Entries.Remove(Key) > 0;
}

/**
 * @brief 查询指定 Key 是否已注册
 * @param Key 数据键
 * @return 已注册返回 true
 */
bool Game::Core::FObservableRegistry::Contains(const FName& Key) const {
    FReadScopeLock ReadLock(RegistryLock);
    return Entries.Contains(Key);
}

/**
 * @brief 当前已注册键数量
 * @return 键数量
 */
int32 Game::Core::FObservableRegistry::Num() const {
    FReadScopeLock ReadLock(RegistryLock);
    return Entries.Num();
}

/**
 * @brief 清空全部注册项
 */
void Game::Core::FObservableRegistry::Reset() {
    FWriteScopeLock WriteLock(RegistryLock);
    Entries.Reset();
}
