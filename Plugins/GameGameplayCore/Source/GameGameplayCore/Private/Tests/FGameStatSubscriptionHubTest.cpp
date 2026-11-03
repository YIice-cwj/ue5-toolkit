/**
 * @file FGameStatSubscriptionHubTest.cpp
 * @brief FGameStatSubscriptionHub 属性按 Tag 订阅中心自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Stat/FGameStatSubscriptionHub.h"
#include "Stat/UGameStatComponent.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatTaggedSubscriptionTest,
    "Game.GameplayCore.Stat.TaggedSubscription",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证按 Tag 订阅数值变更、初次订阅即时回放及退订后隔离
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameStatTaggedSubscriptionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Subscribe.Health")));
    const FGameplayTag ShieldTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Subscribe.Shield")));

    Comp->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);
    Comp->RegisterStat(ShieldTag, 50.0f, 50.0f, 0.0f);

    int32 BroadcastCount = 0;
    float ObservedOld = 0.0f;
    float ObservedNew = 0.0f;
    const FDelegateHandle Handle = Comp->SubscribeStatChanged(
        HealthTag,
        FOnGameStatChangedNative::FDelegate::CreateLambda(
            [&BroadcastCount, &ObservedOld, &ObservedNew](const FGameplayTag&, float OldValue, float NewValue, float) {
                ++BroadcastCount;
                ObservedOld = OldValue;
                ObservedNew = NewValue;
            }
        )
    );

    TestEqual(TEXT("订阅即回放当前值"), BroadcastCount, 1);
    TestEqual(TEXT("回放旧值为当前值"), ObservedOld, 100.0f);
    TestEqual(TEXT("回放新值为当前值"), ObservedNew, 100.0f);

    Comp->ModifyStat(ShieldTag, -5.0f);
    TestEqual(TEXT("非订阅属性变化不触发回调"), BroadcastCount, 1);

    Comp->ModifyStat(HealthTag, -25.0f);
    TestEqual(TEXT("订阅属性变化触发回调"), BroadcastCount, 2);
    TestEqual(TEXT("回调旧值正确"), ObservedOld, 100.0f);
    TestEqual(TEXT("回调新值正确"), ObservedNew, 75.0f);

    Comp->UnsubscribeStatChanged(HealthTag, Handle);
    Comp->ModifyStat(HealthTag, -5.0f);
    TestEqual(TEXT("取消订阅后不再触发回调"), BroadcastCount, 2);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatRegisterReplayTest,
    "Game.GameplayCore.Stat.RegisterReplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证未注册属性订阅时抑制回放、注册完成立即以初始值回放及后续修正器触发
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameStatRegisterReplayTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag ManaTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Subscribe.LateRegister")));

    int32 CallbackCount = 0;
    float ObservedNew = 0.0f;
    Comp->SubscribeStatChanged(
        ManaTag,
        FOnGameStatChangedNative::FDelegate::CreateLambda(
            [&CallbackCount, &ObservedNew](const FGameplayTag&, float, float NewValue, float) {
                ++CallbackCount;
                ObservedNew = NewValue;
            }
        )
    );
    TestEqual(TEXT("未注册属性订阅时不回放"), CallbackCount, 0);

    Comp->RegisterStat(ManaTag, 60.0f, 120.0f, 0.0f);
    TestEqual(TEXT("注册时向既有订阅者回放初始值"), CallbackCount, 1);
    TestEqual(TEXT("注册回放值为初始当前值"), ObservedNew, 60.0f);

    Comp->AddStatModifier(ManaTag, FGameStatModifier(20.0f, EGameStatModifierOp::Add));
    TestEqual(TEXT("注册后修正器变化继续触发"), CallbackCount, 2);
    TestEqual(TEXT("修正后回放值为派生值"), ObservedNew, 80.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatOwnerBoundSubscriptionTest,
    "Game.GameplayCore.Stat.OwnerBoundSubscription",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证基于 UObject 宿主生命周期的弱引用订阅、垃圾回收后自动静默与显式退订
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameStatOwnerBoundSubscriptionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag ArmorTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Subscribe.OwnerArmor")));
    Comp->RegisterStat(ArmorTag, 80.0f, 80.0f, 0.0f);

    UGameStatComponent* Owner = NewObject<UGameStatComponent>();
    int32 CallbackCount = 0;
    float LastNewValue = 0.0f;
    const FDelegateHandle Handle = Comp->SubscribeStatChanged(Owner, ArmorTag,
        [&CallbackCount, &LastNewValue](const FGameplayTag&, float, float NewValue, float) {
            ++CallbackCount;
            LastNewValue = NewValue;
        });
    TestTrue(TEXT("Owner 订阅返回有效句柄"), Handle.IsValid());
    TestEqual(TEXT("Owner 订阅即回放当前值"), CallbackCount, 1);
    TestEqual(TEXT("回放值为当前值"), LastNewValue, 80.0f);

    Comp->ModifyStat(ArmorTag, -10.0f);
    TestEqual(TEXT("Owner 存活时收到更新"), CallbackCount, 2);
    TestEqual(TEXT("更新值正确"), LastNewValue, 70.0f);

    Owner->MarkAsGarbage();
    Comp->ModifyStat(ArmorTag, -10.0f);
    TestEqual(TEXT("Owner 销毁后不再回调"), CallbackCount, 2);

    Comp->UnsubscribeStatChanged(ArmorTag, Handle);
    Comp->ModifyStat(ArmorTag, -10.0f);
    TestEqual(TEXT("显式退订后不再回调"), CallbackCount, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatOwnerSubscriptionTargetedReplayTest,
    "Game.GameplayCore.Stat.OwnerSubscriptionTargetedReplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证新订阅者加入时专属初始值回放，不向既有订阅者重复广播
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameStatOwnerSubscriptionTargetedReplayTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag ArmorTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Subscribe.TargetedReplay")));
    Comp->RegisterStat(ArmorTag, 80.0f, 80.0f, 0.0f);

    UGameStatComponent* FirstOwner = NewObject<UGameStatComponent>();
    UGameStatComponent* SecondOwner = NewObject<UGameStatComponent>();
    int32 FirstCallbackCount = 0;
    int32 SecondCallbackCount = 0;
    float SecondObservedValue = 0.0f;

    Comp->SubscribeStatChanged(FirstOwner, ArmorTag,
        [&FirstCallbackCount](const FGameplayTag&, float, float, float) {
            ++FirstCallbackCount;
        });
    TestEqual(TEXT("首个 Owner 订阅收到初始回放"), FirstCallbackCount, 1);

    Comp->SubscribeStatChanged(SecondOwner, ArmorTag,
        [&SecondCallbackCount, &SecondObservedValue](const FGameplayTag&, float, float NewValue, float) {
            ++SecondCallbackCount;
            SecondObservedValue = NewValue;
        });
    TestEqual(TEXT("第二个 Owner 订阅不向首个订阅者重复回放"), FirstCallbackCount, 1);
    TestEqual(TEXT("第二个 Owner 仅收到一次初始回放"), SecondCallbackCount, 1);
    TestEqual(TEXT("第二个 Owner 收到当前值"), SecondObservedValue, 80.0f);

    Comp->ModifyStat(ArmorTag, -10.0f);
    TestEqual(TEXT("后续有效值变化仍通知首个订阅者"), FirstCallbackCount, 2);
    TestEqual(TEXT("后续有效值变化仍通知第二个订阅者"), SecondCallbackCount, 2);
    return true;
}

#endif
