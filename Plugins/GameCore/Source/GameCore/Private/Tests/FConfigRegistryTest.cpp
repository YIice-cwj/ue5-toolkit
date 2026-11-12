/**
 * @file FConfigRegistryTest.cpp
 * @brief 配置注册表模块内自动化测试（并发压测 / 响应式订阅 / 编译开关注册对账 / 只读拒绝 / 引擎类型），Shipping 构建自动剔除
 */

#include "Config/FConfigRegistry.h"
#include "Config/GameCompileSwitches.h"
#include "Config/FGameCoreRegistration.h"

#if WITH_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Tests/EnsureScope.h"

#include <atomic>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameConfigConcurrencyTest, "Game.Core.Config.Concurrency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 并发压测：16 工作线程高频纯值语义读取对撞后台写线程
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 *
 * 实现思路：写线程持续覆盖两个不同标量类型的键，读线程循环取 int32 值、bool 可选值与
 * 未注册键回退值；断言读取值始终合法（无撕裂、无类型错配、无死锁），验证读写分离正确性
 */
bool FGameConfigConcurrencyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FConfigRegistry::ClearWorldRegistriesForTesting();
    FConfigRegistry& Registry = FConfigRegistry::Get();
    static const FName ReadKey(TEXT("UnitTest.Config.Concurrency.Int"));
    static const FName FlagKey(TEXT("UnitTest.Config.Concurrency.Flag"));
    static const FName MissingKey(TEXT("UnitTest.Config.Concurrency.Missing"));
    Registry.RegisterConfig<int32>(ReadKey, 0);
    Registry.RegisterConfig<bool>(FlagKey, false);

    constexpr int32 ReaderWorkers = 16;
    constexpr int32 ReaderIterations = 6000;
    constexpr int32 WriterIterations = 3000;

    std::atomic<bool> bWriterFinished{false};
    std::atomic<int32> InvalidReadCount{0};

    const TFuture<void> WriterFuture = Async(EAsyncExecution::Thread, [&Registry, &bWriterFinished]() {
        for (int32 Index = 1; Index <= WriterIterations; ++Index) {
            Registry.SetConfig<int32>(ReadKey, Index);
            Registry.SetConfig<bool>(FlagKey, (Index % 2) == 0);
        }
        bWriterFinished.store(true);
    });

    ParallelFor(ReaderWorkers, [&Registry, &bWriterFinished, &InvalidReadCount]([[maybe_unused]] int32 WorkerIndex) {
        int32 LocalInvalidCount = 0;
        for (int32 Iteration = 0; Iteration < ReaderIterations; ++Iteration) {
            const int32 IntValue = Registry.GetConfig<int32>(ReadKey, -1);
            const TOptional<bool> FlagValue = Registry.GetConfigOptional<bool>(FlagKey);
            const int64 FallbackValue = Registry.GetConfig<int64>(MissingKey, -1);
            if (IntValue < 0 || !FlagValue.IsSet() || FallbackValue != -1) {
                ++LocalInvalidCount;
            }
        }
        if (LocalInvalidCount > 0) {
            InvalidReadCount.fetch_add(LocalInvalidCount);
        }
    });
    WriterFuture.Wait();

    TestTrue(TEXT("写线程已完成全部覆盖"), bWriterFinished.load());
    TestEqual(TEXT("并发读写期间读取值始终合法（无撕裂/无类型错配/无死锁）"), InvalidReadCount.load(), 0);
    TestEqual(TEXT("读线程结束后读到末次写入值"), Registry.GetConfig<int32>(ReadKey, -1), WriterIterations);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameConfigWatchTest, "Game.Core.Config.WatchConfig",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 响应式订阅：回调外派、回调内重入安全、守卫析构自动退订
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 *
 * 实现思路：订阅回调内同时执行读重入与异键写重入（验证 SetConfig 出锁广播不自杀锁），
 * 再销毁守卫触发退订并断言后续覆盖不再回调
 */
bool FGameConfigWatchTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FConfigRegistry& Registry = FConfigRegistry::Get();
    static const FName WatchKey(TEXT("UnitTest.Config.Watch.Value"));
    static const FName MirrorKey(TEXT("UnitTest.Config.Watch.Mirror"));
    Registry.RegisterConfig<int32>(WatchKey, 0);
    Registry.RegisterConfig<int32>(MirrorKey, 0);

    int32 LastSeenValue = 0;
    int32 CallbackCount = 0;
    {
        const TSharedRef<FObserverSubscription> Subscription = Registry.WatchConfig<int32>(WatchKey,
            [&Registry, &LastSeenValue, &CallbackCount](const int32& NewValue) {
                LastSeenValue = NewValue;
                ++CallbackCount;
                Registry.GetConfig<int32>(WatchKey, -1);
                Registry.SetConfig<int32>(MirrorKey, NewValue);
            });

        Registry.SetConfig<int32>(WatchKey, 42);
        TestEqual(TEXT("订阅者收到覆盖后的新值"), LastSeenValue, 42);
        TestEqual(TEXT("单次覆盖仅回调一次"), CallbackCount, 1);
        TestEqual(TEXT("回调内读重入无死锁"), Registry.GetConfig<int32>(WatchKey, -1), 42);
        TestEqual(TEXT("回调内异键写重入无死锁"), Registry.GetConfig<int32>(MirrorKey, -1), 42);
    }

    Registry.SetConfig<int32>(WatchKey, 100);
    TestEqual(TEXT("守卫析构后自动退订，不再回调"), CallbackCount, 1);
    TestEqual(TEXT("退订后值仍可正常覆盖"), Registry.GetConfig<int32>(WatchKey, -1), 100);

    const TSharedRef<FObserverSubscription> NullSubscription = Registry.WatchConfig<int32>(WatchKey, nullptr);
    TestFalse(TEXT("空回调订阅返回未激活的守卫"), NullSubscription->IsActive());

    FEnsureScope MismatchScope([](const FEnsureHandlerArgs& Args) {
        return FString(Args.Message).Contains(TEXT("float"));
    });
    const TSharedRef<FObserverSubscription> MismatchSubscription = Registry.WatchConfig<float>(WatchKey, [](const float&) {});
    TestFalse(TEXT("类型错配的订阅返回未激活的守卫"), MismatchSubscription->IsActive());
    TestEqual(TEXT("类型错配触发 ensure 告警"), MismatchScope.GetCount(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameCompileSwitchBridgeTest, "Game.Core.Config.CompileSwitchBridge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 注册对账：编译开关桥接初值必须与 Game::Config 镜像常量逐一相等
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 *
 * 实现思路：模块 StartupModule 已将编译态注入配置表，此处逐键对账宏/常量/键三列映射，
 * 并断言桥接键已注册，防止双源漂移
 */
bool FGameCompileSwitchBridgeTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FConfigRegistry& Registry = FConfigRegistry::Get();
    const FName EditorKey(TEXT("Env.IsEditor"));
    const FName TestKey(TEXT("Feature.Test"));
    const FName GmKey(TEXT("Feature.GM"));
    const FName DebugDrawKey(TEXT("Dev.DebugDraw"));
    const FName CpuProfilerKey(TEXT("Dev.CpuProfiler"));

    TestTrue(TEXT("Env.IsEditor 已由模块启动注册"), Registry.IsRegistered(EditorKey));
    TestTrue(TEXT("Feature.Test 已由模块启动注册"), Registry.IsRegistered(TestKey));
    TestTrue(TEXT("Feature.GM 已由模块启动注册"), Registry.IsRegistered(GmKey));
    TestTrue(TEXT("Dev.DebugDraw 已由模块启动注册"), Registry.IsRegistered(DebugDrawKey));
    TestTrue(TEXT("Dev.CpuProfiler 已由模块启动注册"), Registry.IsRegistered(CpuProfilerKey));

    TestTrue(TEXT("Env.IsEditor 初值与 IS_EDITOR 一致"),
        Registry.GetConfig<bool>(EditorKey, !Game::Config::bIsEditor) == Game::Config::bIsEditor);
    TestTrue(TEXT("Feature.Test 初值与 ENABLE_TEST 一致"),
        Registry.GetConfig<bool>(TestKey, !Game::Config::bEnableTest) == Game::Config::bEnableTest);
    TestTrue(TEXT("Feature.GM 初值与 ENABLE_GM 一致"),
        Registry.GetConfig<bool>(GmKey, !Game::Config::bEnableGM) == Game::Config::bEnableGM);
    TestTrue(TEXT("Dev.DebugDraw 初值与 ENABLE_DEBUG_DRAW 一致"),
        Registry.GetConfig<bool>(DebugDrawKey, !Game::Config::bEnableDebugDraw) == Game::Config::bEnableDebugDraw);
    TestTrue(TEXT("Dev.CpuProfiler 初值与 ENABLE_CPU_PROFILER 一致"),
        Registry.GetConfig<bool>(CpuProfilerKey, !Game::Config::bEnableCpuProfiler) == Game::Config::bEnableCpuProfiler);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameConfigReadOnlyRejectionTest, "Game.Core.Config.ReadOnlyRejection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 只读键拦截：Env.* 恒拒绝覆盖，返回码与 ensure 计数双断言
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 *
 * 实现思路：FEnsureScope 作用域收窄至单次 SetConfig（EnsureHandler 为 thread_local，仅拦截本线 ensure），
 * 返回码断言与原值回读断言均置于作用域之外，避免邻近新增 ensure 造成计数脆性
 */
bool FGameConfigReadOnlyRejectionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FConfigRegistry& Registry = FConfigRegistry::Get();
    const FName EditorKey(TEXT("Env.IsEditor"));
    const bool OriginalValue = Registry.GetConfig<bool>(EditorKey, true);

    ESetConfigResult WriteResult = ESetConfigResult::Applied;
    int32 CapturedEnsureCount = 0;
    {
        FEnsureScope Scope;
        WriteResult = Registry.SetConfig<bool>(EditorKey, !OriginalValue);
        CapturedEnsureCount = Scope.GetCount();
    }

    TestTrue(TEXT("只读键写入返回 RejectedReadOnly"), WriteResult == ESetConfigResult::RejectedReadOnly);
    TestEqual(TEXT("只读键拒绝通过返回码受控通知，不触发 ensure 崩溃上报"), CapturedEnsureCount, 0);
    TestTrue(TEXT("只读键取值保持原状"), Registry.GetConfig<bool>(EditorKey, true) == OriginalValue);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameConfigMathTypeTest, "Game.Core.Config.MathTypes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 引擎类型支持：FVector/FRotator/FGameplayTag 往返一致 + 类型不匹配告警文案含期望类型名
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 *
 * 实现思路：注册引擎类型键后按值回读比对（FGameplayTag 经标签管理器取真实非空标签）；
 * 再以 int32 键分别读取 FVector 与 FGameplayTag，用消息过滤型 FEnsureScope 捕获并断言
 * 告警文案含期望类型名（非匹配 ensure 不拦截）
 */
bool FGameConfigMathTypeTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FConfigRegistry& Registry = FConfigRegistry::Get();
    const FName VectorKey(TEXT("UnitTest.Config.Math.Vector"));
    const FName RotatorKey(TEXT("UnitTest.Config.Math.Rotator"));
    const FName TagKey(TEXT("UnitTest.Config.Math.Tag"));
    const FName IntKey(TEXT("UnitTest.Config.Math.Int"));
    const FVector VectorValue(1.5, -2.25, 3.0);
    const FRotator RotatorValue(10.0, 20.0, 30.0);
    const FGameplayTag TagValue = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Game.Core.UnitTest.ConfigTag")));
    Registry.RegisterConfig<FVector>(VectorKey, FVector::ZeroVector);
    Registry.RegisterConfig<FRotator>(RotatorKey, FRotator::ZeroRotator);
    Registry.RegisterConfig<FGameplayTag>(TagKey, FGameplayTag::EmptyTag);
    Registry.RegisterConfig<int32>(IntKey, 7);

    Registry.SetConfig<FVector>(VectorKey, VectorValue);
    Registry.SetConfig<FRotator>(RotatorKey, RotatorValue);
    Registry.SetConfig<FGameplayTag>(TagKey, TagValue);
    TestTrue(TEXT("FVector 往返一致"), Registry.GetConfig<FVector>(VectorKey, FVector::ZeroVector).Equals(VectorValue));
    TestTrue(TEXT("FRotator 往返一致"), Registry.GetConfig<FRotator>(RotatorKey, FRotator::ZeroRotator).Equals(RotatorValue));
    TestTrue(TEXT("FGameplayTag 往返一致"), Registry.GetConfig<FGameplayTag>(TagKey, FGameplayTag::EmptyTag) == TagValue);

    FEnsureScope MismatchScope([](const FEnsureHandlerArgs& Args) {
        return FString(Args.Message).Contains(TEXT("FVector"));
    });
    const FVector FallbackValue = Registry.GetConfig<FVector>(IntKey, FVector::ZeroVector);
    const int32 CapturedEnsureCount = MismatchScope.GetCount();

    TestTrue(TEXT("类型不匹配时回退 Fallback"), FallbackValue.Equals(FVector::ZeroVector));
    TestEqual(TEXT("类型不匹配告警文案含期望类型名"), CapturedEnsureCount, 1);

    FEnsureScope TagMismatchScope([](const FEnsureHandlerArgs& Args) {
        return FString(Args.Message).Contains(TEXT("FGameplayTag"));
    });
    const TOptional<FGameplayTag> TagOptionalValue = Registry.GetConfigOptional<FGameplayTag>(IntKey);
    const int32 CapturedTagEnsureCount = TagMismatchScope.GetCount();

    TestFalse(TEXT("FGameplayTag 类型不匹配时返回未设置"), TagOptionalValue.IsSet());
    TestEqual(TEXT("FGameplayTag 不匹配告警文案含期望类型名"), CapturedTagEnsureCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameConfigNotificationOrderTest, "Game.Core.Config.NotificationCommitOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证通知一阻塞时并发提交通知二仍按提交顺序完成回调
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameConfigNotificationOrderTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FConfigRegistry& Registry = FConfigRegistry::Get();
    const FName Key(TEXT("UnitTest.Config.NotificationCommitOrder"));
    Registry.RegisterConfig<int32>(Key, 0);

    FEvent* FirstEntered = FPlatformProcess::GetSynchEventFromPool(false);
    FEvent* ReleaseFirst = FPlatformProcess::GetSynchEventFromPool(false);
    FCriticalSection ReceivedMutex;
    TArray<int32> Received;
    const TSharedRef<FObserverSubscription> Subscription = Registry.WatchConfig<int32>(Key,
        [FirstEntered, ReleaseFirst, &ReceivedMutex, &Received](const int32& Value) {
            if (Value == 1) {
                FirstEntered->Trigger();
                ReleaseFirst->Wait(1000);
            }
            FScopeLock Lock(&ReceivedMutex);
            Received.Add(Value);
        });

    TFuture<void> Writer = Async(EAsyncExecution::Thread, [&Registry, Key]() {
        Registry.SetConfig<int32>(Key, 1);
    });
    TestTrue(TEXT("首个通知已进入回调"), FirstEntered->Wait(1000));
    Registry.SetConfig<int32>(Key, 2);
    ReleaseFirst->Trigger();
    Writer.Wait();

    TestEqual(TEXT("两次提交各回调一次"), Received.Num(), 2);
    if (Received.Num() == 2) {
        TestEqual(TEXT("通知一先于通知二完成"), Received[0], 1);
        TestEqual(TEXT("通知二保持提交值"), Received[1], 2);
    }

    FPlatformProcess::ReturnSynchEventToPool(FirstEntered);
    FPlatformProcess::ReturnSynchEventToPool(ReleaseFirst);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameConfigIdempotencyAndVersionTest, "Game.Core.Config.IdempotencyAndVersion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证注册默认值幂等性、全局版本号递增及未注册只读键写入拦截
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 */
bool FGameConfigIdempotencyAndVersionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FConfigRegistry& Registry = FConfigRegistry::Get();
    const FName Key(TEXT("UnitTest.Config.IdempotentKey"));

    const uint32 InitialVersion = Registry.GetVersion();

    Registry.RegisterConfig<int32>(Key, 100);
    TestEqual(TEXT("首次注册写入默认值"), Registry.GetConfig<int32>(Key, 0), 100);
    TestTrue(TEXT("首次注册导致版本号递增"), Registry.GetVersion() > InitialVersion);

    const uint32 VersionAfterFirstRegister = Registry.GetVersion();
    Registry.RegisterConfig<int32>(Key, 999);
    TestEqual(TEXT("重复注册默认值不覆盖已有值"), Registry.GetConfig<int32>(Key, 0), 100);
    TestEqual(TEXT("重复注册未变更数据时不递增版本号"), Registry.GetVersion(), VersionAfterFirstRegister);

    const uint32 VersionBeforeSet = Registry.GetVersion();
    Registry.SetConfig<int32>(Key, 200);
    TestEqual(TEXT("写入新值成功"), Registry.GetConfig<int32>(Key, 0), 200);
    TestEqual(TEXT("覆盖写入后版本号自增"), Registry.GetVersion(), VersionBeforeSet + 1);

    const FName UnregisteredReadOnlyKey(TEXT("Env.UnitTest.NeverRegisteredBefore"));
    ESetConfigResult RejectResult = ESetConfigResult::Applied;
    {
        FEnsureScope Scope;
        RejectResult = Registry.SetConfig<bool>(UnregisteredReadOnlyKey, true);
    }
    TestTrue(TEXT("未预先注册的 Env.* 只读键写入直接被拒绝"), RejectResult == ESetConfigResult::RejectedReadOnly);

    FGameCoreRegistration::RegisterAll(nullptr);
    FGameCoreRegistration::RegisterAll();

    const FName EmptyDomainName(NAME_None);
    FGameCoreRegistration::RegisterCacheDomain(EmptyDomainName);
    TestFalse(TEXT("空域名注册不生成非法配置项"), Registry.IsRegistered(FName(TEXT("Cache..Strategy"))));

    const FName DomainKey(TEXT("UnitTestRegDomain"));
    FGameCoreRegistration::RegisterCacheDomain(DomainKey);
    TestTrue(TEXT("注册域缓存策略默认键已生效"), Registry.IsRegistered(FName(TEXT("Cache.UnitTestRegDomain.Strategy"))));

    TestTrue(TEXT("无有效世界的 UObject 获取共享注册表回退到全局单例"),
        FConfigRegistry::GetShared(GetTransientPackage()) == FConfigRegistry::GetShared(nullptr));

    TestTrue(TEXT("全局注册表非空"), !Registry.IsEmpty());
    TestTrue(TEXT("已注册配置项总数大于 0"), Registry.Num() > 0);

    TArray<FName> AllKeys;
    const int32 KeyCount = Registry.GetRegisteredKeys(AllKeys);
    TestEqual(TEXT("GetRegisteredKeys 返回数量与 Num() 一致"), KeyCount, Registry.Num());
    TestTrue(TEXT("导出的键列表中包含指定测试键"), AllKeys.Contains(Key));

    return true;
}

#endif  // WITH_AUTOMATION_TESTS


