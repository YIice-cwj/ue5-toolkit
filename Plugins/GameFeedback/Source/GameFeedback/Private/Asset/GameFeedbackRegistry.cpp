// Copyright Epic Games, Inc. All Rights Reserved.

#include "Asset/GameFeedbackRegistry.h"

#include "Algo/StableSort.h"
#include "GameplayTagsManager.h"
#include "Logging/GameLogCategories.h"
#include "Misc/Crc.h"

namespace {

/**
 * @brief 计算 GameplayTag 在真实节点树中的层级深度
 * @param Tag 待计算标签
 * @return 节点深度；未登记标签返回 0
 */
int32 GetTagDepth(const FGameplayTag& Tag) {
    int32 Depth = 0;
    for (TSharedPtr<FGameplayTagNode> Node = UGameplayTagsManager::Get().FindTagNode(Tag);
        Node.IsValid();
        Node = Node->GetParentTagNode()) {
        ++Depth;
    }
    return Depth;
}

/**
 * @brief 提取 FGameplayTagQuery 的规范化结构指纹（基于真实表达式 TokenStream 与标签字典，不受可选 Description 干扰）
 * @param Query 复合标签查询对象
 * @return 确定性结构化指纹字符串
 */
FString BuildTagQuerySignature(const FGameplayTagQuery& Query) {
    if (Query.IsEmpty()) {
        return TEXT("Empty");
    }

    FGameplayTagQueryExpression RootExpr;
    Query.GetQueryExpr(RootExpr);

    TArray<uint8> TokenStream;
    TArray<FGameplayTag> TagDictionary;
    RootExpr.EmitTokens(TokenStream, TagDictionary);

    FString Signature;
    Signature.Reserve(TokenStream.Num() * 2 + TagDictionary.Num() * 24 + 16);
    Signature.Append(BytesToHex(TokenStream.GetData(), TokenStream.Num()));
    for (const FGameplayTag& Tag : TagDictionary) {
        Signature.AppendChar(TEXT(','));
        Signature.Append(Tag.ToString());
    }
    return Signature;
}

struct FSortableFeedbackTagEntry {
    FGameplayTag Tag;
    TSoftObjectPtr<UGameFeedbackDefinition> Definition;
    int32 Depth = 0;
};

}  // namespace

/**
 * @brief 资产加载后重建复合规则顺序、反馈标签有序索引与稳定 Definition 索引
 */
void UGameFeedbackRegistry::PostLoad() {
    Super::PostLoad();
    RebuildDefinitionIndex();
}

#if WITH_EDITOR
/**
 * @brief 编辑器属性变更后自动同步重建有序索引与确定性映射版本
 * @param PropertyChangedEvent 属性变更事件
 */
void UGameFeedbackRegistry::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) {
    Super::PostEditChangeProperty(PropertyChangedEvent);
    RebuildDefinitionIndex();
}
#endif

/**
 * @brief 匹配最优 Definition 软引用（精确通道 → 最具体反馈标签 → 复合规则 → 缺省资产）
 * @param PrimaryChannel 主伤害通道
 * @param FeedbackTags 特征标签集合
 * @return 最优资产软指针；若均未匹配则返回 DefaultFallbackDefinition
 */
TSoftObjectPtr<UGameFeedbackDefinition> UGameFeedbackRegistry::ResolveDefinitionPtr(
    const FGameplayTag& PrimaryChannel,
    const FGameplayTagContainer& FeedbackTags) const {
    if (PrimaryChannel.IsValid()) {
        const TSoftObjectPtr<UGameFeedbackDefinition>* Found = ExactChannelMap.Find(PrimaryChannel);
        if (Found != nullptr && !Found->IsNull()) {
            return *Found;
        }
    }

    if (!FeedbackTags.IsEmpty()) {
        for (const auto& Pair : SortedExactFeedbackTags) {
            if (FeedbackTags.HasTag(Pair.Key)) {
                return Pair.Value;
            }
        }
    }

    for (const FGameFeedbackCompositeEntry& Entry : CompositeRules) {
        if (!Entry.Definition.IsNull() && Entry.TagQuery.Matches(FeedbackTags)) {
            return Entry.Definition;
        }
    }

    return DefaultFallbackDefinition;
}

/**
 * @brief 重建复合规则优先级顺序、反馈标签有序索引（深度降序、同深度字典序升序）及 Definition 索引、冲突集合与映射版本
 */
void UGameFeedbackRegistry::RebuildDefinitionIndex() {
    Algo::StableSort(CompositeRules, [](const FGameFeedbackCompositeEntry& A, const FGameFeedbackCompositeEntry& B) {
        return A.Priority > B.Priority;
    });

    TArray<FSortableFeedbackTagEntry> SortableTags;
    SortableTags.Reserve(ExactFeedbackTagMap.Num());
    for (const auto& Pair : ExactFeedbackTagMap) {
        if (Pair.Key.IsValid() && !Pair.Value.IsNull()) {
            SortableTags.Add(FSortableFeedbackTagEntry{ Pair.Key, Pair.Value, GetTagDepth(Pair.Key) });
        }
    }
    SortableTags.Sort([](const FSortableFeedbackTagEntry& A, const FSortableFeedbackTagEntry& B) {
        if (A.Depth != B.Depth) {
            return A.Depth > B.Depth;
        }
        return A.Tag.GetTagName().LexicalLess(B.Tag.GetTagName());
    });

    SortedExactFeedbackTags.Reset(SortableTags.Num());
    for (FSortableFeedbackTagEntry& Entry : SortableTags) {
        SortedExactFeedbackTags.Emplace(Entry.Tag, MoveTemp(Entry.Definition));
    }

    DefinitionIdByPath.Reset();
    DefinitionsById.Reset();
    ConflictedDefinitionIds.Reset();

    RegisterDefinitionIdentity(DefaultFallbackDefinition);
    for (const auto& Pair : ExactChannelMap) {
        RegisterDefinitionIdentity(Pair.Value);
    }
    for (const auto& Pair : ExactFeedbackTagMap) {
        RegisterDefinitionIdentity(Pair.Value);
    }
    for (const FGameFeedbackCompositeEntry& Entry : CompositeRules) {
        RegisterDefinitionIdentity(Entry.Definition);
    }

    TArray<FString> MappingParts;
    const int32 TotalEntries = (DefaultFallbackDefinition ? 1 : 0)
        + ExactChannelMap.Num()
        + ExactFeedbackTagMap.Num()
        + CompositeRules.Num();
    MappingParts.Reserve(TotalEntries);
    if (DefaultFallbackDefinition) {
        MappingParts.Add(FString::Printf(TEXT("Default|%s"), *FSoftObjectPath(DefaultFallbackDefinition).ToString()));
    }
    for (const auto& Pair : ExactChannelMap) {
        MappingParts.Add(FString::Printf(TEXT("Channel|%s|%s"), *Pair.Key.ToString(), *Pair.Value.ToSoftObjectPath().ToString()));
    }
    for (const auto& Pair : ExactFeedbackTagMap) {
        MappingParts.Add(FString::Printf(TEXT("Feedback|%s|%s"), *Pair.Key.ToString(), *Pair.Value.ToSoftObjectPath().ToString()));
    }
    for (int32 RuleIndex = 0; RuleIndex < CompositeRules.Num(); ++RuleIndex) {
        const FGameFeedbackCompositeEntry& Entry = CompositeRules[RuleIndex];
        const FString QuerySignature = BuildTagQuerySignature(Entry.TagQuery);
        MappingParts.Add(FString::Printf(TEXT("Composite|%d|%d|%s|%s"),
            RuleIndex, Entry.Priority, *QuerySignature, *Entry.Definition.ToSoftObjectPath().ToString()));
    }
    MappingParts.Sort();

    const uint32 Hash = FCrc::StrCrc32(*FString::Join(MappingParts, TEXT("\n")));
    DefinitionMappingVersion = MappingParts.IsEmpty() ? 0u : (Hash == 0 ? 1u : Hash);
}

/**
 * @brief O(1) 查询已登记且无冲突的 Definition 网络 ID
 * @param Definition 待查询资产
 * @return 非零网络 ID；未登记或冲突时返回 0
 */
uint32 UGameFeedbackRegistry::GetDefinitionId(const UGameFeedbackDefinition* Definition) const {
    if (!Definition) {
        return 0u;
    }

    const uint32* FoundId = DefinitionIdByPath.Find(FSoftObjectPath(Definition));
    return FoundId != nullptr ? *FoundId : 0u;
}

/**
 * @brief 按稳定网络 ID 查询已登记 Definition
 * @param DefinitionId 网络 Definition ID
 * @return 对应软引用；未知或冲突 ID 返回空
 */
TSoftObjectPtr<UGameFeedbackDefinition> UGameFeedbackRegistry::FindDefinitionById(uint32 DefinitionId) const {
    if (DefinitionId == 0 || ConflictedDefinitionIds.Contains(DefinitionId)) {
        return nullptr;
    }

    const TSoftObjectPtr<UGameFeedbackDefinition>* Found = DefinitionsById.Find(DefinitionId);
    return Found ? *Found : nullptr;
}

/**
 * @brief 获取当前映射的确定性版本
 * @return 非零映射版本；空映射返回 0
 */
uint32 UGameFeedbackRegistry::GetDefinitionMappingVersion() const {
    return DefinitionMappingVersion;
}

/**
 * @brief 根据完整资产路径生成确定性 Definition ID
 * @param DefinitionPath 完整软对象路径
 * @return 非零稳定 ID；空路径返回 0
 */
uint32 UGameFeedbackRegistry::MakeDefinitionId(const FSoftObjectPath& DefinitionPath) {
    const FString PathString = DefinitionPath.ToString();
    if (PathString.IsEmpty()) {
        return 0u;
    }

    const uint32 Hash = FCrc::StrCrc32(*PathString);
    return Hash == 0 ? 1u : Hash;
}

/**
 * @brief 注册单条 Definition 路径并执行 CRC32 碰撞检测
 * @param Definition 待登记资产软引用
 */
void UGameFeedbackRegistry::RegisterDefinitionIdentity(const TSoftObjectPtr<UGameFeedbackDefinition>& Definition) {
    if (Definition.IsNull()) {
        return;
    }

    const FSoftObjectPath Path = Definition.ToSoftObjectPath();
    const uint32 DefinitionId = MakeDefinitionId(Path);
    if (DefinitionId == 0u || ConflictedDefinitionIds.Contains(DefinitionId)) {
        return;
    }

    if (const TSoftObjectPtr<UGameFeedbackDefinition>* Existing = DefinitionsById.Find(DefinitionId)) {
        const FSoftObjectPath ExistingPath = Existing->ToSoftObjectPath();
        if (ExistingPath != Path) {
            ConflictedDefinitionIds.Add(DefinitionId);
            DefinitionsById.Remove(DefinitionId);
            DefinitionIdByPath.Remove(ExistingPath);
            GAME_LOG(Warning, TEXT("[Registry] Definition CRC32 冲突，相关资产禁止网络解析：%s 与 %s"),
                *ExistingPath.ToString(), *Path.ToString());
        }
        return;
    }

    DefinitionsById.Add(DefinitionId, Definition);
    DefinitionIdByPath.Add(Path, DefinitionId);
}
