// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/GameFeedbackDefinitionProvider.h"

#include "Asset/GameFeedbackDefinition.h"
#include "Asset/GameFeedbackRegistry.h"
#include "Config/GameFeedbackSettings.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Logging/GameLogCategories.h"

/**
 * @brief 按信号主通道与反馈标签解析最优表现资产
 * @param Signal 表现信号
 * @return 可安全同步访问的定义指针；注册表未配置时返回 nullptr
 */
const UGameFeedbackDefinition* UGameFeedbackDefinitionProvider::Resolve(const FGameFeedbackSignal& Signal)
{
    if (!LoadedRegistry) {
        return nullptr;
    }

    TSoftObjectPtr<UGameFeedbackDefinition> DefPtr = LoadedRegistry->ResolveDefinitionPtr(Signal.PrimaryChannel, Signal.FeedbackTags);
    if (DefPtr.IsNull()) {
        return LoadedRegistry->DefaultFallbackDefinition.Get();
    }

    if (const UGameFeedbackDefinition* Definition = ResolveDefinition(DefPtr)) {
        return Definition;
    }

    return LoadedRegistry->DefaultFallbackDefinition.Get();
}

/**
 * @brief 查询已登记 Definition 的稳定网络 ID
 * @param Definition 待查询资产
 * @return 非零 ID；未登记、冲突或 Registry 未配置时返回 0
 */
uint32 UGameFeedbackDefinitionProvider::GetDefinitionId(const UGameFeedbackDefinition* Definition) const
{
    return LoadedRegistry ? LoadedRegistry->GetDefinitionId(Definition) : 0u;
}

/**
 * @brief 获取当前 Registry 的确定性映射版本
 * @return 映射版本；Registry 未配置时返回 0
 */
uint32 UGameFeedbackDefinitionProvider::GetDefinitionMappingVersion() const
{
    return LoadedRegistry ? LoadedRegistry->GetDefinitionMappingVersion() : 0u;
}

/**
 * @brief 按稳定 ID 与映射版本还原服务端最终 Definition
 * @param DefinitionId 服务端最终 Definition 稳定 ID
 * @param DefinitionMappingVersion 服务端 Registry 映射版本
 * @return 同一 Definition；未知、冲突、版本不一致或未加载时返回 nullptr
 */
const UGameFeedbackDefinition* UGameFeedbackDefinitionProvider::ResolveById(uint32 DefinitionId, uint32 DefinitionMappingVersion)
{
    if (!LoadedRegistry) {
        return nullptr;
    }
    if (DefinitionMappingVersion != LoadedRegistry->GetDefinitionMappingVersion()) {
        GAME_LOG_THROTTLE(1.0, Warning, TEXT("【GameFeedback】拒绝映射版本不一致的网络事件：远端=%u，本地=%u"),
            DefinitionMappingVersion, LoadedRegistry->GetDefinitionMappingVersion());
        return nullptr;
    }

    TSoftObjectPtr<UGameFeedbackDefinition> Definition = LoadedRegistry->FindDefinitionById(DefinitionId);
    if (Definition.IsNull()) {
        GAME_LOG_THROTTLE(1.0, Warning, TEXT("【GameFeedback】拒绝未知或冲突的 Definition ID：%u"), DefinitionId);
        return nullptr;
    }
    return ResolveDefinition(Definition);
}

/**
 * @brief 批量预热软引用定义资产
 * @param Definitions 待异步加载的定义软指针集合
 */
void UGameFeedbackDefinitionProvider::PreloadFeedbackBundle(const TArray<TSoftObjectPtr<UGameFeedbackDefinition>>& Definitions)
{
    for (const auto& SoftDef : Definitions) {
        if (!SoftDef.IsNull()) {
            ResolveDefinition(SoftDef);
        }
    }
}

/**
 * @brief 仅在游戏世界创建（包含 Dedicated Server）
 * @param Outer 外层对象
 * @return 是否创建
 */
bool UGameFeedbackDefinitionProvider::ShouldCreateSubsystem(UObject* Outer) const
{
    if (!Super::ShouldCreateSubsystem(Outer)) {
        return false;
    }
    const UWorld* World = Outer ? Outer->GetWorld() : nullptr;
    return World && World->IsGameWorld();
}

/**
 * @brief 初始化：加载全局表现注册表
 * @param Collection 子系统管理器集合
 */
void UGameFeedbackDefinitionProvider::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    const UGameFeedbackSettings* Settings = GetDefault<UGameFeedbackSettings>();
    if (Settings && !Settings->GlobalRegistry.IsNull()) {
        LoadedRegistry = Settings->GlobalRegistry.LoadSynchronous();
        if (!LoadedRegistry) {
            GAME_LOG(Error, TEXT("【GameFeedback】GlobalRegistry 同步加载失败，请检查项目配置！"));
        } else {
            LoadedRegistry->RebuildDefinitionIndex();
        }
    }
}

/**
 * @brief 注销：取消异步加载并释放注册表、句柄与强缓存
 */
void UGameFeedbackDefinitionProvider::Deinitialize()
{
    for (auto& Pair : DefinitionLoads) {
        Pair.Value->CancelHandle();
    }
    DefinitionLoads.Empty();
    LoadedDefinitions.Empty();
    LoadedRegistry = nullptr;
    Super::Deinitialize();
}

/**
 * @brief 安全获取当前世界的定义提供者
 * @param WorldContextObject 世界上下文对象
 * @return 服务指针；未就绪时返回 nullptr
 */
UGameFeedbackDefinitionProvider* UGameFeedbackDefinitionProvider::GetPtr(const UObject* WorldContextObject)
{
    if (!WorldContextObject) {
        return nullptr;
    }
    const UWorld* World = WorldContextObject->GetWorld();
    return World ? World->GetSubsystem<UGameFeedbackDefinitionProvider>() : nullptr;
}

/**
 * @brief 解析软定义，命中强缓存或已加载对象时写入缓存并返回，否则按路径去重发起异步请求
 * @param Definition 待解析软定义
 * @return 已加载定义；加载中或加载失败时返回 nullptr
 */
const UGameFeedbackDefinition* UGameFeedbackDefinitionProvider::ResolveDefinition(const TSoftObjectPtr<UGameFeedbackDefinition>& Definition)
{
    const FSoftObjectPath Path = Definition.ToSoftObjectPath();
    if (const TObjectPtr<UGameFeedbackDefinition>* CachedDefinition = LoadedDefinitions.Find(Path)) {
        return CachedDefinition->Get();
    }
    if (Definition.IsValid()) {
        LoadedDefinitions.Add(Path, Definition.Get());
        return Definition.Get();
    }
    if (!DefinitionLoads.Contains(Path)) {
        DefinitionLoads.Add(Path, UAssetManager::GetStreamableManager().RequestAsyncLoad(Path));
    }
    return nullptr;
}
