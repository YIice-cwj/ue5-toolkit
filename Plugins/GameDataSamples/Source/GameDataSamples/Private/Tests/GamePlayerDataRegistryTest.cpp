/**
 * @file GamePlayerDataRegistryTest.cpp
 * @brief 玩家动态数据注册表与玩家档案存档适配器自动化测试
 */

#include "Dynamic/GamePlayerDataRegistry.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Save/GamePlayerProfileSaveAdapter.h"
#include "Save/USaveGameSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGamePlayerDataRegistryTest, "Game.Data.Dynamic.PlayerDataRegistry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证玩家动态数据注册表读写通知及 UGamePlayerProfileSaveAdapter 存档段往返与安全解绑
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
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

    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("PlayerProfileAdapterTestWorld")));
    USaveGameSubsystem* SaveSys = GameInstance->GetSubsystem<USaveGameSubsystem>();
    if (TestNotNull(TEXT("存档子系统实例有效"), SaveSys)) {
        UGamePlayerProfileSaveAdapter* Adapter = NewObject<UGamePlayerProfileSaveAdapter>(GameInstance);
        Adapter->Setup(SaveSys, Registry);
        TestTrue(TEXT("Setup 后档案存档段已登记"), SaveSys->HasSegment(UGamePlayerProfileSaveAdapter::SegmentName));

        const FString ProfileSlot = TEXT("UnitTest_PlayerProfileSlot");
        TestTrue(TEXT("保存玩家档案段成功"), SaveSys->SaveGameToSlot(ProfileSlot).IsSuccess());

        Registry->ResetToDefault();
        TestEqual(TEXT("重置后等级恢复为 1"), Registry->GetProfile().Progression.Level, 1);

        TestTrue(TEXT("从存档槽位恢复玩家档案成功"), SaveSys->LoadGameFromSlot(ProfileSlot).IsSuccess());
        TestEqual(TEXT("恢复后 PlayerId 一致"), Registry->GetProfile().PlayerId, FString(TEXT("Player_001")));
        TestEqual(TEXT("恢复后等级为 10"), Registry->GetProfile().Progression.Level, 10);
        TestEqual(TEXT("恢复后金币为 9999"), Registry->GetProfile().Progression.Gold, static_cast<int64>(9999));

        (void)SaveSys->DeleteSaveSlot(ProfileSlot);
        Adapter->Teardown();
        TestFalse(TEXT("Teardown 后档案存档段已注销"), SaveSys->HasSegment(UGamePlayerProfileSaveAdapter::SegmentName));
    }
    GameInstance->Shutdown();

    bBroadcasted = false;
    Registry->ResetToDefault();
    TestTrue(TEXT("重置档案触发广播"), bBroadcasted);
    TestEqual(TEXT("重置后等级恢复为 1"), Registry->GetProfile().Progression.Level, 1);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS

