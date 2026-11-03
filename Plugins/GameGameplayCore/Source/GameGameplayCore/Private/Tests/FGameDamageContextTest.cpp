/**
 * @file FGameDamageContextTest.cpp
 * @brief FGameDamageContext 上下文计算自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Damage/FGameDamageContext.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDamageContextTest,
    "Game.GameplayCore.Damage.ContextOperations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameDamageContext 通道数值设定、累加、精确/层级读取与总基础伤害演算
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameDamageContextTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameDamageContext Context;

    const FGameplayTag PhysTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Context.Physical")));
    const FGameplayTag SlashTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Context.Physical.Slash")));
    const FGameplayTag FireTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Context.Fire")));

    TestEqual(TEXT("初始总基础伤害为 0"), Context.GetTotalBaseDamage(), 0.0f);

    Context.SetChannelDamage(SlashTag, 80.0f);
    Context.AddChannelDamage(FireTag, 20.0f);

    TestEqual(TEXT("精确获取 Slash 伤害正确"), Context.GetChannelDamageExact(SlashTag), 80.0f);
    TestEqual(TEXT("精确获取 Fire 伤害正确"), Context.GetChannelDamageExact(FireTag), 20.0f);
    TestEqual(TEXT("层级匹配 Phys 伤害正确"), Context.GetChannelDamage(PhysTag), 80.0f);
    TestEqual(TEXT("总基础伤害等于各通道之和"), Context.GetTotalBaseDamage(), 100.0f);

    const FGameplayTag TraitCrit = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Trait.Crit")));
    Context.TraitTags.AddTag(TraitCrit);
    TestTrue(TEXT("伤害特质标签记录成功"), Context.TraitTags.HasTag(TraitCrit));

    return true;
}

#endif
