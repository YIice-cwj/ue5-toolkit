// Copyright Epic Games, Inc. All Rights Reserved.

#include "Message/GameplayMessageSubsystem.h"
#include "Message/AsyncAction_ListenForGameplayMessage.h"
#include "Tests/GamePoolableTestActor.h"

#if WITH_AUTOMATION_TESTS

#include <atomic>

#include "Async/ParallelFor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"

namespace {

struct FClosureCopyProbe {
    TSharedRef<int32> CopyCount = MakeShared<int32>(0);

    FClosureCopyProbe() = default;

    FClosureCopyProbe(const FClosureCopyProbe& Other)
        : CopyCount(Other.CopyCount) {
        ++(*CopyCount);
    }

    FClosureCopyProbe(FClosureCopyProbe&&) noexcept = default;
    FClosureCopyProbe& operator=(const FClosureCopyProbe&) = default;
    FClosureCopyProbe& operator=(FClosureCopyProbe&&) noexcept = default;
};

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameMessageBroadcastAndListenTest, "Game.Core.Message.BroadcastAndListen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证精确匹配、载荷传递与通道隔离
 */
bool FGameMessageBroadcastAndListenTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UGameplayMessageSubsystem* Subsystem = NewObject<UGameplayMessageSubsystem>(GameInstance);

    const FGameplayTag ChannelHit = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.Hit")));
    const FGameplayTag ChannelHeal = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.Heal")));

    int32 HitCount = 0;
    int32 LastHitValue = 0;
    FString LastHitText;

    int32 HealCount = 0;

    FGameplayMessageListenerHandle HitHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        ChannelHit,
        [&HitCount, &LastHitValue, &LastHitText]([[maybe_unused]] FGameplayTag Channel, const FTestGameplayMessage& Payload) {
            ++HitCount;
            LastHitValue = Payload.Value;
            LastHitText = Payload.Text;
        }
    );

    FGameplayMessageListenerHandle HealHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        ChannelHeal,
        [&HealCount]([[maybe_unused]] FGameplayTag Channel, [[maybe_unused]] const FTestGameplayMessage& Payload) {
            ++HealCount;
        }
    );

    TestEqual(TEXT("已注册 2 个监听器"), Subsystem->GetListenerCount(), 2);

    FTestGameplayMessage HitPayload;
    HitPayload.Value = 42;
    HitPayload.Text = TEXT("CriticalStrike");
    Subsystem->BroadcastMessage(ChannelHit, HitPayload);

    TestEqual(TEXT("命中通道收到消息"), HitCount, 1);
    TestEqual(TEXT("收到载荷数值一致"), LastHitValue, 42);
    TestEqual(TEXT("收到载荷文本一致"), LastHitText, TEXT("CriticalStrike"));
    TestEqual(TEXT("不同通道互不串扰"), HealCount, 0);

    FTestGameplayMessage HealPayload;
    HealPayload.Value = 100;
    Subsystem->BroadcastMessage(ChannelHeal, HealPayload);
    TestEqual(TEXT("治疗通道收到消息"), HealCount, 1);
    TestEqual(TEXT("命中通道未增加计数"), HitCount, 1);

    HitHandle.Unregister();
    HealHandle.Unregister();
    TestEqual(TEXT("注销后监听器清空"), Subsystem->GetListenerCount(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameMessagePartialMatchTest, "Game.Core.Message.PartialMatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证父标签部分匹配（MatchesTag）机制
 */
bool FGameMessagePartialMatchTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UGameplayMessageSubsystem* Subsystem = NewObject<UGameplayMessageSubsystem>(GameInstance);

    const FGameplayTag ParentChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Combat")));
    const FGameplayTag ChildChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Combat.Damage")));

    int32 ParentSeenCount = 0;
    int32 ChildSeenCount = 0;

    FGameplayMessageListenerHandle ParentHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        ParentChannel,
        [&ParentSeenCount]([[maybe_unused]] FGameplayTag Channel, [[maybe_unused]] const FTestGameplayMessage& Payload) {
            ++ParentSeenCount;
        },
        EGameplayMessageMatch::PartialMatch
    );

    FGameplayMessageListenerHandle ChildHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        ChildChannel,
        [&ChildSeenCount]([[maybe_unused]] FGameplayTag Channel, [[maybe_unused]] const FTestGameplayMessage& Payload) {
            ++ChildSeenCount;
        },
        EGameplayMessageMatch::ExactMatch
    );

    FTestGameplayMessage Msg;
    Msg.Value = 10;
    Subsystem->BroadcastMessage(ChildChannel, Msg);

    TestEqual(TEXT("子标签广播精确触发子监听器"), ChildSeenCount, 1);
    TestEqual(TEXT("父标签监听器通过 PartialMatch 捕获子通道广播"), ParentSeenCount, 1);

    Subsystem->BroadcastMessage(ParentChannel, Msg);
    TestEqual(TEXT("直接向父标签广播时 PartialMatch 监听器仅触发一次（无自身重复捕获）"), ParentSeenCount, 2);

    ParentHandle.Unregister();
    ChildHandle.Unregister();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameMessageRAIIUnregisterTest, "Game.Core.Message.RAIIUnregister",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证句柄作用域离开自动退订、无效通道容错与类型安全过滤
 */
bool FGameMessageRAIIUnregisterTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UGameplayMessageSubsystem* Subsystem = NewObject<UGameplayMessageSubsystem>(GameInstance);
    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.RAII")));

    int32 ReceivedCount = 0;

    {
        FGameplayMessageListenerHandle ScopedHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
            Channel,
            [&ReceivedCount](FGameplayTag, const FTestGameplayMessage&) {
                ++ReceivedCount;
            }
        );
        TestEqual(TEXT("作用域内监听器已存在"), Subsystem->GetListenerCount(), 1);

        FTestGameplayMessage Msg;
        Subsystem->BroadcastMessage(Channel, Msg);
        TestEqual(TEXT("作用域内广播成功送达"), ReceivedCount, 1);

        FVector WrongPayload(1.0f, 2.0f, 3.0f);
        Subsystem->BroadcastMessage(Channel, WrongPayload);
        TestEqual(TEXT("类型不匹配载荷被安全忽略"), ReceivedCount, 1);
    }

    TestEqual(TEXT("离开作用域后句柄析构自动注销"), Subsystem->GetListenerCount(), 0);

    FTestGameplayMessage AfterMsg;
    Subsystem->BroadcastMessage(Channel, AfterMsg);
    TestEqual(TEXT("退订后广播不再被接收"), ReceivedCount, 1);

    FGameplayMessageListenerHandle InvalidHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        FGameplayTag(),
        [](FGameplayTag, const FTestGameplayMessage&) {}
    );
    TestFalse(TEXT("空通道注册返回无效句柄"), InvalidHandle.IsValid());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameMessageHasListenerAndAsyncActionTest, "Game.Core.Message.HasListenerAndAsyncAction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证 HasListener 状态判定与 UAsyncAction_ListenForGameplayMessage 异步动作节点端到端链路
 */
bool FGameMessageHasListenerAndAsyncActionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameplayMessageAsyncActionTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    UGameplayMessageSubsystem* Subsystem = UGameplayMessageSubsystem::GetPtr(World);
    TestNotNull(TEXT("子系统实例有效"), Subsystem);
    if (Subsystem == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.HasListener")));
    TestFalse(TEXT("未注册时 HasListener 为 false"), Subsystem->HasListener(Channel));

    UGameMessageTestReceiver* Receiver = NewObject<UGameMessageTestReceiver>(GameInstance);
    UAsyncAction_ListenForGameplayMessage* AsyncAction = UAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(
        World,
        Channel,
        EGameplayMessageMatch::ExactMatch
    );
    AsyncAction->OnMessageReceived.AddDynamic(Receiver, &UGameMessageTestReceiver::OnAsyncMessageReceived);
    AsyncAction->Activate();

    TestTrue(TEXT("激活异步动作后 HasListener 为 true"), Subsystem->HasListener(Channel));

    FTestGameplayMessage Msg;
    Msg.Value = 7;
    Subsystem->BroadcastMessage(Channel, Msg);
    TestEqual(TEXT("异步动作收到广播"), Receiver->ReceivedCount, 1);
    TestEqual(TEXT("异步动作收到通道正确"), Receiver->LastChannel, Channel);

    AsyncAction->SetReadyToDestroy();
    TestFalse(TEXT("销毁异步动作后 HasListener 回退为 false"), Subsystem->HasListener(Channel));

    Subsystem->BroadcastMessage(Channel, Msg);
    TestEqual(TEXT("销毁异步动作后不再收到广播"), Receiver->ReceivedCount, 1);

    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMessageCrossThreadAndZeroCopyTest,
    "Game.Core.Message.CrossThreadEnqueueAndZeroCopy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多线程并发 EnqueueMessage 主线程串行排空、广播热路径闭包零拷贝及回调内自退订安全
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameMessageCrossThreadAndZeroCopyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameplayMessageCrossThreadTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    UGameplayMessageSubsystem* Subsystem = UGameplayMessageSubsystem::GetPtr(World);
    TestNotNull(TEXT("消息子系统实例有效"), Subsystem);
    if (Subsystem == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    const FGameplayTag AsyncChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(
        FName(TEXT("UnitTest.Message.CrossThread")));

    int32 DeliveredCount = 0;
    int64 DeliveredSum = 0;
    FClosureCopyProbe Probe;
    const TSharedRef<int32> CopyCounter = Probe.CopyCount;

    FGameplayMessageListenerHandle Handle = Subsystem->RegisterListener<FTestGameplayMessage>(
        AsyncChannel,
        [&DeliveredCount, &DeliveredSum, CapturedProbe = MoveTemp(Probe)](
            [[maybe_unused]] FGameplayTag Channel,
            const FTestGameplayMessage& Payload) {
            if (*CapturedProbe.CopyCount >= 0) {
                ++DeliveredCount;
                DeliveredSum += Payload.Value;
            }
        }
    );

    const int32 CopiesAfterRegister = *CopyCounter;
    for (int32 Index = 1; Index <= 20; ++Index) {
        FTestGameplayMessage SyncMsg;
        SyncMsg.Value = Index;
        Subsystem->BroadcastMessage(AsyncChannel, SyncMsg);
    }
    TestEqual(TEXT("高频广播热路径期间监听器闭包发生 0 次拷贝"), *CopyCounter, CopiesAfterRegister);
    TestEqual(TEXT("同步广播 20 次计数准确"), DeliveredCount, 20);

    DeliveredCount = 0;
    DeliveredSum = 0;
    constexpr int32 WorkerCount = 8;
    constexpr int32 MessagesPerWorker = 25;

    ParallelFor(WorkerCount, [Subsystem, AsyncChannel](int32 WorkerIdx) {
        for (int32 Step = 1; Step <= MessagesPerWorker; ++Step) {
            FTestGameplayMessage QueuedMsg;
            QueuedMsg.Value = WorkerIdx * 100 + Step;
            Subsystem->EnqueueMessage(AsyncChannel, MoveTemp(QueuedMsg));
        }
    });

    const int32 FlushedCount = Subsystem->FlushPendingMessages();
    TestTrue(TEXT("主线程显式排空处理的跨线程消息数大于 0"), FlushedCount > 0);
    TestEqual(TEXT("8 个工作线程共 200 条跨线程消息零丢失送达"), DeliveredCount, WorkerCount * MessagesPerWorker);

    int64 ExpectedSum = 0;
    for (int32 WorkerIdx = 0; WorkerIdx < WorkerCount; ++WorkerIdx) {
        for (int32 Step = 1; Step <= MessagesPerWorker; ++Step) {
            ExpectedSum += WorkerIdx * 100 + Step;
        }
    }
    TestEqual(TEXT("跨线程消息载荷累加和精确一致"), DeliveredSum, ExpectedSum);

    int32 SelfRemoveTriggered = 0;
    FGameplayMessageListenerHandle SelfRemoveHandle;
    SelfRemoveHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        AsyncChannel,
        [&SelfRemoveTriggered, &SelfRemoveHandle](FGameplayTag, const FTestGameplayMessage&) {
            ++SelfRemoveTriggered;
            SelfRemoveHandle.Unregister();
        }
    );
    FTestGameplayMessage TriggerMsg;
    TriggerMsg.Value = 1;
    Subsystem->BroadcastMessage(AsyncChannel, TriggerMsg);
    Subsystem->BroadcastMessage(AsyncChannel, TriggerMsg);
    TestEqual(TEXT("广播回调内自注销安全且仅触发一次"), SelfRemoveTriggered, 1);

    Handle.Unregister();
    GameInstance->Shutdown();
    return true;
}

#endif // WITH_AUTOMATION_TESTS
