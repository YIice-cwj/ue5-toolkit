/**
 * @file FConfigRegistryPIETest.cpp
 * @brief FConfigRegistry 多 World 上下文路由与 PIE 隔离性自动化单元测试
 */

#include "Config/FConfigRegistry.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FConfigRegistryPIEIsolationTest,
    "Game.Core.Config.PIEIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多 World 上下文下配置相互独立隔离、继承全局默认与全局单例免污染
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FConfigRegistryPIEIsolationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    if (GEngine == nullptr) {
        return false;
    }

    FConfigRegistry::ClearWorldRegistriesForTesting();

    UGameInstance* GameInstanceA = NewObject<UGameInstance>(GEngine);
    GameInstanceA->InitializeStandalone(FName(TEXT("PIEConfigTestWorld_A")));
    UWorld* WorldA = GameInstanceA->GetWorld();

    UGameInstance* GameInstanceB = NewObject<UGameInstance>(GEngine);
    GameInstanceB->InitializeStandalone(FName(TEXT("PIEConfigTestWorld_B")));
    UWorld* WorldB = GameInstanceB->GetWorld();

    TestNotNull(TEXT("世界 A 创建有效"), WorldA);
    TestNotNull(TEXT("世界 B 创建有效"), WorldB);
    if (WorldA == nullptr || WorldB == nullptr) {
        GameInstanceA->Shutdown();
        GameInstanceB->Shutdown();
        return false;
    }

    const FName TestKey(TEXT("Test.PIE.DamageMultiplier"));
    FConfigRegistry::Get().RegisterConfig<float>(TestKey, 1.0f);

    FConfigRegistry& RegistryGlobal = FConfigRegistry::Get();
    FConfigRegistry& RegistryA = FConfigRegistry::Get(WorldA);
    FConfigRegistry& RegistryB = FConfigRegistry::Get(WorldB);

    TestEqual(TEXT("全局默认值为 1.0"), RegistryGlobal.GetConfig<float>(TestKey, 0.0f), 1.0f);
    TestEqual(TEXT("世界 A 继承默认值为 1.0"), RegistryA.GetConfig<float>(TestKey, 0.0f), 1.0f);
    TestEqual(TEXT("世界 B 继承默认值为 1.0"), RegistryB.GetConfig<float>(TestKey, 0.0f), 1.0f);

    RegistryA.SetConfig<float>(TestKey, 2.5f);
    RegistryB.SetConfig<float>(TestKey, 9.9f);

    TestEqual(TEXT("世界 A 读取本地覆盖值 2.5"), RegistryA.GetConfig<float>(TestKey, 0.0f), 2.5f);
    TestEqual(TEXT("世界 B 读取本地覆盖值 9.9"), RegistryB.GetConfig<float>(TestKey, 0.0f), 9.9f);
    TestEqual(TEXT("全局单例未受污染仍为 1.0"), RegistryGlobal.GetConfig<float>(TestKey, 0.0f), 1.0f);

    GameInstanceA->Shutdown();
    GameInstanceB->Shutdown();
    FConfigRegistry::ClearWorldRegistriesForTesting();

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
