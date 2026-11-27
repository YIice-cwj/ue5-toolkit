/**
 * @file FGameDamageChannelRegistryTest.cpp
 * @brief 运行期伤害通道绑定注册表（含并发读写、防重入死锁与修饰器语义）自动化测试套件
 */

#include "Damage/FGameDamageChannelRegistry.h"

#if WITH_AUTOMATION_TESTS

#include <atomic>

#include "AbilitySystemTestAttributeSet.h"
#include "Async/ParallelFor.h"
#include "Damage/FDamageModifier.h"
#include "Damage/UGameDamageExecution.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDamageChannelRegistryTest,
    "Game.Gameplay.DamageChannelRegistry.Core",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证通道绑定注册、幂等短路、覆盖、继承合并、锁外防重入死锁及伤害修饰器语义
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameDamageChannelRegistryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameDamageChannelRegistry& Registry = FGameDamageChannelRegistry::Get();
    UClass* ExecutionClass = UGameDamageExecution::StaticClass();
    UClass* BaseClass = UGameplayEffectExecutionCalculation::StaticClass();
    ON_SCOPE_EXIT {
        Registry.UnregisterAll(ExecutionClass);
        Registry.UnregisterAll(BaseClass);
    };

    Registry.UnregisterAll(ExecutionClass);
    Registry.UnregisterAll(BaseClass);

    const FGameplayAttribute TargetAttribute(FindFieldChecked<FProperty>(
        UAbilitySystemTestAttributeSet::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UAbilitySystemTestAttributeSet, Health)
    ));

    const FGameplayTag ChannelTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.RegistryTest")));
    const FGameplayTag SecondChannelTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.RegistryTest.Second")));
    const FGameplayTag ReentrantChannelTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.RegistryTest.Reentrant")));

    TestTrue(
        TEXT("有效绑定 IsValid 为 true"),
        FDamageChannelBinding(ChannelTag, TargetAttribute).IsValid()
    );
    TestFalse(
        TEXT("空标签绑定 IsValid 为 false"),
        FDamageChannelBinding(FGameplayTag(), TargetAttribute).IsValid()
    );
    TestFalse(
        TEXT("空属性绑定 IsValid 为 false"),
        FDamageChannelBinding(ChannelTag, FGameplayAttribute()).IsValid()
    );

    TestTrue(
        TEXT("运行期注册通道成功"),
        Registry.Register(ExecutionClass, FDamageChannelBinding(ChannelTag, TargetAttribute))
    );
    TestTrue(
        TEXT("同绑定完全一致重复注册幂等成功"),
        Registry.Register(ExecutionClass, FDamageChannelBinding(ChannelTag, TargetAttribute))
    );

    TArray<FDamageChannelBinding> Bindings;
    Registry.ForEachBinding(ExecutionClass, [&Bindings](TConstArrayView<FDamageChannelBinding> InView) {
        Bindings = InView;
    });
    TestEqual(TEXT("注册后绑定数量为 1"), Bindings.Num(), 1);
    if (Bindings.Num() != 1) {
        return false;
    }
    TestTrue(TEXT("绑定通道标签正确"), Bindings[0].ChannelTag == ChannelTag);

    TestTrue(
        TEXT("同通道修改算子覆盖而非叠加"),
        Registry.Register(ExecutionClass, FDamageChannelBinding(ChannelTag, TargetAttribute, EGameplayModOp::Multiplicitive))
    );
    Registry.ForEachBinding(ExecutionClass, [&Bindings](TConstArrayView<FDamageChannelBinding> InView) {
        Bindings = InView;
    });
    TestEqual(TEXT("重复注册后绑定数量仍为 1"), Bindings.Num(), 1);
    if (Bindings.Num() != 1) {
        return false;
    }
    TestTrue(TEXT("重复注册后修饰操作已覆盖"), Bindings[0].ModOp == EGameplayModOp::Multiplicitive);

    TestTrue(
        TEXT("注册第二条通道成功"),
        Registry.Register(ExecutionClass, FDamageChannelBinding(SecondChannelTag, TargetAttribute))
    );
    Registry.ForEachBinding(ExecutionClass, [&Bindings](TConstArrayView<FDamageChannelBinding> InView) {
        Bindings = InView;
    });
    TestEqual(TEXT("两条通道合并查询正确"), Bindings.Num(), 2);
    if (Bindings.Num() != 2) {
        return false;
    }

    TestTrue(TEXT("运行期注销通道成功"), Registry.Unregister(ExecutionClass, ChannelTag));
    Registry.ForEachBinding(ExecutionClass, [&Bindings](TConstArrayView<FDamageChannelBinding> InView) {
        Bindings = InView;
    });
    TestEqual(TEXT("注销后剩余一条通道"), Bindings.Num(), 1);
    if (Bindings.Num() != 1) {
        return false;
    }
    TestFalse(TEXT("重复注销返回失败"), Registry.Unregister(ExecutionClass, ChannelTag));
    TestFalse(TEXT("无效标签注销返回失败"), Registry.Unregister(ExecutionClass, FGameplayTag()));

    Registry.UnregisterAll(ExecutionClass);
    Registry.ForEachBinding(ExecutionClass, [&Bindings](TConstArrayView<FDamageChannelBinding> InView) {
        Bindings = InView;
    });
    TestEqual(TEXT("清理后无残留绑定"), Bindings.Num(), 0);

    TArray<FDamageChannelBinding> CachedBindings;
    Registry.ForEachBinding(ExecutionClass, [&CachedBindings](TConstArrayView<FDamageChannelBinding> InView) {
        CachedBindings = InView;
    });
    TestEqual(TEXT("连续二次查询命中读缓存"), CachedBindings.Num(), 0);

    bool bNullCalled = false;
    Registry.ForEachBinding(nullptr, [&bNullCalled](TConstArrayView<FDamageChannelBinding>) {
        bNullCalled = true;
    });
    TestFalse(TEXT("空类查询不触发回调"), bNullCalled);

    TestEqual(TEXT("查询空类绑定返回 0"), Registry.Num(nullptr), 0);
    TestEqual(TEXT("查询未注册类绑定返回 0"), Registry.Num(UObject::StaticClass()), 0);
    TestFalse(TEXT("注销空类返回 false"), Registry.Unregister(nullptr, ChannelTag));
    TestFalse(TEXT("注销未注册类通道返回 false"), Registry.Unregister(UObject::StaticClass(), ChannelTag));
    TestEqual(TEXT("全部注销空类返回 0"), Registry.UnregisterAll(nullptr), 0);
    TestEqual(TEXT("全部注销未注册类返回 0"), Registry.UnregisterAll(UObject::StaticClass()), 0);

    TestFalse(TEXT("空类注册失败"), Registry.Register(nullptr, FDamageChannelBinding(ChannelTag, TargetAttribute)));
    TestFalse(TEXT("空标签注册失败"), Registry.Register(ExecutionClass, FDamageChannelBinding(FGameplayTag(), TargetAttribute)));
    TestFalse(TEXT("空属性注册失败"), Registry.Register(ExecutionClass, FDamageChannelBinding(ChannelTag, FGameplayAttribute())));
    TestEqual(TEXT("非法参数注册后类绑定数量仍为 0"), Registry.Num(ExecutionClass), 0);

    Registry.Register(BaseClass, FDamageChannelBinding(ChannelTag, TargetAttribute, EGameplayModOp::Additive));
    Registry.Register(BaseClass, FDamageChannelBinding(SecondChannelTag, TargetAttribute, EGameplayModOp::Additive));
    Registry.Register(ExecutionClass, FDamageChannelBinding(ChannelTag, TargetAttribute, EGameplayModOp::Multiplicitive));

    int32 NestedObservedCount = 0;
    Registry.ForEachBinding(ExecutionClass, [&Registry, ExecutionClass, ReentrantChannelTag, TargetAttribute, &NestedObservedCount, &Bindings](TConstArrayView<FDamageChannelBinding> InView) {
        Bindings = InView;
        Registry.Register(ExecutionClass, FDamageChannelBinding(ReentrantChannelTag, TargetAttribute));
        Registry.ForEachBinding(ExecutionClass, [&NestedObservedCount](TConstArrayView<FDamageChannelBinding> InnerView) {
            NestedObservedCount = InnerView.Num();
        });
    });
    TestEqual(TEXT("外层冷启动快照保持原合并总数 2"), Bindings.Num(), 2);
    TestEqual(TEXT("回调内重入注册与查询无死锁且可见新通道总数 3"), NestedObservedCount, 3);
    const FDamageChannelBinding* FoundPhys = Bindings.FindByPredicate([&ChannelTag](const FDamageChannelBinding& B) {
        return B.ChannelTag == ChannelTag;
    });
    TestNotNull(TEXT("找到派生类覆写通道"), FoundPhys);
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

    FDamageModifier OverrideDifferentNonActive(3.5f, 99.0f);
    OverrideDifferentNonActive.bOverride = true;
    OverrideDifferentNonActive.OverrideValue = 42.0f;
    TestTrue(TEXT("覆写模式下无关倍率加减差异仍判定语义相等"), OverrideModifier == OverrideDifferentNonActive);
    TestFalse(TEXT("覆写与非覆写修饰器不相等"), OverrideModifier == CombinedModifier);

    const FDamageChannelBinding BindingA(ChannelTag, TargetAttribute, EGameplayModOp::Additive);
    const FDamageChannelBinding BindingB(ChannelTag, TargetAttribute, EGameplayModOp::Additive);
    const FDamageChannelBinding BindingC(ChannelTag, TargetAttribute, EGameplayModOp::Multiplicitive);
    TestTrue(TEXT("通道绑定相同则值相等"), BindingA == BindingB);
    TestFalse(TEXT("修饰操作不同则值不等"), BindingA == BindingC);

    const FDamageModifier ModA(2.0f, 10.0f);
    const FDamageModifier ModB(2.0f, 10.0f);
    const FDamageModifier ModC(1.5f, 10.0f);
    TestTrue(TEXT("伤害修饰器相同则值相等"), ModA == ModB);
    TestFalse(TEXT("乘数不同则值不等"), ModA == ModC);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDamageChannelRegistryConcurrencyTest,
    "Game.Gameplay.DamageChannelRegistry.Concurrency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多线程并发注册/注销/遍历的线程安全与零死锁不变式
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameDamageChannelRegistryConcurrencyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameDamageChannelRegistry& Registry = FGameDamageChannelRegistry::Get();
    UClass* ExecutionClass = UGameDamageExecution::StaticClass();
    ON_SCOPE_EXIT {
        Registry.UnregisterAll(ExecutionClass);
    };

    Registry.UnregisterAll(ExecutionClass);
    const FGameplayAttribute TargetAttribute(FindFieldChecked<FProperty>(
        UAbilitySystemTestAttributeSet::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UAbilitySystemTestAttributeSet, Health)
    ));
    const FGameplayTag StableTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.Concurrent.Stable")));
    const FGameplayTag DynamicTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.Concurrent.Dynamic")));

    Registry.Register(ExecutionClass, FDamageChannelBinding(StableTag, TargetAttribute));

    constexpr int32 WorkerTasks = 32;
    constexpr int32 IterationsPerTask = 200;
    std::atomic<int32> ObservedValidSnapshots{0};

    ParallelFor(WorkerTasks, [&](int32 WorkerIndex) {
        for (int32 Iter = 0; Iter < IterationsPerTask; ++Iter) {
            if ((WorkerIndex & 3) == 0) {
                if ((Iter & 1) == 0) {
                    Registry.Register(ExecutionClass, FDamageChannelBinding(DynamicTag, TargetAttribute));
                } else {
                    Registry.Unregister(ExecutionClass, DynamicTag);
                }
            } else {
                Registry.ForEachBinding(ExecutionClass, [&ObservedValidSnapshots, &StableTag](TConstArrayView<FDamageChannelBinding> View) {
                    if (View.Num() >= 1 && View[0].ChannelTag == StableTag) {
                        ObservedValidSnapshots.fetch_add(1, std::memory_order_relaxed);
                    }
                });
            }
        }
    });

    const int32 ExpectedReads = (WorkerTasks * 3 / 4) * IterationsPerTask;
    TestEqual(TEXT("全部并发读快照均观测到稳定通道且无崩溃无死锁"), ObservedValidSnapshots.load(), ExpectedReads);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
