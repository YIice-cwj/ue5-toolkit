/**
 * @file FGameStatSubscriptionHubTest.cpp
 * @brief FGameStatSubscriptionHub 属性按 Tag 订阅中心、弱引用生命周期与重入安全自动化测试
 */

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
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

    TSharedRef<FDelegateHandle> SelfRemoveHandle = MakeShared<FDelegateHandle>();
    int32 SelfRemoveCount = 0;
    *SelfRemoveHandle = Comp->SubscribeStatChanged(
        HealthTag,
        FOnGameStatChangedNative::FDelegate::CreateLambda(
            [&SelfRemoveCount, SelfRemoveHandle, Comp, HealthTag](const FGameplayTag&, float, float, float) {
                ++SelfRemoveCount;
                if (SelfRemoveHandle->IsValid()) {
                    Comp->UnsubscribeStatChanged(HealthTag, *SelfRemoveHandle);
                }
            }
        )
    );
    TestEqual(TEXT("单次订阅初次回放执行 1 次"), SelfRemoveCount, 1);
    Comp->ModifyStat(HealthTag, -10.0f);
    TestEqual(TEXT("首次变动通知时完成自我退订"), SelfRemoveCount, 2);
    Comp->ModifyStat(HealthTag, -10.0f);
    TestEqual(TEXT("自我退订后后续变动不再触发"), SelfRemoveCount, 2);

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

    const FDelegateHandle InvalidOwnerHandle = Comp->SubscribeStatChanged(Owner, ArmorTag,
        [](const FGameplayTag&, float, float, float) {});
    TestFalse(TEXT("已失效 Owner 拒绝订阅"), InvalidOwnerHandle.IsValid());

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatUnregisterSubscriptionCleanupTest,
    "Game.GameplayCore.Stat.UnregisterSubscriptionCleanup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证注销属性时同步清除该标签下的全部订阅者，重新注册后旧订阅不复生
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameStatUnregisterSubscriptionCleanupTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag TempTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Subscribe.TempCleanup")));
    Comp->RegisterStat(TempTag, 50.0f, 50.0f, 0.0f);

    int32 CallbackCount = 0;
    Comp->SubscribeStatChanged(
        TempTag,
        FOnGameStatChangedNative::FDelegate::CreateLambda(
            [&CallbackCount](const FGameplayTag&, float, float, float) {
                ++CallbackCount;
            }
        )
    );
    TestEqual(TEXT("初次订阅回放 1 次"), CallbackCount, 1);

    const TGameStatusResult<void> UnregResult = Comp->UnregisterStat(TempTag);
    TestTrue(TEXT("成功注销属性"), UnregResult.IsSuccess());

    Comp->RegisterStat(TempTag, 100.0f, 100.0f, 0.0f);
    TestEqual(TEXT("重新注册后旧订阅已被清空，不应触发回调"), CallbackCount, 1);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatSubscriptionReentrancyTest,
    "Game.GameplayCore.Stat.SubscriptionReentrancy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证广播期间重入：回调内自退订最后监听者、大规模订阅触发 TMap 扩容重排、清空当前标签与中途销毁同组 Owner
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameStatSubscriptionReentrancyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameStatSubscriptionHub Hub;
    const FGameplayTag PrimaryTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Reentrancy.Primary")));

    int32 RehashSecondCount = 0;
    bool bSawRehashBeforeSecond = false;
    bool bDidRehash = false;
    Hub.Subscribe(
        PrimaryTag,
        FOnGameStatChangedNative::FDelegate::CreateLambda(
            [&RehashSecondCount, &bDidRehash, &bSawRehashBeforeSecond](const FGameplayTag&, float, float, float) {
                ++RehashSecondCount;
                bSawRehashBeforeSecond = bDidRehash;
            }
        )
    );
    Hub.Subscribe(
        PrimaryTag,
        FOnGameStatChangedNative::FDelegate::CreateLambda(
            [&Hub, &bDidRehash](const FGameplayTag&, float, float, float) {
                if (!bDidRehash) {
                    bDidRehash = true;
                    for (int32 Index = 0; Index < 32; ++Index) {
                        const FGameplayTag ExtraTag = UGameplayTagsManager::Get().AddNativeGameplayTag(
                            FName(*FString::Printf(TEXT("Attribute.Reentrancy.Extra.%d"), Index)));
                        Hub.Subscribe(ExtraTag, FOnGameStatChangedNative::FDelegate::CreateLambda(
                            [](const FGameplayTag&, float, float, float) {}));
                    }
                }
            }
        )
    );

    Hub.Broadcast(PrimaryTag, 100.0f, 90.0f, 100.0f);
    TestTrue(TEXT("回调内触发了 32 次跨标签订阅扩容"), bDidRehash);
    TestEqual(TEXT("TMap 扩容重排后同轮后续订阅者安全完成回调"), RehashSecondCount, 1);
    TestTrue(TEXT("确证后续订阅者是在 TMap 扩容重排之后安全执行的"), bSawRehashBeforeSecond);

    const FGameplayTag ClearTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Reentrancy.ClearDuringBroadcast")));
    int32 ClearInvokerCount = 0;
    int32 ResubscribedCount = 0;
    Hub.Subscribe(
        ClearTag,
        FOnGameStatChangedNative::FDelegate::CreateLambda(
            [&Hub, ClearTag, &ClearInvokerCount, &ResubscribedCount](const FGameplayTag&, float, float, float) {
                ++ClearInvokerCount;
                Hub.ClearTagSubscriptions(ClearTag);
                Hub.Subscribe(ClearTag, FOnGameStatChangedNative::FDelegate::CreateLambda(
                    [&ResubscribedCount](const FGameplayTag&, float, float, float) {
                        ++ResubscribedCount;
                    }));
            }
        )
    );
    Hub.Broadcast(ClearTag, 10.0f, 5.0f, 10.0f);
    TestEqual(TEXT("广播中清空并重订阅当前标签安全完成"), ClearInvokerCount, 1);
    Hub.Broadcast(ClearTag, 5.0f, 1.0f, 10.0f);
    TestEqual(TEXT("重订阅的新监听者在下一轮广播保持存活且正常触发"), ResubscribedCount, 1);

    const FGameplayTag MidKillTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Reentrancy.MidBroadcastKill")));
    UGameStatComponent* VictimOwner = NewObject<UGameStatComponent>();
    int32 KillerCalled = 0;
    int32 VictimCalled = 0;
    Hub.Subscribe(
        VictimOwner,
        MidKillTag,
        [&VictimCalled](const FGameplayTag&, float, float, float) {
            ++VictimCalled;
        }
    );
    Hub.Subscribe(
        MidKillTag,
        FOnGameStatChangedNative::FDelegate::CreateLambda(
            [VictimOwner, &KillerCalled](const FGameplayTag&, float, float, float) {
                ++KillerCalled;
                VictimOwner->MarkAsGarbage();
            }
        )
    );

    Hub.Broadcast(MidKillTag, 50.0f, 40.0f, 50.0f);
    TestEqual(TEXT("前置回调执行完成"), KillerCalled, 1);
    TestEqual(TEXT("广播中途被 MarkAsGarbage 的后置 Owner 回调被 WeakLambda 实时拦截"), VictimCalled, 0);

    return true;
}

#endif
