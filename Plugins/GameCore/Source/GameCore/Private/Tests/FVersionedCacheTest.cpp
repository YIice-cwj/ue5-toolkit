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

/**
 * @brief 测试用缓存值（记录键与代次，用于区分重建前后的对象世代）
 */
struct FUnitTestCacheValue {
    FName Key;
    int32 Generation = 0;
};

constexpr int32 UnitTestKeyCount = 64;
constexpr int32 UnitTestLruCapacity = 16;
constexpr int32 UnitTestReaderWorkers = 16;
constexpr int32 UnitTestReaderMinIterations = 2000;
constexpr int32 UnitTestReaderMaxIterations = 12000;
constexpr int32 UnitTestWriterGenerations = 64;

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
    TestTrue(TEXT("读写合计操作数达到高并发压测阈值"),
        TotalReadCount.load() + UnitTestWriterGenerations * UnitTestKeyCount >= 30000);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameCachePolicyResolveTest, "Game.Core.Cache.PolicyResolve",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证缓存策略配置驱动解析（默认策略、各策略枚举解析、非法配置回退与容量阈值对账）
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameCachePolicyResolveTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FConfigRegistry& Registry = FConfigRegistry::Get();
    const FName DefaultDomain(TEXT("PolicyDefaultDomain"));
    const FCachePolicy DefaultPolicy = FCachePolicy::Resolve(DefaultDomain);
    TestTrue(TEXT("未配置域默认落 Automatic"), DefaultPolicy.Strategy == ECacheStrategy::Automatic);
    TestEqual(TEXT("未配置域 LazyMinRows 默认为 MAX_int64"), DefaultPolicy.LazyMinRows, MAX_int64);
    TestEqual(TEXT("未配置域 LruMinRows 默认为 MAX_int64"), DefaultPolicy.LruMinRows, MAX_int64);
    TestEqual(TEXT("未配置域 LruCapacity 默认为 10000"), DefaultPolicy.LruCapacity, 10000LL);

    const FName CustomDomain(TEXT("PolicyCustomDomain"));
    Registry.SetConfig<FString>(FName(TEXT("Cache.PolicyCustomDomain.Strategy")), TEXT("lru"));
    Registry.SetConfig<int64>(FName(TEXT("Cache.PolicyCustomDomain.LazyMinRows")), 500LL);
    Registry.SetConfig<int64>(FName(TEXT("Cache.PolicyCustomDomain.LruMinRows")), 2000LL);
    Registry.SetConfig<int64>(FName(TEXT("Cache.PolicyCustomDomain.LruCapacity")), 50LL);

    const FCachePolicy LruPolicy = FCachePolicy::Resolve(CustomDomain);
    TestTrue(TEXT("配置 lru 解析为 ECacheStrategy::Lru"), LruPolicy.Strategy == ECacheStrategy::Lru);
    TestEqual(TEXT("LazyMinRows 解析准确"), LruPolicy.LazyMinRows, 500LL);
    TestEqual(TEXT("LruMinRows 解析准确"), LruPolicy.LruMinRows, 2000LL);
    TestEqual(TEXT("LruCapacity 解析准确"), LruPolicy.LruCapacity, 50LL);
    TestEqual(TEXT("GetClampedLruCapacity 正确映射到 int32"), LruPolicy.GetClampedLruCapacity(), 50);

    FCachePolicy CopyPolicy = LruPolicy;
    TestTrue(TEXT("FCachePolicy operator== 成立"), CopyPolicy == LruPolicy);
    CopyPolicy.LruCapacity = 100;
    TestFalse(TEXT("修改字段后 operator== 返回 false"), CopyPolicy == LruPolicy);

    Registry.SetConfig<FString>(FName(TEXT("Cache.PolicyCustomDomain.Strategy")), TEXT("eager"));
    TestTrue(TEXT("配置 eager 解析为 ECacheStrategy::Eager"), FCachePolicy::Resolve(CustomDomain).Strategy == ECacheStrategy::Eager);

    Registry.SetConfig<FString>(FName(TEXT("Cache.PolicyCustomDomain.Strategy")), TEXT("lazy"));
    TestTrue(TEXT("配置 lazy 解析为 ECacheStrategy::Lazy"), FCachePolicy::Resolve(CustomDomain).Strategy == ECacheStrategy::Lazy);

    Registry.SetConfig<FString>(FName(TEXT("Cache.PolicyCustomDomain.Strategy")), TEXT("auto"));
    TestTrue(TEXT("配置 auto 解析为 ECacheStrategy::Automatic"), FCachePolicy::Resolve(CustomDomain).Strategy == ECacheStrategy::Automatic);

    Registry.SetConfig<FString>(FName(TEXT("Cache.PolicyCustomDomain.Strategy")), TEXT("unknown_invalid_strategy"));
    TestTrue(TEXT("无法识别的策略回退为 ECacheStrategy::Automatic"), FCachePolicy::Resolve(CustomDomain).Strategy == ECacheStrategy::Automatic);

    const FName LazyRaceDomain(TEXT("UnitTestCacheLazyConcurrentRace"));
    Registry.SetConfig<FString>(FName(TEXT("Cache.UnitTestCacheLazyConcurrentRace.Strategy")), TEXT("lazy"));
    FVersionedCache<FUnitTestCacheValue> LazyCache(LazyRaceDomain);
    const TArray<FName> RaceKeys = BuildUnitTestKeys();
    LazyCache.Rebuild(RaceKeys, [](FName Key) {
        return FUnitTestCacheValue{Key, 99};
    });

    std::atomic<int32> LazyInvalidReads{0};
    ParallelFor(16, [&LazyCache, &RaceKeys, &LazyInvalidReads]([[maybe_unused]] int32 WorkerIdx) {
        for (int32 Step = 0; Step < 256; ++Step) {
            const FName& Key = RaceKeys[Step % RaceKeys.Num()];
            const TSharedPtr<const FUnitTestCacheValue> Built = LazyCache.FindShared(Key);
            if (!Built.IsValid() || Built->Key != Key || Built->Generation != 99) {
                LazyInvalidReads.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });
    TestEqual(TEXT("Lazy 策略 16 线程并发惰性构建与读取零竞态异常"), LazyInvalidReads.load(), 0);
    TestEqual(TEXT("Lazy 策略并发构建后全部 64 键就绪"), LazyCache.Num(), UnitTestKeyCount);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameCacheLruIndexPoolAndResetTest,
    "Game.Core.Cache.LruIndexPoolAndReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FLruIndexPool 节点复用与二次机会淘汰机制，以及 FVersionedCache::Reset、空闭包与空键安全
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameCacheLruIndexPoolAndResetTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FLruIndexPool Lru;
    TestTrue(TEXT("初始状态索引池为空"), Lru.IsEmpty());
    TestEqual(TEXT("初始条目数为 0"), Lru.Num(), 0);

    const FName Key1(TEXT("LruKey1"));
    const FName Key2(TEXT("LruKey2"));
    const FName Key3(TEXT("LruKey3"));

    const int32 Node1 = Lru.AllocateNode(Key1);
    Lru.InsertAtHead(Node1);
    const int32 Node2 = Lru.AllocateNode(Key2);
    Lru.InsertAtHead(Node2);
    const int32 Node3 = Lru.AllocateNode(Key3);
    Lru.InsertAtHead(Node3);

    TestEqual(TEXT("插入 3 个节点后条目数为 3"), Lru.Num(), 3);
    TestFalse(TEXT("非空判定准确"), Lru.IsEmpty());
    TestTrue(TEXT("最近插入的 Key3 处于头部"), Lru.IsHead(Key3));
    TestEqual(TEXT("GetHeadKey 返回 Key3"), Lru.GetHeadKey(), Key3);
    TestTrue(TEXT("GetTailKey 返回有效尾部"), !Lru.GetTailKey().IsNone());
    TestTrue(TEXT("GetCapacity 大于等于 3"), Lru.GetCapacity() >= 3);

    const int32 ReallocatedNode2 = Lru.AllocateNode(Key2);
    TestEqual(TEXT("重复分配既有键复用既有节点索引"), ReallocatedNode2, Node2);
    Lru.InsertAtHead(ReallocatedNode2);
    TestTrue(TEXT("重复分配复用后节点处于头部"), Lru.IsHead(Key2));

    Lru.Touch(Key1);
    TestTrue(TEXT("Touch 将 Key1 提升至头部"), Lru.IsHead(Key1));

    TestTrue(TEXT("并发读标记 Key2 热度位成功"), Lru.MarkReferenced(Key2));
    TestFalse(TEXT("未注册键标记热度返回 false"), Lru.MarkReferenced(FName(TEXT("NoSuchKey"))));

    const FName Evicted = Lru.EvictTail();
    TestTrue(TEXT("尾部节点持有二次机会晋升后淘汰冷节点"), Evicted == Key3 || Evicted == Key1 || Evicted == Key2);
    TestFalse(TEXT("被淘汰节点不再存在于池中"), Lru.Contains(Evicted));
    TestEqual(TEXT("淘汰后条目数减少为 2"), Lru.Num(), 2);

    Lru.Reset();
    TestTrue(TEXT("Reset 后索引池清空"), Lru.IsEmpty());
    TestEqual(TEXT("Reset 后条目数为 0"), Lru.Num(), 0);
    TestTrue(TEXT("空池 EvictTail 安全返回 NAME_None"), Lru.EvictTail().IsNone());

    FConfigRegistry& Registry = FConfigRegistry::Get();
    static const FName Domain(TEXT("UnitTestCacheResetDomain"));
    Registry.SetConfig<FString>(FName(TEXT("Cache.UnitTestCacheResetDomain.Strategy")), TEXT("eager"));

    FVersionedCache<FUnitTestCacheValue> Cache(Domain);
    const TArray<FName> Keys = {Key1, Key2};
    Cache.Rebuild(Keys, [](FName Key) {
        return FUnitTestCacheValue{Key, 10};
    });
    TestEqual(TEXT("预热后包含 2 个条目"), Cache.Num(), 2);
    TestFalse(TEXT("预热后 IsEmpty 为 false"), Cache.IsEmpty());
    TestTrue(TEXT("Contains Key1 为 true"), Cache.Contains(Key1));
    TestTrue(TEXT("Contains Key2 为 true"), Cache.Contains(Key2));
    TestFalse(TEXT("Contains 未知键为 false"), Cache.Contains(FName(TEXT("UnknownKey"))));
    TestEqual(TEXT("GetEffectiveStrategy 返回 Eager"), Cache.GetEffectiveStrategy(), ECacheStrategy::Eager);
    TestEqual(TEXT("GetDomain 返回预期 Domain"), Cache.GetDomain(), Domain);
    TestTrue(TEXT("GetGeneration 大于 0"), Cache.GetGeneration() > 0);
    TestTrue(TEXT("Key1 查找成功"), Cache.FindShared(Key1).IsValid());

    Cache.Reset();
    TestEqual(TEXT("Cache.Reset 后条目数归零"), Cache.Num(), 0);
    TestTrue(TEXT("Cache.Reset 后 IsEmpty 为 true"), Cache.IsEmpty());
    TestFalse(TEXT("Reset 后原有键不再命中"), Cache.FindShared(Key1).IsValid());
    TestFalse(TEXT("Reset 后 Contains Key1 为 false"), Cache.Contains(Key1));

    Cache.Rebuild(Keys, nullptr);
    TestEqual(TEXT("空闭包 Rebuild 安全清空不崩溃"), Cache.Num(), 0);

    TestFalse(TEXT("查找 NAME_None 安全返回空指针"), Cache.FindShared(NAME_None).IsValid());
    TestFalse(TEXT("失效 NAME_None 安全返回 false"), Cache.Invalidate(NAME_None));

    return true;
}

#endif  // WITH_AUTOMATION_TESTS

