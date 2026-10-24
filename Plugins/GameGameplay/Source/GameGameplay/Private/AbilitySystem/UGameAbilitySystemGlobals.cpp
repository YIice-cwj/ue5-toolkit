/**
 * @file UGameAbilitySystemGlobals.cpp
 * @brief 能力系统全局单例实现
 */

#include "AbilitySystem/UGameAbilitySystemGlobals.h"

#include "AbilitySystem/FGameGameplayEffectContext.h"

/**
 * @brief 分配本工程的打击反馈上下文
 * @return 新建的 FGameGameplayEffectContext（所有权经 FGameplayEffectContextHandle 或调用方 TUniquePtr 接管；
 *         裸 new 为引擎虚函数契约例外，见《代码规范(C++UE5)》17.1）
 */
FGameplayEffectContext* UGameAbilitySystemGlobals::AllocGameplayEffectContext() const {
    return new FGameGameplayEffectContext();
}
