/**
 * @file GameFeedbackCueBridgeTest.cpp
 * @brief GameplayCue 到 GameFeedback 信号桥接的自动化测试
 */

#include "Bridge/UGameplayCueNotify_GameFeedback.h"

#if WITH_AUTOMATION_TESTS

#include "Bus/GameFeedbackChannels.h"
#include "Bus/GameFeedbackMessages.h"
#include "Damage/FGameGameplayEffectContext.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/AGameFeedbackTestActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackCueBridgeTest,
    "Game.Gameplay.FeedbackCueBridge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证最大实际伤害通道、完整信号、原生上下文 HitResult 法线回退与 Dedicated Server 消息广播
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackCueBridgeTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameFeedbackCueBridgeTestWorld")));
    ON_SCOPE_EXIT {
        GameInstance->Shutdown();
    };

    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("Cue 桥接测试世界有效"), World);
    if (World == nullptr) {
        return false;
    }
    World->WorldType = EWorldType::PIE;
    World->SetPlayInEditorInitialNetMode(NM_DedicatedServer);
    AActor* Target = World->SpawnActor<AActor>();
    AActor* Instigator = World->SpawnActor<AActor>();
    AGameFeedbackTestActor* MovingCauser = World->SpawnActor<AGameFeedbackTestActor>();
    MovingCauser->SimulatedVelocity = FVector(0.0f, 500.0f, 0.0f);
    TestEqual(TEXT("测试目标运行于 Dedicated Server NetMode"), Target->GetNetMode(), NM_DedicatedServer);

    UGameplayMessageSubsystem* Bus = GameInstance->GetSubsystem<UGameplayMessageSubsystem>();
    int32 ReceivedCount = 0;
    FGameFeedbackMessage ReceivedMessage;
    FGameplayMessageListenerHandle Handle = Bus->RegisterListener<FGameFeedbackMessage>(
        TAG_Feedback_Channel_Play,
        [&ReceivedCount, &ReceivedMessage]([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackMessage& Message) {
            ++ReceivedCount;
            ReceivedMessage = Message;
        }
    );
    ON_SCOPE_EXIT {
        Handle.Unregister();
    };

    const FGameplayTag SlashTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Cue.Slash")));
    const FGameplayTag BluntTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Cue.Blunt")));
    const FGameplayTag CriticalTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.Cue.Critical")));

    FGameGameplayEffectContext* GameContext = new FGameGameplayEffectContext();
    GameContext->AddInstigator(Instigator, MovingCauser);
    GameContext->SetActualDamage(150.0f);
    GameContext->SetActualChannelDamage(BluntTag, 50.0f);
    GameContext->SetActualChannelDamage(SlashTag, 100.0f);
    GameContext->AddFeedbackTag(CriticalTag);
    GameContext->AddFeedbackTag(TAG_Feedback_State_Lethal);
    GameContext->SetFeedbackEventId(321);
    FHitResult Hit;
    Hit.TraceStart = FVector::ZeroVector;
    Hit.TraceEnd = FVector(0.0f, 10.0f, 0.0f);
    Hit.ImpactPoint = FVector(1.0f, 2.0f, 3.0f);
    Hit.ImpactNormal = FVector::UpVector;
    GameContext->AddHitResult(Hit);

    FGameplayCueParameters CueParameters;
    CueParameters.EffectContext = FGameplayEffectContextHandle(GameContext);
    CueParameters.MatchedTagName = BluntTag;
    CueParameters.RawMagnitude = 1.0f;

    const UGameplayCueNotify_GameFeedback* Cue = GetDefault<UGameplayCueNotify_GameFeedback>();
    TestFalse(TEXT("空 Target 时执行返回 false"), Cue->OnExecute_Implementation(nullptr, CueParameters));

    TestTrue(TEXT("Dedicated Server 必须允许 Cue 桥接"), Cue->OnExecute_Implementation(Target, CueParameters));
    TestEqual(TEXT("Play 消息必须广播一次"), ReceivedCount, 1);
    TestEqual(TEXT("强化链路从 EffectContext 自动还原 Instigator（即使 Parameters.Instigator 未手动填充）"), ReceivedMessage.Signal.Instigator.Get(), Instigator);
    TestEqual(TEXT("强化链路从 EffectContext 自动还原 DamageCauser"), ReceivedMessage.Signal.DamageCauser.Get(), static_cast<AActor*>(MovingCauser));
    TestEqual(TEXT("还原出带速度 DamageCauser 后冲击方向正确取速度归一化向量"), ReceivedMessage.Signal.ImpactDirection, FVector::RightVector);
    TestEqual(TEXT("最大实际伤害通道优先于 MatchedTagName"), ReceivedMessage.Signal.PrimaryChannel, SlashTag);
    TestEqual(TEXT("实际总伤害应完整保留"), ReceivedMessage.Signal.Magnitude, 150.0f);
    TestTrue(TEXT("本地完整反馈标签应保留"), ReceivedMessage.Signal.FeedbackTags.HasTag(CriticalTag));
    TestTrue(TEXT("致死状态应保留"), ReceivedMessage.Signal.bLethal);
    TestEqual(TEXT("跨端预测事件 ID 应保留"), ReceivedMessage.Signal.PredictionEventId, static_cast<uint16>(321));
    TestEqual(TEXT("轨迹方向应显式保留"), ReceivedMessage.Signal.TraceDirection, FVector::RightVector);

    FGameplayCueParameters FallbackCueParams;
    FallbackCueParams.Location = FVector(10.0f, 0.0f, 0.0f);
    FallbackCueParams.Normal = FVector::UpVector;
    FallbackCueParams.RawMagnitude = 33.0f;
    FallbackCueParams.MatchedTagName = SlashTag;

    TestTrue(TEXT("空上下文环境回落派发成功"), Cue->OnExecute_Implementation(Target, FallbackCueParams));
    TestEqual(TEXT("空上下文环境累积广播二次"), ReceivedCount, 2);
    TestEqual(TEXT("空上下文环境保留幅度"), ReceivedMessage.Signal.Magnitude, 33.0f);
    TestFalse(TEXT("空上下文回退不得把 Cue Tag 当作主通道"), ReceivedMessage.Signal.PrimaryChannel.IsValid());
    TestEqual(TEXT("空上下文环境保留参数法线作为冲击方向"), ReceivedMessage.Signal.ImpactDirection, FVector::UpVector);

    FGameplayEffectContext* NativeContext = new FGameplayEffectContext();
    NativeContext->AddInstigator(Instigator, MovingCauser);
    FHitResult NativeHit;
    NativeHit.ImpactNormal = FVector::ForwardVector;
    NativeHit.TraceStart = FVector::ZeroVector;
    NativeHit.TraceEnd = FVector(0.0f, 0.0f, 20.0f);
    NativeContext->AddHitResult(NativeHit);

    FGameplayCueParameters NativeCueParams;
    NativeCueParams.EffectContext = FGameplayEffectContextHandle(NativeContext);
    NativeCueParams.Normal = FVector::ZeroVector;
    NativeCueParams.RawMagnitude = 77.0f;
    NativeCueParams.MatchedTagName = BluntTag;

    TestTrue(TEXT("原生上下文环境回落派发成功"), Cue->OnExecute_Implementation(Target, NativeCueParams));
    TestEqual(TEXT("原生上下文环境累积广播三次"), ReceivedCount, 3);
    TestEqual(TEXT("原生回落链路从 EffectContext 自动还原 Instigator"), ReceivedMessage.Signal.Instigator.Get(), Instigator);
    TestEqual(TEXT("原生回落链路从 EffectContext 自动还原 DamageCauser"), ReceivedMessage.Signal.DamageCauser.Get(), static_cast<AActor*>(MovingCauser));
    TestEqual(TEXT("原生上下文环境保留幅度"), ReceivedMessage.Signal.Magnitude, 77.0f);
    TestFalse(TEXT("原生上下文回退不得把 Cue Tag 当作主通道"), ReceivedMessage.Signal.PrimaryChannel.IsValid());
    TestEqual(TEXT("Parameters.Normal 为零时回退使用 HitResult.ImpactNormal"), ReceivedMessage.Signal.ImpactDirection, FVector::ForwardVector);
    TestEqual(TEXT("原生回退分支正确计算 TraceDirection"), ReceivedMessage.Signal.TraceDirection, FVector::UpVector);

    return true;
}

#endif
