/**
 * @file UGameAbilitySystemGlobalsTest.cpp
 * @brief 能力系统全局单例与打击上下文分配自动化测试套件
 */

#include "Ability/UGameAbilitySystemGlobals.h"

#if WITH_AUTOMATION_TESTS

#include "Damage/FGameGameplayEffectContext.h"
#include "GameplayAbilitiesDeveloperSettings.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAbilitySystemGlobalsTest,
    "Game.Gameplay.AbilitySystemGlobals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证全局能力设置指向框架层 Globals 子类，且 AllocGameplayEffectContext 分配本工程自定义结构
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameAbilitySystemGlobalsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const UGameplayAbilitiesDeveloperSettings* Settings = GetDefault<UGameplayAbilitiesDeveloperSettings>();
    TestNotNull(TEXT("能力系统开发者设置可用"), Settings);
    if (Settings == nullptr) {
        return false;
    }

    UClass* ConfiguredGlobalsClass = Settings->AbilitySystemGlobalsClassName.TryLoadClass<UAbilitySystemGlobals>();
    TestNotNull(TEXT("配置的 Globals 类可解析"), ConfiguredGlobalsClass);
    TestTrue(TEXT("Globals 配置指向框架层子类（落位生效）"), ConfiguredGlobalsClass == UGameAbilitySystemGlobals::StaticClass());

    UAbilitySystemGlobals& Globals = UAbilitySystemGlobals::Get();
    TestTrue(TEXT("运行时 Globals 单例为框架层子类"), Globals.GetClass() == UGameAbilitySystemGlobals::StaticClass());
    if (Globals.GetClass() != UGameAbilitySystemGlobals::StaticClass()) {
        return false;
    }

    TUniquePtr<FGameplayEffectContext> Context(Globals.AllocGameplayEffectContext());
    TestNotNull(TEXT("上下文分配成功"), Context.Get());
    if (!Context.IsValid()) {
        return false;
    }
    TestTrue(TEXT("分配出的上下文为本工程脚本结构"), Context->GetScriptStruct() == FGameGameplayEffectContext::StaticStruct());

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
