/**
 * @file UGameAbilitySystemGlobalsTest.cpp
 * @brief GameGameplay 模块自动化测试套件（Globals、EffectContext、AttributeSet、ValueMapping、Bootstrap）
 */

#include "Damage/FGameGameplayEffectContext.h"

#if WITH_AUTOMATION_TESTS

#include "Damage/FGameDamageChannelRegistry.h"
#include "Damage/UGameDamageExecution.h"
#include "Ability/UGameGameplayAbility.h"
#include "Ability/UGameAbilitySystemGlobals.h"
#include "Attribute/UGameplayAttributeValueMapping.h"
#include "Ability/UGameAbilitySystemBootstrap.h"
#include "Ability/UGameAbilitySystemComponent.h"
#include "Attribute/UGameBaseAttributeSet.h"
#include "AbilitySystemTestAttributeSet.h"
#include "GameplayAbilitiesDeveloperSettings.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayEffectContextTest,
    "Game.Gameplay.EffectContext",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameGameplayEffectContextTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const UGameplayAbilitiesDeveloperSettings* Settings = GetDefault<UGameplayAbilitiesDeveloperSettings>();
    TestNotNull(TEXT("能力系统开发者设置可用"), Settings);
    if (Settings == nullptr) {
        return false;
    }

    UClass* ConfiguredGlobalsClass = Settings->AbilitySystemGlobalsClassName.TryLoadClass<UAbilitySystemGlobals>();
    TestNotNull(TEXT("配置的 Globals 类可解析"), ConfiguredGlobalsClass);
    TestTrue(TEXT("Globals 配置指向框架层子类（落位生效）"), ConfiguredGlobalsClass == UGameAbilitySystemGlobals::StaticClass());

    const UGameAbilitySystemGlobals* Globals = GetDefault<UGameAbilitySystemGlobals>();
    TUniquePtr<FGameplayEffectContext> Context(Globals->AllocGameplayEffectContext());
    TestNotNull(TEXT("上下文分配成功"), Context.Get());
    if (!Context.IsValid()) {
        return false;
    }
    TestTrue(TEXT("分配出的上下文为本工程脚本结构"), Context->GetScriptStruct() == FGameGameplayEffectContext::StaticStruct());

    FGameGameplayEffectContext* Source = static_cast<FGameGameplayEffectContext*>(Context.Get());
    Source->SetActualDamage(37.5f);

    const FGameplayTag CriticalTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.Critical")));
    const FGameplayTag FatalTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.Fatal")));
    const FGameplayTag HitStopTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.HitStop")));
    const FGameplayTag ChannelPhysTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.Physical")));

    Source->AddFeedbackTag(CriticalTag);
    Source->SetDynamicValue(HitStopTag, 0.12f);
    Source->SetActualChannelDamage(ChannelPhysTag, 37.5f);

    TestTrue(TEXT("上下文包含反馈标签"), Source->HasFeedbackTag(CriticalTag));
    TestFalse(TEXT("上下文未添加标签不存在"), Source->HasFeedbackTag(FatalTag));
    TestEqual(TEXT("上下文获取动态数值正确"), Source->GetDynamicValue(HitStopTag), 0.12f);
    TestEqual(TEXT("上下文获取通道实际伤害正确"), Source->GetActualChannelDamage(ChannelPhysTag), 37.5f);

    TUniquePtr<FGameplayEffectContext> Duplicated(Source->Duplicate());
    const FGameGameplayEffectContext* Copy = static_cast<const FGameGameplayEffectContext*>(Duplicated.Get());
    TestNotNull(TEXT("Duplicate 返回有效上下文"), Copy);
    if (Copy == nullptr) {
        return false;
    }
    TestTrue(TEXT("Duplicate 保持本工程脚本结构"), Copy->GetScriptStruct() == FGameGameplayEffectContext::StaticStruct());
    TestEqual(TEXT("Duplicate 复制实际伤害"), Copy->GetActualDamage(), 37.5f);
    TestTrue(TEXT("Duplicate 复制反馈标签"), Copy->HasFeedbackTag(CriticalTag));
    TestEqual(TEXT("Duplicate 复制动态数值"), Copy->GetDynamicValue(HitStopTag), 0.12f);
    TestEqual(TEXT("Duplicate 复制通道实际伤害"), Copy->GetActualChannelDamage(ChannelPhysTag), 37.5f);

    FBufferArchive Writer;
    bool bWriteSuccess = false;
    TestTrue(TEXT("写出 NetSerialize 成功"), Source->NetSerialize(Writer, nullptr, bWriteSuccess));
    TestTrue(TEXT("写出成功标记置位"), bWriteSuccess);

    FMemoryReader Reader(Writer);
    FGameGameplayEffectContext Loaded;
    bool bReadSuccess = false;
    TestTrue(TEXT("读入 NetSerialize 成功"), Loaded.NetSerialize(Reader, nullptr, bReadSuccess));
    TestTrue(TEXT("读入成功标记置位"), bReadSuccess);
    TestEqual(TEXT("NetSerialize 往返保持实际伤害"), Loaded.GetActualDamage(), 37.5f);
    TestTrue(TEXT("NetSerialize 往返保持反馈标签"), Loaded.HasFeedbackTag(CriticalTag));
    TestEqual(TEXT("NetSerialize 往返保持动态数值"), Loaded.GetDynamicValue(HitStopTag), 0.12f);
    TestEqual(TEXT("NetSerialize 往返保持通道伤害"), Loaded.GetActualChannelDamage(ChannelPhysTag), 37.5f);

    const FGameDamageResult Result = Source->ToDamageResult();
    TestEqual(TEXT("ToDamageResult 总伤害对齐"), Result.TotalActualDamage, 37.5f);
    TestTrue(TEXT("ToDamageResult 反馈标签对齐"), Result.HasFeedbackTag(CriticalTag));
    TestEqual(TEXT("ToDamageResult 动态数值对齐"), Result.GetDynamicValue(HitStopTag), 0.12f);
    TestEqual(TEXT("ToDamageResult 通道实际伤害对齐"), Result.GetActualChannelDamageExact(ChannelPhysTag), 37.5f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBaseAttributeSetTest,
    "Game.Gameplay.AttributeSet",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameBaseAttributeSetTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TestEqual(TEXT("ClampAttribute 上限钳制"), UGameBaseAttributeSet::ClampAttribute(120.0f, 0.0f, 100.0f), 100.0f);
    TestEqual(TEXT("ClampAttribute 下限钳制"), UGameBaseAttributeSet::ClampAttribute(-20.0f, 0.0f, 100.0f), 0.0f);
    TestEqual(TEXT("ClampAttribute 范围内直通"), UGameBaseAttributeSet::ClampAttribute(50.0f, 0.0f, 100.0f), 50.0f);

    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(GetTransientPackage());
    const FGameplayTag ThresholdTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Threshold.LowHealth")));

    TestTrue(TEXT("首次达阈授予松标签并返回 true"), UGameBaseAttributeSet::BroadcastThresholdOnce(ASC, ThresholdTag));
    TestTrue(TEXT("ASC 持有临界标签"), ASC->HasMatchingGameplayTag(ThresholdTag));
    TestFalse(TEXT("再次达阈幂等短路返回 false"), UGameBaseAttributeSet::BroadcastThresholdOnce(ASC, ThresholdTag));

    UGameBaseAttributeSet::ResetThreshold(ASC, ThresholdTag);
    TestFalse(TEXT("重置后 ASC 不再持有临界标签"), ASC->HasMatchingGameplayTag(ThresholdTag));
    TestTrue(TEXT("重置后可再次达阈返回 true"), UGameBaseAttributeSet::BroadcastThresholdOnce(ASC, ThresholdTag));

    FGameplayAttributeData MetaData;
    MetaData.SetBaseValue(85.0f);
    MetaData.SetCurrentValue(85.0f);
    TestEqual(TEXT("MoveMetaAttribute 提取当前数值"), UGameBaseAttributeSet::MoveMetaAttribute(MetaData), 85.0f);
    TestEqual(TEXT("MoveMetaAttribute 提取后归零 Base"), MetaData.GetBaseValue(), 0.0f);
    TestEqual(TEXT("MoveMetaAttribute 提取后归零 Current"), MetaData.GetCurrentValue(), 0.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAttributeValueMappingTest,
    "Game.Gameplay.ValueMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameAttributeValueMappingTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(GetTransientPackage());
    UAbilitySystemTestAttributeSet* AttrSet = NewObject<UAbilitySystemTestAttributeSet>(ASC);
    ASC->AddAttributeSetSubobject(AttrSet);

    const FGameplayAttribute HealthAttr(FindFieldChecked<FProperty>(UAbilitySystemTestAttributeSet::StaticClass(), GET_MEMBER_NAME_CHECKED(UAbilitySystemTestAttributeSet, Health)));
    ASC->SetNumericAttributeBase(HealthAttr, 150.0f);

    float ObservedNew = 0.0f;
    float ObservedOld = 0.0f;
    int32 BroadcastCount = 0;

    UGameplayAttributeValueMapping* Mapping = UGameplayAttributeValueMapping::CreateAttributeValueMapping(ASC, HealthAttr, true);

    Mapping->OnValueChangedNative.AddLambda([&](float NewVal, float OldVal) {
        ObservedNew = NewVal;
        ObservedOld = OldVal;
        ++BroadcastCount;
    });

    Mapping->QueryAndBroadcast();
    TestEqual(TEXT("立即广播获得正确初值"), ObservedNew, 150.0f);
    TestEqual(TEXT("获取当前值正确"), Mapping->GetCurrentValue(), 150.0f);
    TestTrue(TEXT("广播次数增加"), BroadcastCount >= 1);

    ASC->SetNumericAttributeBase(HealthAttr, 200.0f);
    TestEqual(TEXT("属性变动触发映射广播"), ObservedNew, 200.0f);

    UGameBaseAttributeSet::ApplyAttributeToLocal(ASC, HealthAttr, -50.0f);
    TestEqual(TEXT("ApplyAttributeToLocal 落地增量正确"), ASC->GetNumericAttributeBase(HealthAttr), 150.0f);

    Mapping->Unbind();
    TestEqual(TEXT("解绑后数值查询返回 0"), Mapping->GetCurrentValue(), 0.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAbilitySystemBootstrapTest,
    "Game.Gameplay.Bootstrap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameAbilitySystemBootstrapTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameAbilitySystemBootstrap* Bootstrap = NewObject<UGameAbilitySystemBootstrap>(GetTransientPackage());
    UGameAbilitySystemComponent* ASC1 = NewObject<UGameAbilitySystemComponent>(GetTransientPackage());
    UGameAbilitySystemComponent* ASC2 = NewObject<UGameAbilitySystemComponent>(GetTransientPackage());

    UAbilitySystemComponent* CurrentTargetASC = ASC1;
    Bootstrap->SetResolveAbilitySystemComponentDelegate(FResolveAbilitySystemComponent::CreateLambda([&CurrentTargetASC]() {
        return CurrentTargetASC;
    }));

    int32 InitialAttributesWriteCount = 0;
    Bootstrap->SetWriteInitialAttributesDelegate(FWriteInitialAttributes::CreateLambda([&InitialAttributesWriteCount](UAbilitySystemComponent*) {
        ++InitialAttributesWriteCount;
    }));

    AActor* DummyAvatar1 = NewObject<AActor>(GetTransientPackage());
    AActor* DummyAvatar2 = NewObject<AActor>(GetTransientPackage());

    Bootstrap->EnsureInitialized(DummyAvatar1);
    TestTrue(TEXT("初次引导写入初值"), Bootstrap->IsInitialAttributesWritten());
    TestEqual(TEXT("初值写入回调触发一次"), InitialAttributesWriteCount, 1);

    Bootstrap->EnsureInitialized(DummyAvatar1);
    TestEqual(TEXT("同 Avatar 重复调用幂等保持"), InitialAttributesWriteCount, 1);

    CurrentTargetASC = ASC2;
    Bootstrap->EnsureInitialized(DummyAvatar2);
    TestTrue(TEXT("换 ASC 重新引导写入初值"), Bootstrap->IsInitialAttributesWritten());
    TestEqual(TEXT("新 ASC 重置状态并再次写入初值"), InitialAttributesWriteCount, 2);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDamageChannelRegistryTest,
    "Game.Gameplay.DamageChannelRegistry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

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
    TestTrue(TEXT("绑定通道标签正确"), Bindings[0].ChannelTag.MatchesTagExact(ChannelTag));

    TestTrue(
        TEXT("同通道重复注册覆盖而非叠加"),
        UGameDamageExecution::RegisterRuntimeChannelBinding(ExecutionClass, ChannelTag, TargetAttribute, EGameplayModOp::Multiplicitive)
    );
    Registry.GetBindings(ExecutionClass, Bindings);
    TestEqual(TEXT("重复注册后绑定数量仍为 1"), Bindings.Num(), 1);
    TestTrue(TEXT("重复注册后修饰操作已覆盖"), Bindings[0].ModOp == EGameplayModOp::Multiplicitive);

    TestTrue(
        TEXT("注册第二条通道成功"),
        UGameDamageExecution::RegisterRuntimeChannelBinding(ExecutionClass, SecondChannelTag, TargetAttribute)
    );
    Registry.GetBindings(ExecutionClass, Bindings);
    TestEqual(TEXT("两条通道合并查询正确"), Bindings.Num(), 2);

    TestTrue(TEXT("运行期注销通道成功"), UGameDamageExecution::UnregisterRuntimeChannelBinding(ExecutionClass, ChannelTag));
    Registry.GetBindings(ExecutionClass, Bindings);
    TestEqual(TEXT("注销后剩余一条通道"), Bindings.Num(), 1);
    TestFalse(TEXT("重复注销返回失败"), UGameDamageExecution::UnregisterRuntimeChannelBinding(ExecutionClass, ChannelTag));

    Registry.UnregisterAll(ExecutionClass);
    Registry.GetBindings(ExecutionClass, Bindings);
    TestEqual(TEXT("清理后无残留绑定"), Bindings.Num(), 0);

    const FDamageModifier CombinedModifier(2.0f, 20.0f);
    TestEqual(TEXT("伤害修饰器算子顺序为先加后乘"), CombinedModifier.Evaluate(100.0f), 240.0f);
    const FDamageModifier OverrideModifier = FDamageModifier::MakeOverride(42.0f);
    TestEqual(TEXT("伤害修饰器覆写模式优先生效"), OverrideModifier.Evaluate(100.0f), 42.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameEffectContextNetSerializeLimitTest,
    "Game.Gameplay.EffectContextLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameEffectContextNetSerializeLimitTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameGameplayEffectContext Source;
    const int32 OverflowCount = FGameGameplayEffectContext::MaxNetSerializedEntries + 8;

    for (int32 Index = 0; Index < OverflowCount; ++Index) {
        const FGameplayTag EntryTag = UGameplayTagsManager::Get().AddNativeGameplayTag(
            FName(*FString::Printf(TEXT("Damage.Channel.LimitTest.%d"), Index))
        );
        Source.SetActualChannelDamage(EntryTag, static_cast<float>(Index));
        Source.SetDynamicValue(EntryTag, static_cast<float>(Index) * 0.5f);
    }

    TestEqual(TEXT("源上下文通道数超过上限"), Source.GetActualChannels().Num(), OverflowCount);

    FBufferArchive Writer;
    bool bWriteSuccess = false;
    TestTrue(TEXT("超限上下文写出成功"), Source.NetSerialize(Writer, nullptr, bWriteSuccess));
    TestTrue(TEXT("写出成功标记置位"), bWriteSuccess);

    FMemoryReader Reader(Writer);
    FGameGameplayEffectContext Loaded;
    bool bReadSuccess = false;
    TestTrue(TEXT("超限上下文读入成功"), Loaded.NetSerialize(Reader, nullptr, bReadSuccess));
    TestTrue(TEXT("读入成功标记置位"), bReadSuccess);
    TestEqual(TEXT("通道明细截断至网络上限"), Loaded.GetActualChannels().Num(), FGameGameplayEffectContext::MaxNetSerializedEntries);
    TestEqual(TEXT("动态数值截断至网络上限"), Loaded.GetDynamicValues().Num(), FGameGameplayEffectContext::MaxNetSerializedEntries);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayAbilityPolicyTest,
    "Game.Gameplay.AbilityPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameGameplayAbilityPolicyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const UGameGameplayAbility* CDO = GetDefault<UGameGameplayAbility>();
    TestNotNull(TEXT("Ability CDO 有效"), CDO);
    if (CDO == nullptr) {
        return false;
    }

    TestTrue(TEXT("默认实例化策略为 InstancedPerActor 以支持网络复制"),
        CDO->GetInstancingPolicy() == EGameplayAbilityInstancingPolicy::InstancedPerActor);
    TestTrue(TEXT("默认网络复制策略为 ReplicateYes"),
        CDO->GetReplicationPolicy() == EGameplayAbilityReplicationPolicy::ReplicateYes);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
