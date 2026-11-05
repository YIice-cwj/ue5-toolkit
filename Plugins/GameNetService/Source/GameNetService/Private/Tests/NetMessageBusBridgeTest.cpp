/**
 * @file NetMessageBusBridgeTest.cpp
 * @brief 网络事件总线桥接器（FNetMessageBusBridge）外发请求订阅、下行广播、状态迁移广播与解绑单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Bridge/NetMessageBusBridge.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Message/GameplayMessageSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetMessageBusBridgeTest,
    "Game.Net.Service.MessageBusBridge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证桥接器绑定前静默安全、绑定后双向转发以及解绑后零残留监听
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetMessageBusBridgeTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameNetMessageBusBridgeTestWorld")));
    UGameplayMessageSubsystem& MessageBus = UGameplayMessageSubsystem::Get(GameInstance->GetWorld());

    FNetMessageBusBridge Bridge;
    TestFalse(TEXT("初始未绑定消息总线"), Bridge.IsBound());

    FNetPacketHeader SampleHeader;
    SampleHeader.MessageId = 901;
    SampleHeader.SequenceId = 12;
    Bridge.BroadcastInboundPacket(SampleHeader, {});
    Bridge.BroadcastConnectionState(ENetConnectionState::Connected, TEXT("mock://idle"));

    Bridge.Bind(MessageBus, nullptr);
    TestFalse(TEXT("空回调绑定请求安全忽略"), Bridge.IsBound());

    TArray<FGameNetOutboundMessage> CapturedOutbounds;
    Bridge.Bind(MessageBus, [&CapturedOutbounds](const FGameNetOutboundMessage& Outbound) {
        CapturedOutbounds.Add(Outbound);
    });
    TestTrue(TEXT("有效回调成功绑定消息总线"), Bridge.IsBound());

    TArray<FGameNetInboundMessage> CapturedInbounds;
    TArray<FGameNetStateMessage> CapturedStates;
    FGameplayMessageListenerHandle InboundHandle = MessageBus.RegisterListener<FGameNetInboundMessage>(
        TAG_Game_Net_Inbound,
        [&CapturedInbounds](FGameplayTag, const FGameNetInboundMessage& Msg) {
            CapturedInbounds.Add(Msg);
        }
    );
    FGameplayMessageListenerHandle StateHandle = MessageBus.RegisterListener<FGameNetStateMessage>(
        TAG_Game_Net_StateChanged,
        [&CapturedStates](FGameplayTag, const FGameNetStateMessage& Msg) {
            CapturedStates.Add(Msg);
        }
    );

    FGameNetOutboundMessage OutMsg;
    OutMsg.MessageId = 3100;
    OutMsg.SequenceId = 7;
    OutMsg.Payload = {10, 20};
    MessageBus.BroadcastMessage(TAG_Game_Net_Outbound, OutMsg);
    TestEqual(TEXT("桥接器捕获 TAG_Game_Net_Outbound 请求"), CapturedOutbounds.Num(), 1);
    TestEqual(TEXT("捕获协议 ID 一致"), CapturedOutbounds[0].MessageId, 3100u);

    const uint8 InPayload[] = {0xAA, 0xBB};
    Bridge.BroadcastInboundPacket(SampleHeader, InPayload);
    TestEqual(TEXT("TAG_Game_Net_Inbound 收到广播"), CapturedInbounds.Num(), 1);
    TestEqual(TEXT("下行广播序列号一致"), CapturedInbounds[0].SequenceId, 12u);
    TestEqual(TEXT("下行广播负载长度一致"), CapturedInbounds[0].Payload.Num(), 2);

    Bridge.BroadcastConnectionState(ENetConnectionState::Connected, TEXT("mock://local"));
    TestEqual(TEXT("TAG_Game_Net_StateChanged 收到广播"), CapturedStates.Num(), 1);
    TestTrue(TEXT("状态广播标记已连接"), CapturedStates[0].bIsConnected);
    TestEqual(TEXT("状态枚举精确一致"), CapturedStates[0].ConnectionState, ENetConnectionState::Connected);

    Bridge.Unbind();
    TestFalse(TEXT("Unbind 后解绑标志更新"), Bridge.IsBound());
    MessageBus.BroadcastMessage(TAG_Game_Net_Outbound, OutMsg);
    TestEqual(TEXT("解绑后不再触发旧回调"), CapturedOutbounds.Num(), 1);

    InboundHandle.Unregister();
    StateHandle.Unregister();
    GameInstance->Shutdown();
    return true;
}

#endif

