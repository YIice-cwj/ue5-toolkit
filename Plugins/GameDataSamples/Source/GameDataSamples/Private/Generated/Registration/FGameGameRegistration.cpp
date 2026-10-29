/**
 * @file FGameGameRegistration.cpp
 * @brief 游戏层业务统一注册入口实现
 */
#include "Generated/Registration/FGameGameRegistration.h"

#include "Registration/FGameCoreRegistration.h"

/**
 * @brief 统一注册
 */
void FGameGameRegistration::RegisterAll([[maybe_unused]] UGameInstance* InGameInstance) {
    static const FName Domains[] = {
        FName(TEXT("Item")),
        FName(TEXT("Shop")),
    };
    for (const FName& Domain : Domains) {
        FGameCoreRegistration::RegisterCacheDomain(Domain);
    }
}

/**
 * @brief 统一注销（配置无生命周期，空实现保持统一入口对称）
 */
void FGameGameRegistration::UnregisterAll([[maybe_unused]] UGameInstance* InGameInstance) {
}
