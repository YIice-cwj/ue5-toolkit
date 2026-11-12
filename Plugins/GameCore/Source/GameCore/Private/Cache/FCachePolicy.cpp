/**
 * @file FCachePolicy.cpp
 * @brief 缓存策略解析实现（从 FConfigRegistry 读取域配置并解析生效策略）
 */

#include "Cache/FCachePolicy.h"

#include "Config/FConfigRegistry.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameCoreCache, Log, All);

namespace {

/**
 * @brief 拼接缓存域配置键（格式：Cache.{Domain}.{Suffix}）
 * @param Domain 缓存域名称
 * @param Suffix 配置项后缀
 * @return 完整配置键名
 */
FName BuildCacheKey(const FName& Domain, const TCHAR* Suffix) {
    return FName(*FString::Printf(TEXT("Cache.%s.%s"), *Domain.ToString(), Suffix));
}

/**
 * @brief 将字符串策略名称解析为 ECacheStrategy 枚举值
 * @param InStrategy 策略配置字符串
 * @return 解析后的策略枚举（未知字符串记录 Warning 并回退为 Automatic）
 */
ECacheStrategy ParseStrategy(const FString& InStrategy) {
    if (InStrategy.Equals(TEXT("auto"), ESearchCase::IgnoreCase) || InStrategy.Equals(TEXT("automatic"), ESearchCase::IgnoreCase)) {
        return ECacheStrategy::Automatic;
    }
    if (InStrategy.Equals(TEXT("eager"), ESearchCase::IgnoreCase)) {
        return ECacheStrategy::Eager;
    }
    if (InStrategy.Equals(TEXT("lazy"), ESearchCase::IgnoreCase)) {
        return ECacheStrategy::Lazy;
    }
    if (InStrategy.Equals(TEXT("lru"), ESearchCase::IgnoreCase)) {
        return ECacheStrategy::Lru;
    }
    UE_LOG(LogGameCoreCache, Warning, TEXT("【缓存策略】无法识别的策略配置 [%s]，回退为 automatic。"), *InStrategy);
    return ECacheStrategy::Automatic;
}

}  // namespace

/**
 * @brief 从 FConfigRegistry 读取指定业务域的缓存策略配置
 * @param Domain 缓存业务域名称
 * @return 解析并完成边界钳制后的缓存策略实例
 */
FCachePolicy FCachePolicy::Resolve(const FName& Domain) {
    ensureMsgf(!Domain.IsNone(), TEXT("FCachePolicy::Resolve should be called with a valid, non-None Domain."));
    FConfigRegistry& Registry = FConfigRegistry::Get();
    FCachePolicy Policy;
    Policy.Strategy = ParseStrategy(Registry.GetConfig<FString>(BuildCacheKey(Domain, TEXT("Strategy")), TEXT("auto")));
    Policy.LazyMinRows = FMath::Max<int64>(0, Registry.GetConfig<int64>(BuildCacheKey(Domain, TEXT("LazyMinRows")), MAX_int64));
    Policy.LruMinRows = FMath::Max<int64>(0, Registry.GetConfig<int64>(BuildCacheKey(Domain, TEXT("LruMinRows")), MAX_int64));
    const int64 RawCapacity = Registry.GetConfig<int64>(BuildCacheKey(Domain, TEXT("LruCapacity")), DefaultLruCapacity);
    Policy.LruCapacity = FMath::Clamp(RawCapacity, MinLruCapacity, MaxLruCapacity);
    return Policy;
}
