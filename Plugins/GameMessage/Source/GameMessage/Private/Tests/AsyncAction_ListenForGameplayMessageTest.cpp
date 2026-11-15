// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file AsyncAction_ListenForGameplayMessageTest.cpp
 * @brief 异步消息监听蓝图节点自动化测试
 */

#include "Message/AsyncAction_ListenForGameplayMessage.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Tests/GameMessageTestReceiver.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAsyncActionListenForGameplayMessageTest,
    "Game.Message.AsyncAction.Listen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 UAsyncAction_ListenForGameplayMessage 异步动作节点激活、消息接收与广播分发
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FAsyncActionListenForGameplayMessageTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("AsyncActionListenTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("测试世界上下文有效"), World);
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

    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.AsyncAction.Listen")));
    TestFalse(TEXT("初始状态无监听器"), Subsystem->HasListener(Channel));

    UGameMessageTestReceiver* Receiver = NewObject<UGameMessageTestReceiver>(GameInstance);
    UAsyncAction_ListenForGameplayMessage* AsyncAction = UAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(
        World,
        Channel,
        EGameplayMessageMatch::ExactMatch
    );
    TestNotNull(TEXT("创建异步节点成功"), AsyncAction);
    if (AsyncAction == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    AsyncAction->OnMessageReceived.AddDynamic(Receiver, &UGameMessageTestReceiver::OnAsyncMessageReceived);
    AsyncAction->Activate();

    TestTrue(TEXT("激活后子系统检测到有效监听器"), Subsystem->HasListener(Channel));

    FTestGameplayMessage Msg1;
    Msg1.Value = 101;
    Msg1.Text = TEXT("FirstPacket");
    Subsystem->BroadcastMessage(Channel, Msg1);

    TestEqual(TEXT("收到首条消息"), Receiver->ReceivedCount, 1);
    TestEqual(TEXT("首条消息通道匹配"), Receiver->LastChannel, Channel);

    FTestGameplayMessage ExtractedMsg1;
    TestTrue(TEXT("可成功提取首条消息载荷"), AsyncAction->GetPayloadTyped(ExtractedMsg1));
    TestEqual(TEXT("提取首条消息 Value 匹配"), ExtractedMsg1.Value, 101);
    TestEqual(TEXT("提取首条消息 Text 匹配"), ExtractedMsg1.Text, TEXT("FirstPacket"));

    FTestGameplayMessage Msg2;
    Msg2.Value = 202;
    Msg2.Text = TEXT("SecondPacket");
    Subsystem->BroadcastMessage(Channel, Msg2);

    TestEqual(TEXT("收到次条消息"), Receiver->ReceivedCount, 2);

    FTestGameplayMessage ExtractedMsg2;
    TestTrue(TEXT("可成功提取次条消息载荷"), AsyncAction->GetPayloadTyped(ExtractedMsg2));
    TestEqual(TEXT("提取次条消息 Value 匹配"), ExtractedMsg2.Value, 202);
    TestEqual(TEXT("提取次条消息 Text 匹配"), ExtractedMsg2.Text, TEXT("SecondPacket"));

    AsyncAction->SetReadyToDestroy();
    TestFalse(TEXT("销毁后监听器已解绑"), Subsystem->HasListener(Channel));

    FTestGameplayMessage ExtractedMsgAfterDestroy;
    TestFalse(TEXT("销毁后提取载荷应返回 false"), AsyncAction->GetPayloadTyped(ExtractedMsgAfterDestroy));

    Subsystem->BroadcastMessage(Channel, Msg2);
    TestEqual(TEXT("销毁后不再触发回调"), Receiver->ReceivedCount, 2);

    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAsyncActionCancelAndBoundaryTest,
    "Game.Message.AsyncAction.CancelAndBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证异步动作节点手动取消、空上下文防守与前缀匹配模式
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FAsyncActionCancelAndBoundaryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("AsyncActionCancelTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    UGameplayMessageSubsystem* Subsystem = UGameplayMessageSubsystem::GetPtr(World);
    TestNotNull(TEXT("子系统有效"), Subsystem);
    if (Subsystem == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    const FGameplayTag ParentChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.AsyncAction.Parent")));
    const FGameplayTag ChildChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.AsyncAction.Parent.Child")));

    UAsyncAction_ListenForGameplayMessage* NullContextAction = UAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(
        nullptr,
        ParentChannel
    );
    TestNull(TEXT("空世界上下文返回 nullptr"), NullContextAction);

    UGameMessageTestReceiver* Receiver = NewObject<UGameMessageTestReceiver>(GameInstance);
    UAsyncAction_ListenForGameplayMessage* ActionToCancel = UAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(
        World,
        ParentChannel,
        EGameplayMessageMatch::PartialMatch
    );
    TestNotNull(TEXT("创建部分匹配异步节点成功"), ActionToCancel);

    ActionToCancel->OnMessageReceived.AddDynamic(Receiver, &UGameMessageTestReceiver::OnAsyncMessageReceived);
    ActionToCancel->Activate();

    TestTrue(TEXT("激活后存在监听器"), Subsystem->HasListener(ParentChannel));

    FTestGameplayMessage ChildMsg;
    ChildMsg.Value = 303;
    Subsystem->BroadcastMessage(ChildChannel, ChildMsg);

    TestEqual(TEXT("前缀匹配成功接收子通道消息"), Receiver->ReceivedCount, 1);
    TestEqual(TEXT("接收到的具体通道为子通道"), Receiver->LastChannel, ChildChannel);

    ActionToCancel->Cancel();
    TestFalse(TEXT("Cancel 后监听器注销"), Subsystem->HasListener(ParentChannel));

    Subsystem->BroadcastMessage(ChildChannel, ChildMsg);
    TestEqual(TEXT("Cancel 后不再接收子通道广播"), Receiver->ReceivedCount, 1);

    GameInstance->Shutdown();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
