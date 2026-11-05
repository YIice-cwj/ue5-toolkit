// Copyright Epic Games, Inc. All Rights Reserved.

#include "Asset/GameFeedbackRegistry.h"

#include "GameplayTagsManager.h"
#include "Logging/GameLogCategories.h"
#include "Misc/Crc.h"

namespace {

/**
 * @brief 计算 GameplayTag 在真实节点树中的层级深度
 * @param Tag 待计算标签
 * @return 节点深度；未登记标签返回 0
 */
int32 GetTagDepth(const FGameplayTag& Tag)
{
    int32 Depth = 0;
    for (TSharedPtr<FGameplayTagNode> Node = UGameplayTagsManager::Get().FindTagNode(Tag);
        Node.IsValid();
        Node = Node->GetParentTagNode()) {
        ++Depth;
    }
    return Depth;
}

}

/**
 * @brief 资产加载后重建复合规则顺序、反馈标签有序索引与稳定 Definition 索引
 */
void UGameFeedbackRegistry::PostLoad()
{
    Super::PostLoad();
    RebuildDefinitionIndex();
}

/**
 * @brief 匹配最优 Definition 软引用（精确通道 → 最具体反馈标签 → 复合规则 → 缺省资产）
 * @param PrimaryChannel 主伤害通道
 * @param FeedbackTags 特征标签集合
 * @return 最优资产软指针；若均未匹配则返回 DefaultFallbackDefinition
 *
 * 反馈标签与复合规则阶段依赖 RebuildDefinitionIndex 预建的有序索引（PostLoad 与 Provider 初始化时自动重建），
 * 首个命中的反馈标签即为“深度最深、同深度字典序最小”的最优项。
 */
TSoftObjectPtr<UGameFeedbackDefinition> UGameFeedbackRegistry::ResolveDefinitionPtr(
    const FGameplayTag& PrimaryChannel,
    const FGameplayTagContainer& FeedbackTags) const
{
    if (PrimaryChannel.IsValid()) {
        if (const TSoftObjectPtr<UGameFeedbackDefinition>* Found = ExactChannelMap.Find(PrimaryChannel)) {
            if (!Found->IsNull()) {
                return *Found;
            }
        }
    }

    for (const auto& Pair : SortedExactFeedbackTags) {
        if (FeedbackTags.HasTag(Pair.Key)) {
            return Pair.Value;
        }
    }

    for (const FGameFeedbackCompositeEntry& Entry : CompositeRules) {
        if (Entry.TagQuery.Matches(FeedbackTags)) {
            if (!Entry.Definition.IsNull()) {
                return Entry.Definition;
            }
        }
    }

    return DefaultFallbackDefinition;
}

/**
 * @brief 重建复合规则优先级顺序、反馈标签有序索引（深度降序、同深度字典序升序）及 Definition 索引、冲突集合与映射版本
 */
void UGameFeedbackRegistry::RebuildDefinitionIndex()
{
    CompositeRules.Sort([](const FGameFeedbackCompositeEntry& A, const FGameFeedbackCompositeEntry& B) {
        return A.Priority > B.Priority;
    });

    SortedExactFeedbackTags.Reset();
    SortedExactFeedbackTags.Reserve(ExactFeedbackTagMap.Num());
    for (const auto& Pair : ExactFeedbackTagMap) {
        if (Pair.Key.IsValid() && !Pair.Value.IsNull()) {
            SortedExactFeedbackTags.Emplace(Pair.Key, Pair.Value);
        }
    }
    SortedExactFeedbackTags.Sort([](const TPair<FGameplayTag, TSoftObjectPtr<UGameFeedbackDefinition>>& A,
                                   const TPair<FGameplayTag, TSoftObjectPtr<UGameFeedbackDefinition>>& B) {
        const int32 DepthA = GetTagDepth(A.Key);
        const int32 DepthB = GetTagDepth(B.Key);
        if (DepthA != DepthB) {
            return DepthA > DepthB;
        }
        return A.Key.GetTagName().LexicalLess(B.Key.GetTagName());
    });

    RegisteredDefinitionPaths.Reset();
    DefinitionsById.Reset();
    ConflictedDefinitionIds.Reset();

    auto RegisterDefinition = [this](const TSoftObjectPtr<UGameFeedbackDefinition>& Definition) {
        if (Definition.IsNull()) {
            return;
        }

        const FSoftObjectPath Path = Definition.ToSoftObjectPath();
        RegisteredDefinitionPaths.Add(Path);
        const uint32 DefinitionId = MakeDefinitionId(Path);
        if (const TSoftObjectPtr<UGameFeedbackDefinition>* Existing = DefinitionsById.Find(DefinitionId)) {
            if (Existing->ToSoftObjectPath() != Path) {
                const FString ExistingPath = Existing->ToSoftObjectPath().ToString();
                ConflictedDefinitionIds.Add(DefinitionId);
                DefinitionsById.Remove(DefinitionId);
                GAME_LOG(Warning, TEXT("【GameFeedback】Definition CRC32 冲突，相关资产禁止网络解析：%s 与 %s"),
                    *ExistingPath, *Path.ToString());
            }
            return;
        }
        if (!ConflictedDefinitionIds.Contains(DefinitionId)) {
            DefinitionsById.Add(DefinitionId, Definition);
        }
    };

    RegisterDefinition(DefaultFallbackDefinition);
    for (const auto& Pair : ExactChannelMap) {
        RegisterDefinition(Pair.Value);
    }
    for (const auto& Pair : ExactFeedbackTagMap) {
        RegisterDefinition(Pair.Value);
    }
    for (const FGameFeedbackCompositeEntry& Entry : CompositeRules) {
        RegisterDefinition(Entry.Definition);
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
    for (const FGameFeedbackCompositeEntry& Entry : CompositeRules) {
        MappingParts.Add(FString::Printf(TEXT("Composite|%d|%s|%s"),
            Entry.Priority, *Entry.TagQuery.GetDescription(), *Entry.Definition.ToSoftObjectPath().ToString()));
    }
    MappingParts.Sort();
    const uint32 Hash = FCrc::StrCrc32(*FString::Join(MappingParts, TEXT("\n")));
    DefinitionMappingVersion = MappingParts.IsEmpty() ? 0u : (Hash == 0 ? 1u : Hash);
}

/**
 * @brief 查询已登记且无冲突的 Definition 网络 ID
 * @param Definition 待查询资产
 * @return 非零网络 ID；未登记或冲突时返回 0
 */
uint32 UGameFeedbackRegistry::GetDefinitionId(const UGameFeedbackDefinition* Definition) const
{
    if (!Definition) {
        return 0;
    }

    const FSoftObjectPath Path(Definition);
    if (!RegisteredDefinitionPaths.Contains(Path)) {
        return 0;
    }

    const uint32 DefinitionId = MakeDefinitionId(Path);
    return ConflictedDefinitionIds.Contains(DefinitionId) ? 0u : DefinitionId;
}

/**
 * @brief 按稳定网络 ID 查询已登记 Definition
 * @param DefinitionId 网络 Definition ID
 * @return 对应软引用；未知或冲突 ID 返回空
 */
TSoftObjectPtr<UGameFeedbackDefinition> UGameFeedbackRegistry::FindDefinitionById(uint32 DefinitionId) const
{
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
uint32 UGameFeedbackRegistry::GetDefinitionMappingVersion() const
{
    return DefinitionMappingVersion;
}

/**
 * @brief 根据完整资产路径生成确定性 Definition ID
 * @param DefinitionPath 完整软对象路径
 * @return 非零稳定 ID；空路径返回 0
 */
uint32 UGameFeedbackRegistry::MakeDefinitionId(const FSoftObjectPath& DefinitionPath)
{
    const FString PathString = DefinitionPath.ToString();
    if (PathString.IsEmpty()) {
        return 0;
    }

    const uint32 Hash = FCrc::StrCrc32(*PathString);
    return Hash == 0 ? 1u : Hash;
}

