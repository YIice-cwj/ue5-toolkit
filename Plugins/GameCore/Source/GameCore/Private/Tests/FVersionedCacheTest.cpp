/**
 * @file FVersionedCacheTest.cpp
 * @brief 版本化缓存模块内自动化测试（LRU 淘汰引用计数兜底 / Eager 快照读 / 并发重建），Shipping 构建自动剔除
 */

#include "Cache/FVersionedCache.h"

#if WITH_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Misc/AutomationTest.h"

#include <atomic>

namespace {

/** 测试用缓存值：记录键与代次，用于区分重建前后的对象世代 */
struct FUnitTestCacheValue {
    FName Key;
    int32 Generation = 0;
};

constexpr int32 UnitTestKeyCount = 64;
constexpr int32 UnitTestLruCapacity = 16;
constexpr int32 UnitTestReaderWorkers = 16;
constexpr int32 UnitTestReaderMinIterations = 6000;
constexpr int32 UnitTestReaderMaxIterations = 40000;
constexpr int32 UnitTestWriterGenerations = 200;

TArray<FName> BuildUnitTestKeys() {
    TArray<FName> Keys;
    Keys.Reserve(UnitTestKeyCount);
    for (int32 Index = 0; Index < UnitTestKeyCount; ++Index) {
        Keys.Add(FName(*FString::Printf(TEXT("UnitTest.Cache.Key.%d"), Index)));
    }
    return Keys;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameCacheLruEvictionTest, "Game.Core.Cache.LruEvictionLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief LRU 淘汰生命周期：驱逐与重建期间已持有快照不得悬垂
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 *
 * 实现思路：容量 16 的 LRU 缓存灌入 64 键，先持有首键快照再触发全量驱逐，
 * 断言持有者因引用计数兜底仍存活且内容不被新一轮 Rebuild 污染
 */
bool FGameCacheLruEvictionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FConfigRegistry& Registry = FConfigRegistry::Get();
    static const FName Domain(TEXT("UnitTestCacheLru"));
    Registry.SetConfig<FString>(FName(TEXT("Cache.UnitTestCacheLru.Strategy")), TEXT("lru"));
    Registry.SetConfig<int64>(FName(TEXT("Cache.UnitTestCacheLru.LruCapacity")), UnitTestLruCapacity);

    FVersionedCache<FUnitTestCacheValue> Cache(Domain);
    const TArray<FName> AllKeys = BuildUnitTestKeys();
    Cache.Rebuild(AllKeys, [](FName Key) {
        return FUnitTestCacheValue{Key, 1};
    });
    TestEqual(TEXT("LRU 策略不预热，初始条目数为 0"), Cache.Num(), 0);
    TestFalse(TEXT("LRU 模式下未注册键返回空且不触发构建"), Cache.FindShared(FName(TEXT("UnitTest.Cache.Key.Missing"))).IsValid());
    TestEqual(TEXT("未注册键不污染缓存容量"), Cache.Num(), 0);

    const TSharedPtr<const FUnitTestCacheValue> HeldValue = Cache.FindShared(AllKeys[0]);
    TestTrue(TEXT("首次查找触发惰性构建"), HeldValue.IsValid());

    for (const FName& Key : AllKeys) {
        [[maybe_unused]] const auto DiscardedValue = Cache.FindShared(Key);
    }
    TestEqual(TEXT("条目数受容量上限约束"), Cache.Num(), UnitTestLruCapacity);
    TestTrue(TEXT("被驱逐快照因引用计数兜底仍存活"), HeldValue.IsValid());
    TestEqual(TEXT("驱逐后持有者内容完好"), HeldValue->Generation, 1);

    Cache.Rebuild(AllKeys, [](FName Key) {
        return FUnitTestCacheValue{Key, 2};
    });
    TestTrue(TEXT("Rebuild 期间持有者仍存活"), HeldValue.IsValid());
    TestEqual(TEXT("旧持有者内容不被新快照污染"), HeldValue->Generation, 1);

    const TSharedPtr<const FUnitTestCacheValue> RebuiltValue = Cache.FindShared(AllKeys[0]);
    TestTrue(TEXT("重建后可再次惰性构建"), RebuiltValue.IsValid());
    TestEqual(TEXT("重建后读到新一代对象"), RebuiltValue->Generation, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameCacheEagerFindSharedTest, "Game.Core.Cache.EagerFindShared",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief Eager 档：不可变快照全量预热 + FindShared 快照读取
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 *
 * 实现思路：显式声明 eager 策略后重建，断言全量预热条目数、命中快照内容与未命中空返回
 */
bool FGameCacheEagerFindSharedTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FConfigRegistry& Registry = FConfigRegistry::Get();
    static const FName Domain(TEXT("UnitTestCacheEager"));
    Registry.SetConfig<FString>(FName(TEXT("Cache.UnitTestCacheEager.Strategy")), TEXT("eager"));

    FVersionedCache<FUnitTestCacheValue> Cache(Domain);
    const TArray<FName> AllKeys = BuildUnitTestKeys();
    Cache.Rebuild(AllKeys, [](FName Key) {
        return FUnitTestCacheValue{Key, 1};
    });
    TestEqual(TEXT("Eager 策略全量预热"), Cache.Num(), UnitTestKeyCount);

    const TSharedPtr<const FUnitTestCacheValue> FoundValue = Cache.FindShared(AllKeys[UnitTestKeyCount - 1]);
    TestTrue(TEXT("FindShared 命中已预热键"), FoundValue.IsValid());
    if (FoundValue.IsValid()) {
        TestTrue(TEXT("命中快照键一致"), FoundValue->Key == AllKeys[UnitTestKeyCount - 1]);
        TestEqual(TEXT("命中快照代次一致"), FoundValue->Generation, 1);
    }
    TestFalse(TEXT("未命中返回空"), Cache.FindShared(FName(TEXT("UnitTest.Cache.Key.Missing"))).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameCacheEagerConcurrentRebuildTest, "Game.Core.Cache.EagerConcurrentRebuild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief Eager 档并发重建：写线程反复 Rebuild 与 16 读线程反复 FindShared 对撞
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 *
 * 实现思路：读者至少各跑 MIN 轮、写线程未收工时继续加压（合计远超十万次操作）；
 * 断言全程零崩溃、命中快照键与代次合法、并发重建期间旧持有者内容不被污染、结束后必命中新一代
 */
bool FGameCacheEagerConcurrentRebuildTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FConfigRegistry& Registry = FConfigRegistry::Get();
    static const FName Domain(TEXT("UnitTestCacheEagerConcurrent"));
    Registry.SetConfig<FString>(FName(TEXT("Cache.UnitTestCacheEagerConcurrent.Strategy")), TEXT("eager"));

    FVersionedCache<FUnitTestCacheValue> Cache(Domain);
    const TArray<FName> AllKeys = BuildUnitTestKeys();
    Cache.Rebuild(AllKeys, [](FName Key) {
        return FUnitTestCacheValue{Key, 1};
    });

    const FName TargetKey = AllKeys[0];
    const TSharedPtr<const FUnitTestCacheValue> HeldFromFirstGeneration = Cache.FindShared(TargetKey);
    TestTrue(TEXT("并发前持有第一代快照成功"), HeldFromFirstGeneration.IsValid());

    std::atomic<bool> bWriterFinished{false};
    std::atomic<int32> InvalidReadCount{0};
    std::atomic<int32> TotalReadCount{0};

    const TFuture<void> WriterFuture = Async(EAsyncExecution::Thread, [&Cache, &bWriterFinished, AllKeys]() {
        for (int32 Generation = 2; Generation <= 1 + UnitTestWriterGenerations; ++Generation) {
            Cache.Rebuild(AllKeys, [Generation](FName Key) {
                return FUnitTestCacheValue{Key, Generation};
            });
        }
        bWriterFinished.store(true);
    });

    ParallelFor(UnitTestReaderWorkers, [&Cache, &bWriterFinished, &InvalidReadCount, &TotalReadCount, TargetKey]([[maybe_unused]] int32 WorkerIndex) {
        int32 LocalIterations = 0;
        int32 LocalInvalidCount = 0;
        while (LocalIterations < UnitTestReaderMaxIterations
            && (!bWriterFinished.load() || LocalIterations < UnitTestReaderMinIterations)) {
            ++LocalIterations;
            const TSharedPtr<const FUnitTestCacheValue> Value = Cache.FindShared(TargetKey);
            if (!Value.IsValid() || Value->Key != TargetKey || Value->Generation < 1) {
                ++LocalInvalidCount;
            }
        }
        TotalReadCount.fetch_add(LocalIterations);
        if (LocalInvalidCount > 0) {
            InvalidReadCount.fetch_add(LocalInvalidCount);
        }
    });
    WriterFuture.Wait();

    TestTrue(TEXT("写线程已完成全部重建代次"), bWriterFinished.load());
    TestEqual(TEXT("并发读写期间读取结果始终合法（零崩溃/无空指针/无字段污染）"), InvalidReadCount.load(), 0);
    TestTrue(TEXT("读写合计操作数超过十万次"),
        TotalReadCount.load() + UnitTestWriterGenerations * UnitTestKeyCount >= 100000);
    TestTrue(TEXT("并发重建期间旧持有者仍存活"), HeldFromFirstGeneration.IsValid());
    TestEqual(TEXT("并发重建期间旧持有者内容未被污染"), HeldFromFirstGeneration->Generation, 1);

    const TSharedPtr<const FUnitTestCacheValue> LatestValue = Cache.FindShared(TargetKey);
    TestTrue(TEXT("重建结束后必命中新一代快照"), LatestValue.IsValid());
    if (LatestValue.IsValid()) {
        TestEqual(TEXT("最终代次为末次重建代次"), LatestValue->Generation, 1 + UnitTestWriterGenerations);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameCacheInvalidateTest, "Game.Core.Cache.InvalidateSingleKey",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证单 Key 精确失效能力（无需整表 Rebuild）
 */
bool FGameCacheInvalidateTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FConfigRegistry& Registry = FConfigRegistry::Get();
    static const FName Domain(TEXT("UnitTestCacheInvalidate"));
    Registry.SetConfig<FString>(FName(TEXT("Cache.UnitTestCacheInvalidate.Strategy")), TEXT("eager"));

    FVersionedCache<FUnitTestCacheValue> Cache(Domain);
    const FName KeyA(TEXT("KeyA"));
    const FName KeyB(TEXT("KeyB"));
    const TArray<FName> Keys = {KeyA, KeyB};

    Cache.Rebuild(Keys, [](FName Key) {
        return FUnitTestCacheValue{Key, 1};
    });

    TestEqual(TEXT("初始缓存条目数为 2"), Cache.Num(), 2);
    TestTrue(TEXT("KeyA 命中缓存"), Cache.FindShared(KeyA).IsValid());
    TestTrue(TEXT("KeyB 命中缓存"), Cache.FindShared(KeyB).IsValid());

    const bool bInvalidated = Cache.Invalidate(KeyA);
    TestTrue(TEXT("KeyA 精确失效成功"), bInvalidated);
    TestEqual(TEXT("失效后条目数减少为 1"), Cache.Num(), 1);
    TestFalse(TEXT("KeyA 不再命中缓存"), Cache.FindShared(KeyA).IsValid());
    TestTrue(TEXT("KeyB 依然有效"), Cache.FindShared(KeyB).IsValid());

    TestFalse(TEXT("重复失效已不存在的键返回 false"), Cache.Invalidate(KeyA));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameCacheGenerationGuardTest, "Game.Core.Cache.GenerationGuard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 代际守卫：惰性构建窗口内 Rebuild 时丢弃旧代构建结果并按新 BuildFn 重试一次
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 *
 * 实现思路：首次构建回调内触发 Rebuild 换新代，断言返回值为新代内容且构建恰好执行两次
 */
bool FGameCacheGenerationGuardTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FConfigRegistry& Registry = FConfigRegistry::Get();
    static const FName Domain(TEXT("UnitTestCacheGeneration"));
    Registry.SetConfig<FString>(FName(TEXT("Cache.UnitTestCacheGeneration.Strategy")), TEXT("lazy"));

    FVersionedCache<FUnitTestCacheValue> Cache(Domain);
    const TArray<FName> Keys = {FName(TEXT("Generation.Key"))};

    int32 BuildCount = 0;
    Cache.Rebuild(Keys, [&Cache, &Keys, &BuildCount](FName Key) {
        ++BuildCount;
        if (BuildCount == 1) {
            Cache.Rebuild(Keys, [&BuildCount](FName InnerKey) {
                ++BuildCount;
                return FUnitTestCacheValue{InnerKey, 2};
            });
        }
        return FUnitTestCacheValue{Key, 1};
    });

    const TSharedPtr<const FUnitTestCacheValue> Value = Cache.FindShared(Keys[0]);
    TestTrue(TEXT("代际竞态后仍返回有效值"), Value.IsValid());
    if (Value.IsValid()) {
        TestEqual(TEXT("旧代构建结果被丢弃，返回新代内容"), Value->Generation, 2);
    }
    TestEqual(TEXT("旧构建与重试各执行一次"), BuildCount, 2);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS

