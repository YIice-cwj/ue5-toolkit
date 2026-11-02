/**
 * @file GameFeedbackRegistry.h
 * @brief 全局打击感资产映射注册表与双层索引匹配
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Asset/GameFeedbackDefinition.h"

#include "GameFeedbackRegistry.generated.h"

/**
 * @brief 复合规则轻量元数据条目（常驻内存，资产本体懒加载）
 */
USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackCompositeEntry {
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, Category = "Matching")
    FGameplayTagQuery TagQuery;

    UPROPERTY(EditDefaultsOnly, Category = "Matching")
    int32 Priority = 0;

    UPROPERTY(EditDefaultsOnly, Category = "Matching")
    TSoftObjectPtr<UGameFeedbackDefinition> Definition;
};

/**
 * @brief 全局反馈资产映射注册表
 */
UCLASS()
class GAMEFEEDBACK_API UGameFeedbackRegistry : public UDataAsset {
    GENERATED_BODY()

public:
    /**
     * @brief 资产加载后重建复合规则顺序与稳定 Definition 索引
     */
    void PostLoad() override;

    /**
     * @brief 匹配最优 Definition 软引用
     * @param PrimaryChannel 主伤害通道
     * @param FeedbackTags 特征标签集合
     * @return 最优资产软指针；若均未匹配则返回 DefaultFallbackDefinition
     */
    TSoftObjectPtr<UGameFeedbackDefinition> ResolveDefinitionPtr(
        const FGameplayTag& PrimaryChannel,
        const FGameplayTagContainer& FeedbackTags) const;

    UPROPERTY(EditDefaultsOnly, Category = "Defaults")
    TObjectPtr<UGameFeedbackDefinition> DefaultFallbackDefinition;

    UPROPERTY(EditDefaultsOnly, Category = "Tier1_FastPath")
    TMap<FGameplayTag, TSoftObjectPtr<UGameFeedbackDefinition>> ExactChannelMap;

    UPROPERTY(EditDefaultsOnly, Category = "Tier1_FastPath")
    TMap<FGameplayTag, TSoftObjectPtr<UGameFeedbackDefinition>> ExactFeedbackTagMap;

    /**
     * @brief 根据完整资产路径生成确定性 Definition ID
     * @param DefinitionPath 完整软对象路径
     * @return 非零稳定 ID；空路径返回 0
     */
    static uint32 MakeDefinitionId(const FSoftObjectPath& DefinitionPath);

    /**
     * @brief 重建全部映射来源的 Definition 索引、冲突集合与映射版本
     */
    void RebuildDefinitionIndex();

    /**
     * @brief 查询已登记且无冲突的 Definition 网络 ID
     * @param Definition 待查询资产
     * @return 非零网络 ID；未登记或冲突时返回 0
     */
    uint32 GetDefinitionId(const UGameFeedbackDefinition* Definition) const;

    /**
     * @brief 按稳定网络 ID 查询已登记 Definition
     * @param DefinitionId 网络 Definition ID
     * @return 对应软引用；未知或冲突 ID 返回空
     */
    TSoftObjectPtr<UGameFeedbackDefinition> FindDefinitionById(uint32 DefinitionId) const;

    /**
     * @brief 获取当前映射的确定性版本
     * @return 非零映射版本；空映射返回 0
     */
    uint32 GetDefinitionMappingVersion() const;

    UPROPERTY(EditDefaultsOnly, Category = "Tier2_CompositeRules")
    TArray<FGameFeedbackCompositeEntry> CompositeRules;

private:
    TSet<FSoftObjectPath> RegisteredDefinitionPaths;
    TMap<uint32, TSoftObjectPtr<UGameFeedbackDefinition>> DefinitionsById;
    TSet<uint32> ConflictedDefinitionIds;
    uint32 DefinitionMappingVersion = 0;
};

