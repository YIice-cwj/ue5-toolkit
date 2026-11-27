/**
 * @file GameFeedbackBlueprintLibraryTest.cpp
 * @brief 表现广播函数库、热关断门禁、预解析与全局配置自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Asset/GameFeedbackDefinition.h"
#include "Asset/GameFeedbackRegistry.h"
#include "Bus/GameFeedbackBlueprintLibrary.h"
#include "Bus/GameFeedbackChannels.h"
#include "Bus/GameFeedbackMessages.h"
#include "Config/GameFeedbackSettings.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Misc/ScopeExit.h"
#include "Tests/GameFeedbackTestHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackBlueprintLibraryTest,
    "Game.Feedback.BlueprintLibraryAndSettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证全局配置契约、BroadcastFeedback 自动预解析以及热关断期 BroadcastClearFeedback 始终放行
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackBlueprintLibraryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedFeedbackSettingsGuard SettingsGuard;
    UGameFeedbackSettings* Settings = SettingsGuard.Get();
    TestEqual(TEXT("全局配置所属分类必须为 Game"), Settings->GetCategoryName(), FName(TEXT("Game")));

    UGameFeedbackDefinition* ChannelDef = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/BPLib/ChannelDef.ChannelDef"));
    UGameFeedbackDefinition* ExplicitDef = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/BPLib/ExplicitDef.ExplicitDef"));
    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    const FGameplayTag SlashTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.BPLib.Slash")));
    Registry->ExactChannelMap.Add(SlashTag, ChannelDef);
    Registry->RebuildDefinitionIndex();
    Settings->GlobalRegistry = Registry;
    Settings->bEnableFeedback = true;

    GameFeedbackTestHelpers::FScopedStandaloneFeedbackWorld TestEnv(FName(TEXT("GameFeedbackBPLibTestWorld")));
    UWorld* World = TestEnv.GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UGameplayMessageSubsystem* Bus = TestEnv.GetGameInstance()->GetSubsystem<UGameplayMessageSubsystem>();
    TestNotNull(TEXT("消息总线有效"), Bus);
    if (!Bus) {
        return false;
    }

    int32 PlayCount = 0;
    int32 NetworkCount = 0;
    int32 ClearCount = 0;
    FGameFeedbackMessage LastPlayMsg;
    FGameFeedbackNetworkMessage LastNetMsg;
    FGameFeedbackClearMessage LastClearMsg;

    FGameplayMessageListenerHandle PlayHandle = Bus->RegisterListener<FGameFeedbackMessage>(
        TAG_Feedback_Channel_Play,
        [&PlayCount, &LastPlayMsg]([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackMessage& Msg) {
            ++PlayCount;
            LastPlayMsg = Msg;
        });
    FGameplayMessageListenerHandle NetHandle = Bus->RegisterListener<FGameFeedbackNetworkMessage>(
        TAG_Feedback_Channel_Network,
        [&NetworkCount, &LastNetMsg]([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackNetworkMessage& Msg) {
            ++NetworkCount;
            LastNetMsg = Msg;
        });
    FGameplayMessageListenerHandle ClearHandle = Bus->RegisterListener<FGameFeedbackClearMessage>(
        TAG_Feedback_Channel_Clear,
        [&ClearCount, &LastClearMsg]([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackClearMessage& Msg) {
            ++ClearCount;
            LastClearMsg = Msg;
        });

    ON_SCOPE_EXIT {
        PlayHandle.Unregister();
        NetHandle.Unregister();
        ClearHandle.Unregister();
    };

    AActor* Target = World->SpawnActor<AActor>();
    FGameFeedbackSignal Signal;
    Signal.PrimaryChannel = SlashTag;
    Signal.Target = Target;
    Signal.Magnitude = 42.0f;

    UGameFeedbackBlueprintLibrary::BroadcastFeedback(World, Signal);
    TestEqual(TEXT("开启状态下 Play 广播应送达一次"), PlayCount, 1);
    TestEqual(TEXT("未显式传入 Definition 时应由 BroadcastFeedback 预解析填充供全部监听者复用"), LastPlayMsg.OverrideDefinition.Get(), ChannelDef);

    UGameFeedbackBlueprintLibrary::BroadcastFeedback(World, Signal, ExplicitDef);
    TestEqual(TEXT("显式 Override 广播应送达二次"), PlayCount, 2);
    TestEqual(TEXT("显式 Override 必须优先于注册表预解析"), LastPlayMsg.OverrideDefinition.Get(), ExplicitDef);

    UGameFeedbackBlueprintLibrary::BroadcastNetworkFeedback(World, Signal, ExplicitDef, true);
    TestEqual(TEXT("开启状态下 Network 回执广播应送达一次"), NetworkCount, 1);
    TestEqual(TEXT("Network 回执应携带服务端最终定义"), LastNetMsg.ResolvedDefinition.Get(), ExplicitDef);
    TestTrue(TEXT("Network 回执应保留抑制标记"), LastNetMsg.bSuppressed);

    UGameFeedbackBlueprintLibrary::BroadcastClearFeedback(World, nullptr);
    TestEqual(TEXT("空 Target 的 Clear 请求应静默忽略"), ClearCount, 0);

    UGameFeedbackBlueprintLibrary::BroadcastClearFeedback(World, Target);
    TestEqual(TEXT("合法 Target 的 Clear 请求应送达一次"), ClearCount, 1);
    TestEqual(TEXT("Clear 载荷目标应匹配"), LastClearMsg.Target.Get(), Target);

    Settings->bEnableFeedback = false;
    UGameFeedbackBlueprintLibrary::BroadcastFeedback(World, Signal, ExplicitDef);
    UGameFeedbackBlueprintLibrary::BroadcastNetworkFeedback(World, Signal, ExplicitDef, false);
    TestEqual(TEXT("热关断期 Play 广播必须被拦截"), PlayCount, 2);
    TestEqual(TEXT("热关断期 Network 广播必须被拦截"), NetworkCount, 1);

    UGameFeedbackBlueprintLibrary::BroadcastClearFeedback(World, Target);
    TestEqual(TEXT("热关断期 BroadcastClearFeedback 必须始终放行以清除残留状态"), ClearCount, 2);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
