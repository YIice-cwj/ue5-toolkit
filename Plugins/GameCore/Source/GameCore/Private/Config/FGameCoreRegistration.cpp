/**
 * @file FGameCoreRegistration.cpp
 * @brief GameCore 统一注册类实现
 */

#include "Config/FGameCoreRegistration.h"

#include "Cache/FCachePolicy.h"
#include "Config/FConfigRegistry.h"
#include "Config/GameCompileSwitches.h"
#include "Engine/GameInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameCoreRegistration, Log, All);

namespace {

/**
 * @brief 注册核心编译期开关到运行时配置表
 * @param Registry 目标配置中心实例
 */
void RegisterCompileSwitches(FConfigRegistry& Registry) {
    Registry.RegisterConfig<bool>(FName(TEXT("Env.IsEditor")), Game::Config::bIsEditor);
    Registry.RegisterConfig<bool>(FName(TEXT("Feature.Test")), Game::Config::bEnableTest);
    Registry.RegisterConfig<bool>(FName(TEXT("Feature.GM")), Game::Config::bEnableGM);
    Registry.RegisterConfig<bool>(FName(TEXT("Dev.DebugDraw")), Game::Config::bEnableDebugDraw);
    Registry.RegisterConfig<bool>(FName(TEXT("Dev.CpuProfiler")), Game::Config::bEnableCpuProfiler);
}

/**
 * @brief 注册指定业务域的缓存策略默认配置项
 * @param Registry 目标配置中心实例
 * @param Domain 业务域名称（为空时记录告警并忽略）
 */
void RegisterCacheDomainDefaults(FConfigRegistry& Registry, const FName& Domain) {
    if (Domain.IsNone()) {
        UE_LOG(LogGameCoreRegistration, Warning, TEXT("FGameCoreRegistration::RegisterCacheDomain ignored: Domain is None."));
        return;
    }
    const FString KeyPrefix = FString::Printf(TEXT("Cache.%s."), *Domain.ToString());
    Registry.RegisterConfig<FString>(FName(*(KeyPrefix + TEXT("Strategy"))), TEXT("auto"));
    Registry.RegisterConfig<int64>(FName(*(KeyPrefix + TEXT("LazyMinRows"))), MAX_int64);
    Registry.RegisterConfig<int64>(FName(*(KeyPrefix + TEXT("LruMinRows"))), MAX_int64);
    Registry.RegisterConfig<int64>(FName(*(KeyPrefix + TEXT("LruCapacity"))), FCachePolicy::DefaultLruCapacity);
}

}  // namespace

/**
 * @brief 注册编译开关桥接键（幂等；当传入非空 InGameInstance 时同时向该 PIE 实例对应的世界隔离配置表注册）
 * @param InGameInstance 当前 GameInstance（可空，模块启动阶段传 nullptr 注册全局根表）
 */
void FGameCoreRegistration::RegisterAll(UGameInstance* InGameInstance) {
    RegisterCompileSwitches(FConfigRegistry::Get());
    if (InGameInstance != nullptr) {
        RegisterCompileSwitches(FConfigRegistry::Get(InGameInstance));
    }
}

/**
 * @brief 注册某业务域的缓存策略默认键（Cache.{Domain}.*，幂等；Domain 为 None 时记录 Warning 并忽略）
 * @param Domain 业务域名称（由游戏层提供，框架层不感知具体域名）
 */
void FGameCoreRegistration::RegisterCacheDomain(const FName& Domain) {
    RegisterCacheDomainDefaults(FConfigRegistry::Get(), Domain);
}
