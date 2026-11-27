/**
 * @file UGameAbilitySystemGlobals.cpp
 * @brief 能力系统全局单例实现
 */

#include "Ability/UGameAbilitySystemGlobals.h"

#include "Damage/FGameGameplayEffectContext.h"

/**
 * @brief 分配本工程的打击反馈上下文（引擎虚函数契约要求返回堆分配裸指针，由 FGameplayEffectContextHandle 接管所有权）
 * @return 新建的 FGameGameplayEffectContext 实例
 */
FGameplayEffectContext* UGameAbilitySystemGlobals::AllocGameplayEffectContext() const {
    return new FGameGameplayEffectContext();
}
