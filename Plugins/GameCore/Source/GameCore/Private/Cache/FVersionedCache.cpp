/**
 * @file FVersionedCache.cpp
 * @brief 缓存策略解析非模板实现（模板实现在 FVersionedCache.h）
 */

#include "Cache/FVersionedCache.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameCoreCache, Log, All);

namespace {

FName BuildCacheKey(const FName& Domain, const TCHAR* Suffix) {
    return FName(*FString::Printf(TEXT("Cache.%s.%s"), *Domain.ToString(), Suffix));
}

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

FCachePolicy FCachePolicy::Resolve(const FName& Domain) {
    FConfigRegistry& Registry = FConfigRegistry::Get();
    FCachePolicy Policy;
    Policy.Strategy = ParseStrategy(Registry.GetConfig<FString>(BuildCacheKey(Domain, TEXT("Strategy")), TEXT("auto")));
    Policy.LazyMinRows = Registry.GetConfig<int64>(BuildCacheKey(Domain, TEXT("LazyMinRows")), MAX_int64);
    Policy.LruMinRows = Registry.GetConfig<int64>(BuildCacheKey(Domain, TEXT("LruMinRows")), MAX_int64);
    Policy.LruCapacity = Registry.GetConfig<int64>(BuildCacheKey(Domain, TEXT("LruCapacity")), 10000);
    return Policy;
}
