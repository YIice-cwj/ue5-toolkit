// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameLoggingSubsystemTest.cpp
 * @brief UGameLoggingSubsystem 生命周期、门面配置、无监听者丢弃快路径、退出期尾日志刷新与防重入测试
 */

#include "Remote/GameLoggingSubsystem.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Types/TCancelableScopeGuard.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoggingSubsystemLifecycleAndFacadeTest,
    "Game.Logging.Subsystem.LifecycleAndFacade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 CDO 零分配、Get/GetPtr、门面配置契约、多批次循环 FlushQueue 与端到端 GLog 总线广播
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameLoggingSubsystemLifecycleAndFacadeTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const UGameLoggingSubsystem* DefaultSubsystem = GetDefault<UGameLoggingSubsystem>();
    TestNotNull(TEXT("CDO 实例应有效"), DefaultSubsystem);
    if (DefaultSubsystem != nullptr) {
        TestEqual(TEXT("CDO 排队数应为 0（零堆分配）"), DefaultSubsystem->GetQueuedCount(), 0);
        TestEqual(TEXT("CDO 默认阈值应为 Warning"), DefaultSubsystem->GetMinVerbosity(), ELogVerbosity::Warning);
    }

    TestNull(TEXT("GetPtr(nullptr) 应返回 nullptr"), UGameLoggingSubsystem::GetPtr(nullptr));

    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameLoggingSubsystemFacadeWorld")));
    const auto ShutdownGuard = MakeCancelableScopeGuard([GameInstance]() {
        GameInstance->Shutdown();
        GameInstance->MarkAsGarbage();
    });

    UGameLoggingSubsystem* LoggingSubsystem = UGameLoggingSubsystem::GetPtr(GameInstance);
    TestNotNull(TEXT("GameInstance 应成功创建 UGameLoggingSubsystem"), LoggingSubsystem);
    UGameplayMessageSubsystem* MessageBus = UGameplayMessageSubsystem::GetPtr(GameInstance);
    TestNotNull(TEXT("GameInstance 应具备 UGameplayMessageSubsystem"), MessageBus);
    if (LoggingSubsystem == nullptr || MessageBus == nullptr) {
        return false;
    }

    TestEqual(TEXT("Get(GameInstance) 应返回同一子系统实例"), &UGameLoggingSubsystem::Get(GameInstance), LoggingSubsystem);
    TestEqual(TEXT("GetPtr(OtherSubsystem) 应解析到同一实例"), UGameLoggingSubsystem::GetPtr(MessageBus), LoggingSubsystem);
    TestEqual(TEXT("GetPtr(World) 应通过 GEngine 解析到同一实例"), UGameLoggingSubsystem::GetPtr(GameInstance->GetWorld()), LoggingSubsystem);
    TestNull(TEXT("无 World 关联对象(GEngine)传入 GetPtr 应返回 nullptr"), UGameLoggingSubsystem::GetPtr(GEngine));
    TestNull(TEXT("CDO 子系统传入 GetPtr 应返回 nullptr"), UGameLoggingSubsystem::GetPtr(DefaultSubsystem));

    TestTrue(TEXT("默认状态下应开启远程日志采集"), LoggingSubsystem->IsRemoteLoggingEnabled());
    TestEqual(TEXT("默认最低采集级别应为 Warning"), LoggingSubsystem->GetMinVerbosity(), ELogVerbosity::Warning);
    TestEqual(TEXT("默认单帧排空上限应为 50"), LoggingSubsystem->GetMaxDrainBatchPerTick(), 50);
    TestEqual(TEXT("默认队列容量上限应等于 MAX_CAPACITY"), LoggingSubsystem->GetMaxQueueCapacity(), FGameRemoteLogOutputDevice::MAX_CAPACITY);

    LoggingSubsystem->SetMaxDrainBatchPerTick(0);
    LoggingSubsystem->SetMaxDrainBatchPerTick(-5);
    TestEqual(TEXT("非正单帧排空上限应被忽略"), LoggingSubsystem->GetMaxDrainBatchPerTick(), 50);

    LoggingSubsystem->SetMaxDrainBatchPerTick(20);
    TestEqual(TEXT("单帧排空上限应已更新为 20"), LoggingSubsystem->GetMaxDrainBatchPerTick(), 20);

    LoggingSubsystem->SetMaxQueueCapacity(0);
    TestEqual(TEXT("非正队列容量应被忽略"), LoggingSubsystem->GetMaxQueueCapacity(), FGameRemoteLogOutputDevice::MAX_CAPACITY);

    LoggingSubsystem->SetMaxQueueCapacity(FGameRemoteLogOutputDevice::MAX_CAPACITY * 2);
    TestEqual(TEXT("超限队列容量应钳制至 MAX_CAPACITY"), LoggingSubsystem->GetMaxQueueCapacity(), FGameRemoteLogOutputDevice::MAX_CAPACITY);

    const FName IgnoredTestCat(TEXT("LogSubsystemIgnoredTest"));
    LoggingSubsystem->AddIgnoredCategory(IgnoredTestCat);
    TestTrue(TEXT("门面添加黑名单分类应生效"), LoggingSubsystem->IsCategoryIgnored(IgnoredTestCat));
    LoggingSubsystem->RemoveIgnoredCategory(IgnoredTestCat);
    TestFalse(TEXT("门面移除黑名单分类应生效"), LoggingSubsystem->IsCategoryIgnored(IgnoredTestCat));

    LoggingSubsystem->SetRemoteLoggingEnabled(true);
    LoggingSubsystem->SetRemoteLoggingEnabled(false);
    TestFalse(TEXT("应成功关闭远程日志采集"), LoggingSubsystem->IsRemoteLoggingEnabled());

    LoggingSubsystem->SetMinVerbosity(ELogVerbosity::Display);
    TestEqual(TEXT("门面最低级别应更新为 Display"), LoggingSubsystem->GetMinVerbosity(), ELogVerbosity::Display);
    LoggingSubsystem->ClearQueue();

    GLog->Serialize(TEXT("DisabledShouldNotCapture"), ELogVerbosity::Display, FName(TEXT("LogSubsystemTestCategory")), 10.0);
    TestEqual(TEXT("关闭远程日志采集期间不得截获 GLog"), LoggingSubsystem->GetQueuedCount(), 0);

    LoggingSubsystem->SetRemoteLoggingEnabled(true);
    TestTrue(TEXT("应成功重新开启远程日志采集"), LoggingSubsystem->IsRemoteLoggingEnabled());
    LoggingSubsystem->ClearQueue();

    int32 ReceivedCount = 0;
    FGameRemoteLogEntry LastReceivedEntry;
    FGameplayMessageListenerHandle BusHandle = MessageBus->RegisterListener<FGameRemoteLogEntry>(
        TAG_Game_Log_Remote,
        [&ReceivedCount, &LastReceivedEntry](const FGameRemoteLogEntry& Payload) {
            ++ReceivedCount;
            LastReceivedEntry = Payload;
        }
    );
    TestTrue(TEXT("消息总线监听句柄应有效"), BusHandle.IsValid());

    GLog->Serialize(
        TEXT("BroadcastDisplayPayload"),
        ELogVerbosity::Display,
        FName(TEXT("LogSubsystemTestCategory")),
        314.159
    );

    const int32 FlushedCount = LoggingSubsystem->FlushQueue();
    TestEqual(TEXT("立即排空应返回 1 条日志"), FlushedCount, 1);
    TestEqual(TEXT("总线监听器应收到 1 次广播"), ReceivedCount, 1);
    TestEqual(TEXT("广播条目分类断言"), LastReceivedEntry.Category, FName(TEXT("LogSubsystemTestCategory")));
    TestEqual(TEXT("广播条目级别断言"), LastReceivedEntry.GetVerbosity(), ELogVerbosity::Display);
    TestEqual(TEXT("广播条目正文断言"), LastReceivedEntry.Message, FString(TEXT("BroadcastDisplayPayload")));
    TestEqual(TEXT("广播条目时间戳断言"), LastReceivedEntry.Timestamp, 314.159);

    ReceivedCount = 0;
    for (int32 Index = 0; Index < 65; ++Index) {
        GLog->Serialize(TEXT("MultiBatchFlushItem"), ELogVerbosity::Display, FName(TEXT("LogSubsystemBatchTest")), 500.0 + Index);
    }
    const int32 MultiFlushed = LoggingSubsystem->FlushQueue();
    TestEqual(TEXT("超过单帧批大小(20)的 65 条日志应由 FlushQueue 一次循环排空"), MultiFlushed, 65);
    TestEqual(TEXT("监听器应精确收到全部 65 次广播"), ReceivedCount, 65);
    TestEqual(TEXT("排空后剩余队列为 0"), LoggingSubsystem->GetQueuedCount(), 0);
    TestEqual(TEXT("空队列 FlushQueue 应返回 0"), LoggingSubsystem->FlushQueue(), 0);

    BusHandle.Unregister();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoggingSubsystemNoListenerAndShutdownTest,
    "Game.Logging.Subsystem.NoListenerAndDeinitializeFlush",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证无监听者零分配丢弃快路径、Deinitialize 尾日志刷新与完整 GameInstance 卸载安全性
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameLoggingSubsystemNoListenerAndShutdownTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameLoggingShutdownFlushWorld")));
    const auto ShutdownGuard = MakeCancelableScopeGuard([GameInstance]() {
        GameInstance->Shutdown();
        GameInstance->MarkAsGarbage();
    });

    UGameLoggingSubsystem* LoggingSubsystem = UGameLoggingSubsystem::GetPtr(GameInstance);
    UGameplayMessageSubsystem* MessageBus = UGameplayMessageSubsystem::GetPtr(GameInstance);
    TestNotNull(TEXT("UGameLoggingSubsystem 有效"), LoggingSubsystem);
    TestNotNull(TEXT("UGameplayMessageSubsystem 有效"), MessageBus);
    if (LoggingSubsystem == nullptr || MessageBus == nullptr) {
        return false;
    }

    LoggingSubsystem->SetMinVerbosity(ELogVerbosity::Display);
    LoggingSubsystem->ClearQueue();

    GLog->Serialize(TEXT("NoListenerDiscardItem1"), ELogVerbosity::Display, FName(TEXT("LogNoListenerTest")), 1.0);
    GLog->Serialize(TEXT("NoListenerDiscardItem2"), ELogVerbosity::Display, FName(TEXT("LogNoListenerTest")), 2.0);
    TestEqual(TEXT("无监听者前已入队 2 条"), LoggingSubsystem->GetQueuedCount(), 2);

    const int32 Discarded = LoggingSubsystem->FlushQueue();
    TestEqual(TEXT("无监听者快速路径应直接丢弃全部 2 条"), Discarded, 2);
    TestEqual(TEXT("丢弃后队列应为 0"), LoggingSubsystem->GetQueuedCount(), 0);

    int32 DeinitReceived = 0;
    FString DeinitLastMsg;
    FGameplayMessageListenerHandle BusHandle = MessageBus->RegisterListener<FGameRemoteLogEntry>(
        TAG_Game_Log_Remote,
        [&DeinitReceived, &DeinitLastMsg](const FGameRemoteLogEntry& Payload) {
            if (Payload.Category == FName(TEXT("LogShutdownFlushTest"))) {
                ++DeinitReceived;
                DeinitLastMsg = Payload.Message;
            }
        }
    );

    GLog->Serialize(TEXT("FinalLogBeforeDeinit"), ELogVerbosity::Display, FName(TEXT("LogShutdownFlushTest")), 99.0);
    TestEqual(TEXT("Deinitialize 前队列应积压 1 条尾日志"), LoggingSubsystem->GetQueuedCount(), 1);

    LoggingSubsystem->Deinitialize();
    TestEqual(TEXT("Deinitialize 时总线尚存则应自动将尾日志刷新送达监听器"), DeinitReceived, 1);
    TestEqual(TEXT("送达的尾日志正文应完全匹配"), DeinitLastMsg, FString(TEXT("FinalLogBeforeDeinit")));
    TestEqual(TEXT("Deinitialize 后队列计数应归零"), LoggingSubsystem->GetQueuedCount(), 0);

    GLog->Serialize(TEXT("AfterDeinitShouldNotCapture"), ELogVerbosity::Display, FName(TEXT("LogShutdownFlushTest")), 100.0);
    TestEqual(TEXT("Deinitialize 解挂后不得再截获后续 GLog 日志"), LoggingSubsystem->GetQueuedCount(), 0);

    BusHandle.Unregister();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoggingSubsystemFeedbackLoopGuardTest,
    "Game.Logging.Subsystem.FeedbackLoopAndReentrantFlushGuard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证总线监听者内部写日志与递归调用 FlushQueue 时，重入守卫能彻底阻断自激雪崩与迭代器失效
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameLoggingSubsystemFeedbackLoopGuardTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameLoggingLoopGuardWorld")));
    const auto ShutdownGuard = MakeCancelableScopeGuard([GameInstance]() {
        GameInstance->Shutdown();
        GameInstance->MarkAsGarbage();
    });

    UGameLoggingSubsystem* LoggingSubsystem = UGameLoggingSubsystem::GetPtr(GameInstance);
    UGameplayMessageSubsystem* MessageBus = UGameplayMessageSubsystem::GetPtr(GameInstance);
    TestNotNull(TEXT("UGameLoggingSubsystem 有效"), LoggingSubsystem);
    TestNotNull(TEXT("UGameplayMessageSubsystem 有效"), MessageBus);
    if (LoggingSubsystem == nullptr || MessageBus == nullptr) {
        return false;
    }

    LoggingSubsystem->SetMinVerbosity(ELogVerbosity::Display);
    LoggingSubsystem->ClearQueue();

    int32 ReceivedBroadcasts = 0;
    int32 ReentrantFlushResult = -1;
    FGameplayMessageListenerHandle BusHandle = MessageBus->RegisterListener<FGameRemoteLogEntry>(
        TAG_Game_Log_Remote,
        [&ReceivedBroadcasts, &ReentrantFlushResult, LoggingSubsystem](const FGameRemoteLogEntry&) {
            ++ReceivedBroadcasts;
            GLog->Serialize(
                TEXT("NestedDisplayInsideListener"),
                ELogVerbosity::Display,
                FName(TEXT("LogNestedListenerTest"))
            );
            ReentrantFlushResult = LoggingSubsystem->FlushQueue();
        }
    );

    GLog->Serialize(TEXT("InitialRootLog1"), ELogVerbosity::Display, FName(TEXT("LogRootTest")));
    GLog->Serialize(TEXT("InitialRootLog2"), ELogVerbosity::Display, FName(TEXT("LogRootTest")));
    TestEqual(TEXT("初始 2 条根日志成功入队"), LoggingSubsystem->GetQueuedCount(), 2);

    const int32 DrainedFirst = LoggingSubsystem->FlushQueue();
    TestEqual(TEXT("首次排空应准确排出 2 条根日志"), DrainedFirst, 2);
    TestEqual(TEXT("监听者应恰好收到 2 次广播"), ReceivedBroadcasts, 2);
    TestEqual(TEXT("监听回调内递归调用 FlushQueue 应安全短路返回 0"), ReentrantFlushResult, 0);
    TestEqual(TEXT("监听者内部输出的日志应被重入守卫拦截阻断"), LoggingSubsystem->GetQueuedCount(), 0);
    TestEqual(TEXT("次轮排空应为 0，彻底杜绝自激雪崩循环"), LoggingSubsystem->FlushQueue(), 0);

    BusHandle.Unregister();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoggingSubsystemTickerDrainTest,
    "Game.Logging.Subsystem.TickerDrainAndDisableZeroSpin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证主线程 FTSTicker 按 MaxDrainBatchPerTick 分帧排空，且禁用态注销 Ticker 零空转
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameLoggingSubsystemTickerDrainTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameLoggingTickerTestWorld")));
    const auto ShutdownGuard = MakeCancelableScopeGuard([GameInstance]() {
        GameInstance->Shutdown();
        GameInstance->MarkAsGarbage();
    });

    UGameLoggingSubsystem* LoggingSubsystem = UGameLoggingSubsystem::GetPtr(GameInstance);
    UGameplayMessageSubsystem* MessageBus = UGameplayMessageSubsystem::GetPtr(GameInstance);
    TestNotNull(TEXT("UGameLoggingSubsystem 有效"), LoggingSubsystem);
    TestNotNull(TEXT("UGameplayMessageSubsystem 有效"), MessageBus);
    if (LoggingSubsystem == nullptr || MessageBus == nullptr) {
        return false;
    }

    LoggingSubsystem->SetMinVerbosity(ELogVerbosity::Display);
    LoggingSubsystem->SetMaxDrainBatchPerTick(10);
    LoggingSubsystem->ClearQueue();

    int32 TickerReceived = 0;
    FGameplayMessageListenerHandle BusHandle = MessageBus->RegisterListener<FGameRemoteLogEntry>(
        TAG_Game_Log_Remote,
        [&TickerReceived](const FGameRemoteLogEntry& Payload) {
            if (Payload.Category == FName(TEXT("LogTickerTest"))) {
                ++TickerReceived;
            }
        }
    );

    for (int32 Index = 0; Index < 35; ++Index) {
        GLog->Serialize(TEXT("TickerDrainItem"), ELogVerbosity::Display, FName(TEXT("LogTickerTest")), 1.0 + Index);
    }
    TestEqual(TEXT("入队 35 条测试日志"), LoggingSubsystem->GetQueuedCount(), 35);

    FTSTicker::GetCoreTicker().Tick(0.016f);
    TestEqual(TEXT("第 1 帧 Ticker 应精确排出 10 条"), TickerReceived, 10);
    TestEqual(TEXT("第 1 帧后队列应余 25 条"), LoggingSubsystem->GetQueuedCount(), 25);

    FTSTicker::GetCoreTicker().Tick(0.016f);
    TestEqual(TEXT("第 2 帧 Ticker 应累计排出 20 条"), TickerReceived, 20);
    TestEqual(TEXT("第 2 帧后队列应余 15 条"), LoggingSubsystem->GetQueuedCount(), 15);

    LoggingSubsystem->SetRemoteLoggingEnabled(false);
    FTSTicker::GetCoreTicker().Tick(0.016f);
    TestEqual(TEXT("禁用态已注销 Ticker，Tick 不得再触发排空"), TickerReceived, 20);
    TestEqual(TEXT("禁用态 Tick 后队列仍保持 15 条"), LoggingSubsystem->GetQueuedCount(), 15);

    LoggingSubsystem->SetRemoteLoggingEnabled(true);
    FTSTicker::GetCoreTicker().Tick(0.016f);
    FTSTicker::GetCoreTicker().Tick(0.016f);
    TestEqual(TEXT("重新启用后两帧 Ticker 应排空剩余全部 15 条"), TickerReceived, 35);
    TestEqual(TEXT("最终队列应归零"), LoggingSubsystem->GetQueuedCount(), 0);

    BusHandle.Unregister();
    return true;
}

#endif // WITH_AUTOMATION_TESTS
