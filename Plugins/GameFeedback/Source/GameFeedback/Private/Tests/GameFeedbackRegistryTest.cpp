/**
 * @file GameFeedbackRegistryTest.cpp
 * @brief 反馈注册表通道匹配、字典序仲裁、复合规则与 TokenStream 版本校验自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Asset/GameFeedbackDefinition.h"
#include "Asset/GameFeedbackRegistry.h"
#include "GameplayTagsManager.h"
#include "Tests/GameFeedbackTestHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackRegistryLookupTest,
    "Game.Feedback.RegistryLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Registry 按真实 GameplayTag 层级择优并按同深度字典序稳定决策
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackRegistryLookupTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackDefinition* DefaultDef = NewObject<UGameFeedbackDefinition>();
    UGameFeedbackDefinition* SlashDef = NewObject<UGameFeedbackDefinition>();

    Registry->DefaultFallbackDefinition = DefaultDef;

    const FGameplayTag SlashTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Physical.Slash")));
    const FGameplayTag FireTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Elemental.Fire")));

    Registry->ExactChannelMap.Add(SlashTag, SlashDef);

    UGameFeedbackDefinition* ShallowLongDef = NewObject<UGameFeedbackDefinition>();
    UGameFeedbackDefinition* DeepShortDef = NewObject<UGameFeedbackDefinition>();
    const FGameplayTag ShallowLongTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.ThisIsAVeryLongShallowTag")));
    const FGameplayTag DeepShortTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.A.B.C")));
    Registry->ExactFeedbackTagMap.Add(ShallowLongTag, ShallowLongDef);
    Registry->ExactFeedbackTagMap.Add(DeepShortTag, DeepShortDef);

    UGameFeedbackDefinition* AlphaDef = NewObject<UGameFeedbackDefinition>();
    UGameFeedbackDefinition* BetaDef = NewObject<UGameFeedbackDefinition>();
    const FGameplayTag AlphaTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.SameDepth.Alpha")));
    const FGameplayTag BetaTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.SameDepth.Beta")));
    Registry->ExactFeedbackTagMap.Add(BetaTag, BetaDef);
    Registry->ExactFeedbackTagMap.Add(AlphaTag, AlphaDef);

    const FGameplayTag DeeperNullTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.A.B.C.D")));
    Registry->ExactFeedbackTagMap.Add(DeeperNullTag, TSoftObjectPtr<UGameFeedbackDefinition>());
    Registry->RebuildDefinitionIndex();

    FGameplayTagContainer Tags;
    TSoftObjectPtr<UGameFeedbackDefinition> Found = Registry->ResolveDefinitionPtr(SlashTag, Tags);
    TestEqual(TEXT("精确 Channel 命中时必须返回对应 Definition"), Found.Get(), SlashDef);

    Found = Registry->ResolveDefinitionPtr(FireTag, Tags);
    TestEqual(TEXT("未配置 Channel 且无 Tag 时必须返回 Default 回退"), Found.Get(), DefaultDef);

    Tags.AddTag(ShallowLongTag);
    Tags.AddTag(DeepShortTag);
    TSoftObjectPtr<UGameFeedbackDefinition> FoundTag = Registry->ResolveDefinitionPtr(FireTag, Tags);
    TestEqual(TEXT("多 Tag 冲突时优先选择深度更深的 Tag（与插入顺序无关）"), FoundTag.Get(), DeepShortDef);

    Tags.AddTag(DeeperNullTag);
    FoundTag = Registry->ResolveDefinitionPtr(FireTag, Tags);
    TestEqual(TEXT("空 Definition 的更深标签必须在重建索引时被剔除"), FoundTag.Get(), DeepShortDef);

    Tags.Reset();
    Tags.AddTag(AlphaTag);
    Tags.AddTag(BetaTag);
    FoundTag = Registry->ResolveDefinitionPtr(FireTag, Tags);
    TestEqual(TEXT("同深度按标签字典序稳定选择（与插入顺序无关）"), FoundTag.Get(), AlphaDef);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackCompositeRulesTest,
    "Game.Feedback.CompositeRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Registry 组合规则 TagQuery 匹配、稳定排序及 TokenStream 版本敏感性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackCompositeRulesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackDefinition* DefaultDef = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/CompositeTest/Default.Default"));
    UGameFeedbackDefinition* LowPriorityDef = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/CompositeTest/Low.Low"));
    UGameFeedbackDefinition* HighPriorityDef = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/CompositeTest/High.High"));
    UGameFeedbackDefinition* ExactChannelDef = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/CompositeTest/Channel.Channel"));

    Registry->DefaultFallbackDefinition = DefaultDef;

    const FGameplayTag TagA = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.Composite.TagA")));
    const FGameplayTag TagB = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.Composite.TagB")));
    const FGameplayTag ExactChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Composite.ExactChannel")));

    Registry->ExactChannelMap.Add(ExactChannel, ExactChannelDef);

    FGameFeedbackCompositeEntry LowEntry;
    LowEntry.Priority = 10;
    LowEntry.TagQuery = FGameplayTagQuery::MakeQuery_MatchAnyTags(FGameplayTagContainer(TagA));
    LowEntry.TagQuery.SetUserDescription(TEXT("SharedUserDesc"));
    LowEntry.Definition = LowPriorityDef;
    Registry->CompositeRules.Add(LowEntry);

    FGameFeedbackCompositeEntry HighEntry;
    HighEntry.Priority = 50;
    HighEntry.TagQuery = FGameplayTagQuery::MakeQuery_MatchAllTags(FGameplayTagContainer::CreateFromArray(TArray<FGameplayTag>{TagA, TagB}));
    HighEntry.Definition = HighPriorityDef;
    Registry->CompositeRules.Add(HighEntry);

    Registry->RebuildDefinitionIndex();
    const uint32 VersionBeforeQueryEdit = Registry->GetDefinitionMappingVersion();

    TestEqual(TEXT("组合规则必须按 Priority 降序排序"), Registry->CompositeRules[0].Priority, 50);
    TestEqual(TEXT("组合规则低优先级项排在第二位"), Registry->CompositeRules[1].Priority, 10);

    FGameplayTagContainer SignalTags;
    SignalTags.AddTag(TagA);
    const FGameplayTag UnmatchedChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Composite.Unmatched")));

    TSoftObjectPtr<UGameFeedbackDefinition> MatchedDef = Registry->ResolveDefinitionPtr(UnmatchedChannel, SignalTags);
    TestEqual(TEXT("单标签命中低优先级组合规则"), MatchedDef.Get(), LowPriorityDef);

    SignalTags.AddTag(TagB);
    MatchedDef = Registry->ResolveDefinitionPtr(UnmatchedChannel, SignalTags);
    TestEqual(TEXT("双标签满足高优先级组合规则并覆盖低优先级"), MatchedDef.Get(), HighPriorityDef);

    MatchedDef = Registry->ResolveDefinitionPtr(ExactChannel, SignalTags);
    TestEqual(TEXT("精确伤害通道必须优于组合规则匹配"), MatchedDef.Get(), ExactChannelDef);

    FGameplayTagContainer EmptyTags;
    MatchedDef = Registry->ResolveDefinitionPtr(UnmatchedChannel, EmptyTags);
    TestEqual(TEXT("未匹配任何组合规则时退回默认兜底资产"), MatchedDef.Get(), DefaultDef);

    Registry->CompositeRules[1].TagQuery = FGameplayTagQuery::MakeQuery_MatchAnyTags(FGameplayTagContainer(TagB));
    Registry->CompositeRules[1].TagQuery.SetUserDescription(TEXT("SharedUserDesc"));
    Registry->RebuildDefinitionIndex();
    TestNotEqual(
        TEXT("即使 UserDescription 未变，修改 TagQuery 条件后映射版本必须变化"),
        Registry->GetDefinitionMappingVersion(),
        VersionBeforeQueryEdit);

    UGameFeedbackRegistry* TieOrderA = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackRegistry* TieOrderB = NewObject<UGameFeedbackRegistry>();
    FGameFeedbackCompositeEntry RuleFirst;
    RuleFirst.Priority = 20;
    RuleFirst.TagQuery = FGameplayTagQuery::MakeQuery_MatchAnyTags(FGameplayTagContainer(TagA));
    RuleFirst.Definition = LowPriorityDef;
    FGameFeedbackCompositeEntry RuleSecond;
    RuleSecond.Priority = 20;
    RuleSecond.TagQuery = FGameplayTagQuery::MakeQuery_MatchAnyTags(FGameplayTagContainer(TagA));
    RuleSecond.Definition = HighPriorityDef;

    TieOrderA->CompositeRules = {RuleFirst, RuleSecond};
    TieOrderB->CompositeRules = {RuleSecond, RuleFirst};
    TieOrderA->RebuildDefinitionIndex();
    TieOrderB->RebuildDefinitionIndex();
    TestNotEqual(
        TEXT("同优先级复合规则求值顺序不同必须产出不同映射版本"),
        TieOrderA->GetDefinitionMappingVersion(),
        TieOrderB->GetDefinitionMappingVersion());

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
