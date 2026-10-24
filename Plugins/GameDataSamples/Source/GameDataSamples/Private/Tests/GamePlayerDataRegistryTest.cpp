#include "Dynamic/GamePlayerDataRegistry.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGamePlayerDataRegistryTest, "Game.Data.Dynamic.PlayerDataRegistry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGamePlayerDataRegistryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGamePlayerDataRegistry* Registry = NewObject<UGamePlayerDataRegistry>();

    const FPlayerProfileData& DefaultProfile = Registry->GetProfile();
    TestEqual(TEXT("默认初始等级为 1"), DefaultProfile.Progression.Level, 1);
    TestEqual(TEXT("默认初始经验为 0"), DefaultProfile.Progression.Experience, static_cast<int64>(0));
    TestEqual(TEXT("默认初始金币为 0"), DefaultProfile.Progression.Gold, static_cast<int64>(0));

    bool bBroadcasted = false;
    Registry->OnProfileChanged.AddLambda([&bBroadcasted]([[maybe_unused]] const FPlayerProfileData& Profile) {
        bBroadcasted = true;
    });

    FPlayerProfileData NewProfile;
    NewProfile.PlayerId = TEXT("Player_001");
    NewProfile.Progression.Level = 10;
    NewProfile.Progression.Experience = 5000;
    NewProfile.Progression.Gold = 9999;
    Registry->SetProfile(NewProfile);

    TestTrue(TEXT("设置档案触发广播"), bBroadcasted);
    TestEqual(TEXT("更新后等级为 10"), Registry->GetProfile().Progression.Level, 10);
    TestEqual(TEXT("更新后金币为 9999"), Registry->GetProfile().Progression.Gold, static_cast<int64>(9999));

    bBroadcasted = false;
    Registry->ResetToDefault();
    TestTrue(TEXT("重置档案触发广播"), bBroadcasted);
    TestEqual(TEXT("重置后等级恢复为 1"), Registry->GetProfile().Progression.Level, 1);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS

