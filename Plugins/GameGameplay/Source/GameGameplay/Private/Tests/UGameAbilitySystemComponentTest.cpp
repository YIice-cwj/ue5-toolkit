/**
 * @file UGameAbilitySystemComponentTest.cpp
 * @brief 通用能力系统组件基类自动化测试套件
 */

#include "Ability/UGameAbilitySystemComponent.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAbilitySystemComponentTest,
    "Game.Gameplay.AbilitySystemComponent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 ASC 构造默认网络复制状态、InitAbilityActorInfo 动态分配 ActorInfo 与宿主绑定
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameAbilitySystemComponentTest::RunTest([[maybe_unused]] const FString& Parameters) {
    AActor* OwnerActor = NewObject<AActor>(GetTransientPackage());
    AActor* AvatarActor = NewObject<AActor>(GetTransientPackage());

    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(OwnerActor);
    TestNotNull(TEXT("ASC 创建成功"), ASC);
    if (ASC == nullptr) {
        return false;
    }

    TestTrue(TEXT("ASC 默认开启网络复制"), ASC->GetIsReplicated());
    TestFalse(TEXT("初始化前 AbilityActorInfo 为空"), ASC->AbilityActorInfo.IsValid());

    ASC->InitAbilityActorInfo(OwnerActor, AvatarActor);
    TestTrue(TEXT("初始化后 AbilityActorInfo 分配就绪"), ASC->AbilityActorInfo.IsValid());
    if (ASC->AbilityActorInfo.IsValid()) {
        TestEqual(TEXT("AbilityActorInfo OwnerActor 绑定正确"), ASC->AbilityActorInfo->OwnerActor.Get(), OwnerActor);
        TestEqual(TEXT("AbilityActorInfo AvatarActor 绑定正确"), ASC->AbilityActorInfo->AvatarActor.Get(), AvatarActor);
    }

    return true;
}

#endif  // WITH_AUTOMATION_TESTS

