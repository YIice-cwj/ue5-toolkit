/**
 * @file GameFeedbackDefinitionProvider.h
 * @brief 表现资产解析与流式预热服务（唯一定义来源）
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Feedback/GameFeedbackSignal.h"
#include "UObject/SoftObjectPtr.h"

#include "GameFeedbackDefinitionProvider.generated.h"

class UGameFeedbackDefinition;
class UGameFeedbackRegistry;
struct FStreamableHandle;

/**
 * @brief 负责 Registry 加载、最优定义解析与软引用预热的世界服务
 */
UCLASS()
class GAMEFEEDBACK_API UGameFeedbackDefinitionProvider : public UWorldSubsystem {
    GENERATED_BODY()

public:
    /**
     * @brief 按信号主通道与反馈标签解析最优表现资产
     * @param Signal 表现信号
     * @return 可安全同步访问的定义指针；注册表未配置时返回 nullptr
     */
    const UGameFeedbackDefinition* Resolve(const FGameFeedbackSignal& Signal);

    /**
     * @brief 查询已登记 Definition 的稳定网络 ID
     * @param Definition 待查询资产
     * @return 非零 ID；未登记、冲突或 Registry 未配置时返回 0
     */
    uint32 GetDefinitionId(const UGameFeedbackDefinition* Definition) const;

    /**
     * @brief 获取当前 Registry 的确定性映射版本
     * @return 映射版本；Registry 未配置时返回 0
     */
    uint32 GetDefinitionMappingVersion() const;

    /**
     * @brief 按稳定 ID 与映射版本还原服务端最终 Definition
     * @param DefinitionId 服务端最终 Definition 稳定 ID
     * @param DefinitionMappingVersion 服务端 Registry 映射版本
     * @return 同一 Definition；未知、冲突、版本不一致或未加载时返回 nullptr
     */
    const UGameFeedbackDefinition* ResolveById(uint32 DefinitionId, uint32 DefinitionMappingVersion);

    /**
     * @brief 批量预热软引用定义资产
     * @param Definitions 待异步加载的定义软指针集合
     */
    void PreloadFeedbackBundle(const TArray<TSoftObjectPtr<UGameFeedbackDefinition>>& Definitions);

    /**
     * @brief 仅在游戏世界创建（包含 Dedicated Server）
     * @param Outer 外层对象
     * @return 是否创建
     */
    bool ShouldCreateSubsystem(UObject* Outer) const override;

    /**
     * @brief 初始化：加载全局表现注册表
     * @param Collection 子系统管理器集合
     */
    void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * @brief 注销：取消异步加载并释放注册表、句柄与强缓存
     */
    void Deinitialize() override;

private:
    /**
     * @brief 解析软定义，命中强缓存或已加载对象时直接返回，否则去重发起异步加载
     * @param Definition 待解析软定义
     * @return 已加载定义；加载中或加载失败时返回 nullptr
     */
    const UGameFeedbackDefinition* ResolveDefinition(const TSoftObjectPtr<UGameFeedbackDefinition>& Definition);

public:
    /**
     * @brief 安全获取当前世界的定义提供者
     * @param WorldContextObject 世界上下文对象
     * @return 服务指针；未就绪时返回 nullptr
     */
    static UGameFeedbackDefinitionProvider* GetPtr(const UObject* WorldContextObject);

private:
    UPROPERTY()
    TObjectPtr<UGameFeedbackRegistry> LoadedRegistry = nullptr;

    UPROPERTY()
    TMap<FSoftObjectPath, TObjectPtr<UGameFeedbackDefinition>> LoadedDefinitions;

    TMap<FSoftObjectPath, TSharedPtr<FStreamableHandle>> DefinitionLoads;
};
