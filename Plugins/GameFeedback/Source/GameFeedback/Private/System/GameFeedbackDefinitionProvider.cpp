// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/GameFeedbackDefinitionProvider.h"

#include "Asset/GameFeedbackDefinition.h"
#include "Asset/GameFeedbackRegistry.h"
#include "Config/GameFeedbackSettings.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Feedback/GameFeedbackSignal.h"
#include "Logging/GameLogCategories.h"
#include "System/GameFeedbackInternalHelpers.h"

/**
 * @brief 按信号主通道与反馈标签解析最优表现资产
 * @param Signal 表现信号
 * @return 可安全同步访问的定义指针；注册表未配置时返回 nullptr，并在配置缺失路径输出一次性 Warning
 */
const UGameFeedbackDefinition* UGameFeedbackDefinitionProvider::Resolve(const FGameFeedbackSignal& Signal) {
    if (!LoadedRegistry) {
        if (GetDefault<UGameFeedbackSettings>()->GlobalRegistry.IsNull()) {
            WarnMissingGlobalRegistryOnce();
        }
        return nullptr;
    }

    const TSoftObjectPtr<UGameFeedbackDefinition> DefPtr = LoadedRegistry->ResolveDefinitionPtr(Signal.PrimaryChannel, Signal.FeedbackTags);
    if (DefPtr.IsNull()) {
        return nullptr;
    }

    return ResolveDefinition(DefPtr);
}

/**
 * @brief O(1) 查询已登记 Definition 的稳定网络 ID（带版本校验的指针直查缓存）
 * @param Definition 待查询资产
 * @return 非零 ID；未登记、冲突或 Registry 未配置时返回 0
 */
uint32 UGameFeedbackDefinitionProvider::GetDefinitionId(const UGameFeedbackDefinition* Definition) const {
    if (!LoadedRegistry || !Definition) {
        return 0u;
    }

    const uint32 CurrentVersion = LoadedRegistry->GetDefinitionMappingVersion();
    if (CachedMappingVersion != CurrentVersion) {
        DefinitionIdCache.Reset();
        CachedMappingVersion = CurrentVersion;
    }

    const TObjectKey<UGameFeedbackDefinition> Key(Definition);
    if (const uint32* CachedId = DefinitionIdCache.Find(Key)) {
        return *CachedId;
    }

    const uint32 ResolvedId = LoadedRegistry->GetDefinitionId(Definition);
    DefinitionIdCache.Add(Key, ResolvedId);
    return ResolvedId;
}

/**
 * @brief 获取当前 Registry 的确定性映射版本
 * @return 映射版本；Registry 未配置时返回 0
 */
uint32 UGameFeedbackDefinitionProvider::GetDefinitionMappingVersion() const {
    return LoadedRegistry ? LoadedRegistry->GetDefinitionMappingVersion() : 0u;
}

/**
 * @brief 按稳定 ID 与映射版本还原服务端最终 Definition
 * @param DefinitionId 服务端最终 Definition 稳定 ID
 * @param DefinitionMappingVersion 服务端 Registry 映射版本
 * @return 同一 Definition；未知、冲突、版本不一致或未加载时返回 nullptr
 */
const UGameFeedbackDefinition* UGameFeedbackDefinitionProvider::ResolveById(uint32 DefinitionId, uint32 DefinitionMappingVersion) {
    if (!LoadedRegistry) {
        return nullptr;
    }
    if (DefinitionMappingVersion != LoadedRegistry->GetDefinitionMappingVersion()) {
        GAME_LOG_THROTTLE(1.0, Warning, TEXT("[Provider] 拒绝映射版本不一致的网络事件：远端=%u，本地=%u"),
            DefinitionMappingVersion, LoadedRegistry->GetDefinitionMappingVersion());
        return nullptr;
    }

    const TSoftObjectPtr<UGameFeedbackDefinition> Definition = LoadedRegistry->FindDefinitionById(DefinitionId);
    if (Definition.IsNull()) {
        GAME_LOG_THROTTLE(1.0, Warning, TEXT("[Provider] 拒绝未知或冲突的 Definition ID：%u"), DefinitionId);
        return nullptr;
    }
    return ResolveDefinition(Definition);
}

/**
 * @brief 批量预热软引用定义资产
 * @param Definitions 待异步加载的定义软指针集合
 */
void UGameFeedbackDefinitionProvider::PreloadFeedbackBundle(TConstArrayView<TSoftObjectPtr<UGameFeedbackDefinition>> Definitions) {
    for (const TSoftObjectPtr<UGameFeedbackDefinition>& SoftDef : Definitions) {
        if (!SoftDef.IsNull()) {
            ResolveDefinition(SoftDef);
        }
    }
}

/**
 * @brief 按路径去重异步预热通用视听软资产（VFX / Audio），加载完成后随世界生命周期保活并在注销时释放
 * @param AssetPath 目标软资产对象路径
 */
void UGameFeedbackDefinitionProvider::PreloadSoftAsset(const FSoftObjectPath& AssetPath) {
    if (!AssetPath.IsValid() || PendingMediaLoads.Contains(AssetPath)) {
        return;
    }

    if (const TObjectPtr<UObject>* CachedAsset = PreloadedMediaAssets.Find(AssetPath)) {
        if (IsValid(CachedAsset->Get())) {
            return;
        }
        PreloadedMediaAssets.Remove(AssetPath);
    }

    if (UObject* ResidentAsset = AssetPath.ResolveObject(); IsValid(ResidentAsset)) {
        PreloadedMediaAssets.Add(AssetPath, ResidentAsset);
        return;
    }

    TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
        AssetPath,
        FStreamableDelegate::CreateWeakLambda(this, [this, AssetPath]() {
            if (UObject* LoadedAsset = AssetPath.ResolveObject(); IsValid(LoadedAsset)) {
                PreloadedMediaAssets.Add(AssetPath, LoadedAsset);
            }
            PendingMediaLoads.Remove(AssetPath);
        })
    );

    if (!Handle.IsValid()) {
        return;
    }

    if (Handle->HasLoadCompleted()) {
        if (UObject* LoadedAsset = AssetPath.ResolveObject(); IsValid(LoadedAsset)) {
            PreloadedMediaAssets.Add(AssetPath, LoadedAsset);
        }
        return;
    }

    PendingMediaLoads.Add(AssetPath, MoveTemp(Handle));
}

/**
 * @brief 查询当前处于在飞状态的流式异步预热句柄总数（含 Definition 与视听软资产）
 * @return 在飞句柄数量
 */
int32 UGameFeedbackDefinitionProvider::GetActivePreloadHandleCount() const {
    return DefinitionLoads.Num() + PendingMediaLoads.Num();
}

/**
 * @brief 仅在游戏世界创建（包含 Dedicated Server）
 * @param Outer 外层对象
 * @return 是否创建
 */
bool UGameFeedbackDefinitionProvider::ShouldCreateSubsystem(UObject* Outer) const {
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
void UGameFeedbackDefinitionProvider::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);

    const UGameFeedbackSettings* Settings = GetDefault<UGameFeedbackSettings>();
    if (Settings && !Settings->GlobalRegistry.IsNull()) {
        LoadedRegistry = Settings->GlobalRegistry.LoadSynchronous();
        if (!LoadedRegistry) {
            GAME_LOG(Error, TEXT("[Provider] GlobalRegistry 同步加载失败，请检查项目配置！"));
        } else {
            LoadedRegistry->RebuildDefinitionIndex();
            CachedMappingVersion = LoadedRegistry->GetDefinitionMappingVersion();
        }
    }
}

/**
 * @brief 注销：取消全部异步加载并释放注册表、句柄与强缓存
 */
void UGameFeedbackDefinitionProvider::Deinitialize() {
    for (auto& Pair : DefinitionLoads) {
        if (Pair.Value.IsValid()) {
            Pair.Value->CancelHandle();
        }
    }
    for (auto& Pair : PendingMediaLoads) {
        if (Pair.Value.IsValid()) {
            Pair.Value->CancelHandle();
        }
    }
    DefinitionLoads.Empty();
    PendingMediaLoads.Empty();
    LoadedDefinitions.Empty();
    PreloadedMediaAssets.Empty();
    DefinitionIdCache.Empty();
    CachedMappingVersion = 0;
    LoadedRegistry = nullptr;
    bWarnedMissingGlobalRegistry = false;
    Super::Deinitialize();
}

/**
 * @brief 安全获取当前世界的定义提供者
 * @param WorldContextObject 世界上下文对象
 * @return 服务指针；未就绪时返回 nullptr
 */
UGameFeedbackDefinitionProvider* UGameFeedbackDefinitionProvider::GetPtr(const UObject* WorldContextObject) {
    const UWorld* World = GameFeedback::Internal::GetWorldFromContextSafe(WorldContextObject);
    return World ? World->GetSubsystem<UGameFeedbackDefinitionProvider>() : nullptr;
}

/**
 * @brief 解析软定义，命中强缓存或已加载对象时写入缓存并返回，否则按路径去重发起异步请求并闭环回收句柄
 * @param Definition 待解析软定义
 * @return 已加载定义；加载中或加载失败时返回 nullptr
 */
const UGameFeedbackDefinition* UGameFeedbackDefinitionProvider::ResolveDefinition(const TSoftObjectPtr<UGameFeedbackDefinition>& Definition) {
    const FSoftObjectPath Path = Definition.ToSoftObjectPath();
    if (const TObjectPtr<UGameFeedbackDefinition>* CachedDefinition = LoadedDefinitions.Find(Path)) {
        if (IsValid(CachedDefinition->Get())) {
            return CachedDefinition->Get();
        }
        LoadedDefinitions.Remove(Path);
    }
    if (Definition.IsValid()) {
        UGameFeedbackDefinition* LoadedAsset = Definition.Get();
        LoadedDefinitions.Add(Path, LoadedAsset);
        return LoadedAsset;
    }
    if (DefinitionLoads.Contains(Path)) {
        return nullptr;
    }
    return RequestDefinitionAsyncLoad(Path);
}

/**
 * @brief 为未驻留内存的 Definition 发起去重异步流式加载（正确处理同步完成竞态）
 * @param Path 目标 Definition 完整软对象路径
 * @return 若引擎同步完成加载则立即返回已解析指针，否则返回 nullptr
 */
const UGameFeedbackDefinition* UGameFeedbackDefinitionProvider::RequestDefinitionAsyncLoad(const FSoftObjectPath& Path) {
    TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
        Path,
        FStreamableDelegate::CreateWeakLambda(this, [this, Path]() {
            if (UGameFeedbackDefinition* LoadedAsset = Cast<UGameFeedbackDefinition>(Path.ResolveObject()); IsValid(LoadedAsset)) {
                LoadedDefinitions.Add(Path, LoadedAsset);
            }
            DefinitionLoads.Remove(Path);
        })
    );

    if (!Handle.IsValid()) {
        return nullptr;
    }

    if (Handle->HasLoadCompleted()) {
        if (const TObjectPtr<UGameFeedbackDefinition>* CachedDefinition = LoadedDefinitions.Find(Path);
            CachedDefinition && IsValid(CachedDefinition->Get())) {
            return CachedDefinition->Get();
        }
        if (UGameFeedbackDefinition* LoadedAsset = Cast<UGameFeedbackDefinition>(Path.ResolveObject()); IsValid(LoadedAsset)) {
            LoadedDefinitions.Add(Path, LoadedAsset);
            return LoadedAsset;
        }
        return nullptr;
    }

    DefinitionLoads.Add(Path, MoveTemp(Handle));
    return nullptr;
}

/**
 * @brief 配置缺失时输出一次性诊断，避免每次查询重复刷屏
 */
void UGameFeedbackDefinitionProvider::WarnMissingGlobalRegistryOnce() {
    if (bWarnedMissingGlobalRegistry) {
        return;
    }
    bWarnedMissingGlobalRegistry = true;
    GAME_LOG(Warning, TEXT("[Provider] 未配置 GlobalRegistry，表现解析将全部返回空；请在 Project Settings > Game Feedback > Registry 指定 GlobalRegistry 资产。"));
}
