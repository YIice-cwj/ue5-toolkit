// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Remote/GameLoggingSubsystem.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoggingSubsystemTest,
    "Game.Logging.Subsystem",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 GameLoggingSubsystem 挂载、配置、总线广播与批量排空生命周期
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLoggingSubsystemTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameLoggingSubsystemTestWorld")));

    UGameLoggingSubsystem* LoggingSubsystem = GameInstance->GetSubsystem<UGameLoggingSubsystem>();
    TestNotNull(TEXT("GameInstance 应成功创建 UGameLoggingSubsystem"), LoggingSubsystem);
    if (LoggingSubsystem == nullptr) {
        return false;
    }

    UGameplayMessageSubsystem* MessageBus = GameInstance->GetSubsystem<UGameplayMessageSubsystem>();
    TestNotNull(TEXT("GameInstance 应具备 UGameplayMessageSubsystem"), MessageBus);
    if (MessageBus == nullptr) {
        return false;
    }

    TestTrue(TEXT("默认状态下应开启远程日志采集"), LoggingSubsystem->IsRemoteLoggingEnabled());
    TestNotNull(TEXT("底层捕获设备指针应有效"), LoggingSubsystem->GetOutputDevice());
    TestEqual(TEXT("默认单帧排空上限应为 50"), LoggingSubsystem->GetMaxDrainBatchPerTick(), 50);

    LoggingSubsystem->SetMaxDrainBatchPerTick(20);
    TestEqual(TEXT("单帧排空上限应已更新为 20"), LoggingSubsystem->GetMaxDrainBatchPerTick(), 20);

    LoggingSubsystem->SetRemoteLoggingEnabled(false);
    TestFalse(TEXT("应成功关闭远程日志采集"), LoggingSubsystem->IsRemoteLoggingEnabled());

    LoggingSubsystem->SetRemoteLoggingEnabled(true);
    TestTrue(TEXT("应成功重新开启远程日志采集"), LoggingSubsystem->IsRemoteLoggingEnabled());

    LoggingSubsystem->FlushQueue();

    int32 ReceivedCount = 0;
    FGameRemoteLogEntry LastReceivedEntry;

    FGameplayMessageListenerHandle BusHandle = MessageBus->RegisterListener<FGameRemoteLogEntry>(
        TAG_Game_Log_Remote,
        [&ReceivedCount, &LastReceivedEntry]([[maybe_unused]] FGameplayTag Channel, const FGameRemoteLogEntry& Payload) {
            ++ReceivedCount;
            LastReceivedEntry = Payload;
        }
    );

    TestTrue(TEXT("消息总线监听句柄应有效"), BusHandle.IsValid());

    FGameRemoteLogOutputDevice* Device = LoggingSubsystem->GetOutputDevice();
    TestNotNull(TEXT("捕获设备应有效"), Device);
    if (Device != nullptr) {
        Device->Serialize(
            TEXT("BroadcastErrorPayload"),
            ELogVerbosity::Error,
            FName(TEXT("LogSubsystemTestCategory")),
            314.159
        );
    }

    const int32 FlushedCount = LoggingSubsystem->FlushQueue();
    TestEqual(TEXT("立即排空应返回 1 条日志"), FlushedCount, 1);
    TestEqual(TEXT("总线监听器应收到 1 次广播"), ReceivedCount, 1);
    TestEqual(TEXT("广播条目分类断言"), LastReceivedEntry.Category, FName(TEXT("LogSubsystemTestCategory")));
    TestEqual(TEXT("广播条目级别断言"), LastReceivedEntry.GetVerbosity(), ELogVerbosity::Error);
    TestEqual(TEXT("广播条目正文断言"), LastReceivedEntry.Message, FString(TEXT("BroadcastErrorPayload")));
    TestEqual(TEXT("广播条目时间戳断言"), LastReceivedEntry.Timestamp, 314.159);

    const int32 EmptyFlush = LoggingSubsystem->FlushQueue();
    TestEqual(TEXT("队列为空时排空应返回 0"), EmptyFlush, 0);

    BusHandle.Unregister();
    TestFalse(TEXT("注销后监听句柄应失效"), BusHandle.IsValid());

    return true;
}

#endif // WITH_AUTOMATION_TESTS
