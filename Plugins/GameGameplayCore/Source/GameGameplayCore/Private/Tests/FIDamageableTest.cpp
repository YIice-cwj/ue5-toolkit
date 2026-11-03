/**
 * @file FIDamageableTest.cpp
 * @brief IDamageable 泛型受击接口与反射调用自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Damage/FGameDamageContext.h"
#include "Damage/FGameDamageResult.h"
#include "Damage/IDamageable.h"
#include "Tests/GameDamageTestTarget.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FIDamageableInterfaceTest,
    "Game.GameplayCore.Damage.IDamageableContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 IDamageable::Execute_ApplyDamageContext 接口反射调用、伤害结算、打击反馈及默认空实现
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FIDamageableInterfaceTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameDamageTestTarget* Target = NewObject<UGameDamageTestTarget>();
    TestNotNull(TEXT("测试受击 Target 生成成功"), Target);
    if (Target == nullptr) {
        return false;
    }

    TestTrue(TEXT("Target 实现了 UDamageable 接口"), Target->Implements<UDamageable>());

    const FGameplayTag SlashTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.Test.Physical.Slash")));
    const FGameplayTag HitTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.TestHit")));
    const FGameplayTag HitStopTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.TestHitStop")));

    FGameDamageContext Context;
    Context.SetChannelDamage(SlashTag, 100.0f);

    FGameDamageResult Result = IDamageable::Execute_ApplyDamageContext(Target, Context);
    TestEqual(TEXT("受击者同步返回结算总伤害"), Result.TotalActualDamage, 80.0f);
    TestTrue(TEXT("受击者注入了反馈状态标签"), Result.HasFeedbackTag(HitTag));
    TestEqual(TEXT("受击者通道伤害计算正确"), Result.GetActualChannelDamageExact(SlashTag), 80.0f);
    TestEqual(TEXT("受击者注入了动态数值"), Result.GetDynamicValue(HitStopTag), 0.06f);

    UGameDamageDefaultTestTarget* DefaultTarget = NewObject<UGameDamageDefaultTestTarget>();
    TestNotNull(TEXT("默认受击 Target 生成成功"), DefaultTarget);
    if (DefaultTarget != nullptr) {
        TestTrue(TEXT("DefaultTarget 实现了 UDamageable 接口"), DefaultTarget->Implements<UDamageable>());
        FGameDamageResult DefaultResult = IDamageable::Execute_ApplyDamageContext(DefaultTarget, Context);
        TestEqual(TEXT("接口默认实现返回空结果"), DefaultResult.TotalActualDamage, 0.0f);
        TestEqual(TEXT("接口默认实现无反馈标签"), DefaultResult.FeedbackTags.Num(), 0);
    }

    return true;
}

#endif
