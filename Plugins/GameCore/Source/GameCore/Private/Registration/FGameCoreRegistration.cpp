/**
 * @file FGameCoreRegistration.cpp
 * @brief GameCore 统一注册类实现
 */

#include "Registration/FGameCoreRegistration.h"

#include "Config/FConfigRegistry.h"
#include "Config/GameCompileSwitches.h"

namespace {

void RegisterCompileSwitches(FConfigRegistry& Registry) {
    Registry.RegisterConfig<bool>(FName(TEXT("Env.IsEditor")), Game::Config::bIsEditor);
    Registry.RegisterConfig<bool>(FName(TEXT("Feature.Test")), Game::Config::bEnableTest);
    Registry.RegisterConfig<bool>(FName(TEXT("Feature.GM")), Game::Config::bEnableGM);
    Registry.RegisterConfig<bool>(FName(TEXT("Dev.DebugDraw")), Game::Config::bEnableDebugDraw);
}

void RegisterCacheDomainDefaults(FConfigRegistry& Registry, const FName& Domain) {
    const FString KeyPrefix = FString::Printf(TEXT("Cache.%s."), *Domain.ToString());
    Registry.RegisterConfig<FString>(FName(*(KeyPrefix + TEXT("Strategy"))), TEXT("auto"));
    Registry.RegisterConfig<int64>(FName(*(KeyPrefix + TEXT("LazyMinRows"))), MAX_int64);
    Registry.RegisterConfig<int64>(FName(*(KeyPrefix + TEXT("LruMinRows"))), MAX_int64);
    Registry.RegisterConfig<int64>(FName(*(KeyPrefix + TEXT("LruCapacity"))), 10000);
}

}  // namespace

void FGameCoreRegistration::RegisterAll([[maybe_unused]] UGameInstance* InGameInstance) {
    RegisterCompileSwitches(FConfigRegistry::Get());
}

void FGameCoreRegistration::UnregisterAll([[maybe_unused]] UGameInstance* InGameInstance) {
}

void FGameCoreRegistration::RegisterCacheDomain(const FName& Domain) {
    RegisterCacheDomainDefaults(FConfigRegistry::Get(), Domain);
}
