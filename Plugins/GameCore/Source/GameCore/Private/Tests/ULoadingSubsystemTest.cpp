/**
 * @file ULoadingSubsystemTest.cpp
 * @brief ULoadingSubsystem 自动化测试
 */

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "System/ULoadingSubsystem.h"

#if WITH_AUTOMATION_TESTS

namespace {

ULoadingSubsystem* CreateTestSubsystem(UGameInstance*& OutGameInstance) {
    if (GEngine == nullptr) {
        return nullptr;
    }
    OutGameInstance = NewObject<UGameInstance>(GEngine);
    OutGameInstance->InitializeStandalone(FName(TEXT("LoadingSubsystemTestWorld")));
    return OutGameInstance->GetSubsystem<ULoadingSubsystem>();
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoadingSubsystemTest,
    "Game.Core.System.LoadingSubsystem",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameLoadingSubsystemTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    ULoadingSubsystem* LoadingSubsystem = CreateTestSubsystem(GameInstance);
    TestNotNull(TEXT("加载子系统创建成功"), LoadingSubsystem);
    if (LoadingSubsystem == nullptr || GameInstance == nullptr) {
        return false;
    }

    TestFalse(TEXT("初始状态未处于加载态"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("初始目标关卡应为 None"), LoadingSubsystem->GetTargetLevelName(), NAME_None);

    LoadingSubsystem->OpenLevelWithLoading(NAME_None);
    TestFalse(TEXT("空关卡名不应进入加载态"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("目标关卡仍为 None"), LoadingSubsystem->GetTargetLevelName(), NAME_None);

    GameInstance->Shutdown();
    return true;
}

#endif
