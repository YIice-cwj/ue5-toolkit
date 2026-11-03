/**
 * @file FGameDamageChannelRegistryTest.cpp
 * @brief 运行期伤害通道绑定注册表与修饰器演算自动化测试套件
 */

#include "Damage/FGameDamageChannelRegistry.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystemTestAttributeSet.h"
#include "Damage/UGameDamageExecution.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDamageChannelRegistryTest,
    "Game.Gameplay.DamageChannelRegistry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证通道绑定注册、覆盖、继承合并扁平化缓存、注销、清理及伤害修饰器先加后乘算子
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameDamageChannelRegistryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameDamageChannelRegistry& Registry = FGameDamageChannelRegistry::Get();
    UClass* ExecutionClass = UGameDamageExecution::StaticClass();
    Registry.UnregisterAll(ExecutionClass);

    const FGameplayAttribute TargetAttribute(FindFieldChecked<FProperty>(
        UAbilitySystemTestAttributeSet::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UAbilitySystemTestAttributeSet, Health)
    ));

    const FGameplayTag ChannelTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.RegistryTest")));
    const FGameplayTag SecondChannelTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.RegistryTest.Second")));

    TestTrue(
        TEXT("运行期注册通道成功"),
        UGameDamageExecution::RegisterRuntimeChannelBinding(ExecutionClass, ChannelTag, TargetAttribute)
    );

    TArray<FDamageChannelBinding> Bindings;
    Registry.GetBindings(ExecutionClass, Bindings);
    TestEqual(TEXT("注册后绑定数量为 1"), Bindings.Num(), 1);
    if (Bindings.Num() != 1) {
        return false;
    }
    TestTrue(TEXT("绑定通道标签正确"), Bindings[0].ChannelTag.MatchesTagExact(ChannelTag));

    TestTrue(
        TEXT("同通道重复注册覆盖而非叠加"),
        UGameDamageExecution::RegisterRuntimeChannelBinding(ExecutionClass, ChannelTag, TargetAttribute, EGameplayModOp::Multiplicitive)
    );
    Registry.GetBindings(ExecutionClass, Bindings);
    TestEqual(TEXT("重复注册后绑定数量仍为 1"), Bindings.Num(), 1);
    if (Bindings.Num() != 1) {
        return false;
    }
    TestTrue(TEXT("重复注册后修饰操作已覆盖"), Bindings[0].ModOp == EGameplayModOp::Multiplicitive);

    TestTrue(
        TEXT("注册第二条通道成功"),
        UGameDamageExecution::RegisterRuntimeChannelBinding(ExecutionClass, SecondChannelTag, TargetAttribute)
    );
    Registry.GetBindings(ExecutionClass, Bindings);
    TestEqual(TEXT("两条通道合并查询正确"), Bindings.Num(), 2);
    if (Bindings.Num() != 2) {
        return false;
    }

    TestTrue(TEXT("运行期注销通道成功"), UGameDamageExecution::UnregisterRuntimeChannelBinding(ExecutionClass, ChannelTag));
    Registry.GetBindings(ExecutionClass, Bindings);
    TestEqual(TEXT("注销后剩余一条通道"), Bindings.Num(), 1);
    if (Bindings.Num() != 1) {
        return false;
    }
    TestFalse(TEXT("重复注销返回失败"), UGameDamageExecution::UnregisterRuntimeChannelBinding(ExecutionClass, ChannelTag));

    Registry.UnregisterAll(ExecutionClass);
    Registry.GetBindings(ExecutionClass, Bindings);
    TestEqual(TEXT("清理后无残留绑定"), Bindings.Num(), 0);
    if (Bindings.Num() != 0) {
        return false;
    }

    TArray<FDamageChannelBinding> CachedBindings;
    Registry.GetBindings(ExecutionClass, CachedBindings);
    TestEqual(TEXT("连续二次查询命中读缓存"), CachedBindings.Num(), 0);

    TestEqual(TEXT("查询空类绑定返回 0"), Registry.Num(nullptr), 0);
    TestEqual(TEXT("查询未注册类绑定返回 0"), Registry.Num(UObject::StaticClass()), 0);
    TestFalse(TEXT("注销空类返回 false"), Registry.Unregister(nullptr, ChannelTag));
    TestFalse(TEXT("注销未注册类通道返回 false"), Registry.Unregister(UObject::StaticClass(), ChannelTag));
    TestEqual(TEXT("全部注销空类返回 0"), Registry.UnregisterAll(nullptr), 0);
    TestEqual(TEXT("全部注销未注册类返回 0"), Registry.UnregisterAll(UObject::StaticClass()), 0);

    Registry.Register(nullptr, FDamageChannelBinding());
    Registry.Register(ExecutionClass, FDamageChannelBinding(FGameplayTag(), TargetAttribute));
    Registry.Register(ExecutionClass, FDamageChannelBinding(ChannelTag, FGameplayAttribute()));
    TestEqual(TEXT("非法参数注册后类绑定数量仍为 0"), Registry.Num(ExecutionClass), 0);

    UClass* BaseClass = UGameplayEffectExecutionCalculation::StaticClass();
    Registry.UnregisterAll(BaseClass);
    Registry.UnregisterAll(ExecutionClass);

    Registry.Register(BaseClass, FDamageChannelBinding(ChannelTag, TargetAttribute, EGameplayModOp::Additive));
    Registry.Register(BaseClass, FDamageChannelBinding(SecondChannelTag, TargetAttribute, EGameplayModOp::Additive));
    Registry.Register(ExecutionClass, FDamageChannelBinding(ChannelTag, TargetAttribute, EGameplayModOp::Multiplicitive));

    Registry.GetBindings(ExecutionClass, Bindings);
    TestEqual(TEXT("派生类扁平化合并继承通道总数为 2"), Bindings.Num(), 2);
    const FDamageChannelBinding* FoundPhys = Bindings.FindByPredicate([&ChannelTag](const FDamageChannelBinding& B) {
        return B.ChannelTag.MatchesTagExact(ChannelTag);
    });
    TestNotNull(TEXT("找到派生类通道"), FoundPhys);
    if (FoundPhys != nullptr) {
        TestTrue(TEXT("派生类覆写通道算子生效"), FoundPhys->ModOp == EGameplayModOp::Multiplicitive);
    }

    Registry.Reset();
    TestEqual(TEXT("Reset 清空全部绑定"), Registry.Num(ExecutionClass), 0);
    TestEqual(TEXT("Reset 清空基类绑定"), Registry.Num(BaseClass), 0);

    const FDamageModifier CombinedModifier(2.0f, 20.0f);
    TestEqual(TEXT("伤害修饰器算子顺序为先加后乘"), CombinedModifier.Evaluate(100.0f), 240.0f);
    const FDamageModifier OverrideModifier = FDamageModifier::MakeOverride(42.0f);
    TestEqual(TEXT("伤害修饰器覆写模式优先生效"), OverrideModifier.Evaluate(100.0f), 42.0f);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS

