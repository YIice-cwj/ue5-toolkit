/**
 * @file UGameSpatialSubsystem.cpp
 * @brief 世界级无物理引擎依赖 Actor 分层空间索引子系统实现
 */

#include "Subsystem/UGameSpatialSubsystem.h"

#include "Core/SpatialGameplayTags.h"
#include "GameFramework/Actor.h"
#include "Logging/GameLogCategories.h"

/**
 * @brief 子系统初始化生命周期
 * @param Collection 子系统集合上下文
 */
void UGameSpatialSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    ClearAll();
    GAME_LOG(Verbose, TEXT("[SpatialSubsystem] 世界空间索引子系统初始化完成。"));
}

/**
 * @brief 子系统反初始化生命周期（清空全部图层与追踪索引）
 */
void UGameSpatialSubsystem::Deinitialize() {
    ClearAll();
    GAME_LOG(Verbose, TEXT("[SpatialSubsystem] 世界空间索引子系统卸载清理完成。"));
    Super::Deinitialize();
}

/**
 * @brief 注册 Actor 到指定空间层（同层原地更新，跨层无缝迁移）
 * @param Actor 待注册的目标 Actor
 * @param LayerTag 空间所属层级标签
 * @return 注册成功返回 true
 */
bool UGameSpatialSubsystem::RegisterActor(AActor* Actor, FGameplayTag LayerTag) {
    if (!IsActorOperational(Actor)) {
        GAME_LOG(Verbose, TEXT("[SpatialSubsystem] 拒绝注册无效或正在销毁的 Actor。"));
        return false;
    }

    const FVector Location = Actor->GetActorLocation();
    if (!Game::Spatial::Private::IsFiniteVector(Location)) {
        GAME_LOG(Warning, TEXT("[SpatialSubsystem] 拒绝注册携带非有限坐标的 Actor: %s"), *Actor->GetName());
        return false;
    }

    const FGameplayTag ResolvedTag = ResolveLayerTag(LayerTag);
    const TWeakObjectPtr<AActor> WeakActor(Actor);

    if (FActorSpatialEntry* ExistingEntry = TrackedActors.Find(WeakActor)) {
        if (ExistingEntry->LayerTag == ResolvedTag) {
            LayeredGrids.FindChecked(ResolvedTag).Update(WeakActor, Location);
            ExistingEntry->LastLocation = Location;
            return true;
        }

        TSpatialHashGrid3D<TWeakObjectPtr<AActor>>& OldGrid = LayeredGrids.FindChecked(ExistingEntry->LayerTag);
        OldGrid.Remove(WeakActor);
        if (OldGrid.Num() == 0) {
            LayeredGrids.Remove(ExistingEntry->LayerTag);
        }

        LayeredGrids.FindOrAdd(ResolvedTag).Insert(WeakActor, Location);
        ExistingEntry->LayerTag = ResolvedTag;
        ExistingEntry->LastLocation = Location;
        return true;
    }

    LayeredGrids.FindOrAdd(ResolvedTag).Insert(WeakActor, Location);
    TrackedActors.Add(WeakActor, FActorSpatialEntry{ResolvedTag, Location});
    return true;
}

/**
 * @brief 批量注册一组 Actor 到指定空间层
 * @param Actors 待注册目标 Actor 数组
 * @param LayerTag 空间所属层级标签
 * @return 实际成功注册的有效 Actor 数量
 */
int32 UGameSpatialSubsystem::RegisterActors(const TArray<AActor*>& Actors, FGameplayTag LayerTag) {
    int32 RegisteredCount = 0;
    for (AActor* Actor : Actors) {
        if (RegisterActor(Actor, LayerTag)) {
            ++RegisteredCount;
        }
    }
    return RegisteredCount;
}

/**
 * @brief 从空间层注销 Actor（允许销毁中的 Actor 在 EndPlay 正常反注册）
 * @param Actor 待注销的目标 Actor
 * @return 注销成功返回 true
 */
bool UGameSpatialSubsystem::UnregisterActor(AActor* Actor) {
    if (Actor == nullptr) {
        return false;
    }

    const TWeakObjectPtr<AActor> WeakActor(Actor);
    FActorSpatialEntry Entry;
    if (!TrackedActors.RemoveAndCopyValue(WeakActor, Entry)) {
        return false;
    }

    TSpatialHashGrid3D<TWeakObjectPtr<AActor>>& Grid = LayeredGrids.FindChecked(Entry.LayerTag);
    Grid.Remove(WeakActor);
    if (Grid.Num() == 0) {
        LayeredGrids.Remove(Entry.LayerTag);
    }
    return true;
}

/**
 * @brief 批量从空间层注销一组 Actor
 * @param Actors 待注销的目标 Actor 数组
 * @return 实际成功注销的 Actor 数量
 */
int32 UGameSpatialSubsystem::UnregisterActors(const TArray<AActor*>& Actors) {
    int32 UnregisteredCount = 0;
    for (AActor* Actor : Actors) {
        if (UnregisterActor(Actor)) {
            ++UnregisteredCount;
        }
    }
    return UnregisteredCount;
}

/**
 * @brief 手动更新 Actor 的空间索引坐标
 * @param Actor 目标 Actor
 * @param NewLocation 最新的三维世界坐标
 * @return 更新成功返回 true
 */
bool UGameSpatialSubsystem::UpdateActorLocation(AActor* Actor, const FVector& NewLocation) {
    if (!IsActorOperational(Actor) || !Game::Spatial::Private::IsFiniteVector(NewLocation)) {
        return false;
    }

    const TWeakObjectPtr<AActor> WeakActor(Actor);
    FActorSpatialEntry* Entry = TrackedActors.Find(WeakActor);
    if (Entry == nullptr) {
        return false;
    }

    LayeredGrids.FindChecked(Entry->LayerTag).Update(WeakActor, NewLocation);
    Entry->LastLocation = NewLocation;
    return true;
}

/**
 * @brief 自动读取 Actor 当前世界坐标并同步更新索引
 * @param Actor 目标 Actor
 * @return 更新成功返回 true
 */
bool UGameSpatialSubsystem::UpdateActor(AActor* Actor) {
    if (!IsActorOperational(Actor)) {
        return false;
    }
    return UpdateActorLocation(Actor, Actor->GetActorLocation());
}

/**
 * @brief 三维球形范围检索存活 Actor
 * @param Center 球心世界坐标
 * @param Radius 查询半径
 * @param OutActors 输出查询到的有效存活 Actor 列表
 * @param LayerTag 目标空间层级
 */
void UGameSpatialSubsystem::QueryActorsInRadius(
    const FVector& Center,
    float Radius,
    TArray<AActor*>& OutActors,
    FGameplayTag LayerTag
) const {
    OutActors.Reset();
    ForEachActorInRadius(Center, Radius, [&OutActors](AActor* Actor) {
        OutActors.Add(Actor);
    }, LayerTag);
}

/**
 * @brief 三维锥体范围检索存活 Actor
 * @param Apex 锥体顶点世界坐标
 * @param Direction 锥体对称中轴朝向
 * @param Radius 锥体最大作用距离
 * @param HalfAngleDegrees 锥体半顶角（度数）
 * @param OutActors 输出查询到的有效存活 Actor 列表
 * @param LayerTag 目标空间层级
 */
void UGameSpatialSubsystem::QueryActorsInCone(
    const FVector& Apex,
    const FVector& Direction,
    float Radius,
    float HalfAngleDegrees,
    TArray<AActor*>& OutActors,
    FGameplayTag LayerTag
) const {
    OutActors.Reset();
    ForEachActorInCone(Apex, Direction, Radius, HalfAngleDegrees, [&OutActors](AActor* Actor) {
        OutActors.Add(Actor);
    }, LayerTag);
}

/**
 * @brief 三维直立圆柱体范围检索存活 Actor
 * @param Center 圆柱中心世界坐标
 * @param Radius 水平截面半径
 * @param HalfHeight 垂直半高
 * @param OutActors 输出查询到的有效存活 Actor 列表
 * @param LayerTag 目标空间层级
 */
void UGameSpatialSubsystem::QueryActorsInCylinder(
    const FVector& Center,
    float Radius,
    float HalfHeight,
    TArray<AActor*>& OutActors,
    FGameplayTag LayerTag
) const {
    OutActors.Reset();
    ForEachActorInCylinder(Center, Radius, HalfHeight, [&OutActors](AActor* Actor) {
        OutActors.Add(Actor);
    }, LayerTag);
}

/**
 * @brief 轴对齐三维包围盒 AABB 范围检索存活 Actor
 * @param Box 查询轴对齐包围盒
 * @param OutActors 输出查询到的有效存活 Actor 列表
 * @param LayerTag 目标空间层级
 */
void UGameSpatialSubsystem::QueryActorsInAABB(
    const FBox& Box,
    TArray<AActor*>& OutActors,
    FGameplayTag LayerTag
) const {
    OutActors.Reset();
    ForEachActorInAABB(Box, [&OutActors](AActor* Actor) {
        OutActors.Add(Actor);
    }, LayerTag);
}

/**
 * @brief 检索距离中心点最近的单一存活 Actor
 * @param Center 中心世界坐标
 * @param MaxSearchRadius 最大检索截断半径
 * @param OutNearestActor 输出检索到的最近有效存活 Actor
 * @param LayerTag 目标空间层级
 * @return 存在有效 Actor 返回 true，否则返回 false
 */
bool UGameSpatialSubsystem::QueryNearestActor(
    const FVector& Center,
    float MaxSearchRadius,
    AActor*& OutNearestActor,
    FGameplayTag LayerTag
) const {
    OutNearestActor = nullptr;
    const TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolveLayerTag(LayerTag));
    if (Grid == nullptr) {
        return false;
    }

    TWeakObjectPtr<AActor> NearestWeak;
    const bool bFound = Grid->QueryNearest(Center, MaxSearchRadius, NearestWeak, [](const TWeakObjectPtr<AActor>& Item) {
        return ResolveOperationalActor(Item) != nullptr;
    });
    if (!bFound) {
        return false;
    }

    OutNearestActor = NearestWeak.Get();
    return true;
}

/**
 * @brief 检索距离中心点最近的 K 个存活 Actor（按距离升序排列，零中间弱指针数组分配）
 * @param Center 中心世界坐标
 * @param MaxSearchRadius 最大检索截断半径
 * @param K 期望获取的最大近邻数量
 * @param OutActors 输出检索到的最近有效 Actor 列表
 * @param LayerTag 目标空间层级
 */
void UGameSpatialSubsystem::QueryKNearestActors(
    const FVector& Center,
    float MaxSearchRadius,
    int32 K,
    TArray<AActor*>& OutActors,
    FGameplayTag LayerTag
) const {
    OutActors.Reset();
    if (K <= 0) {
        return;
    }

    const TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolveLayerTag(LayerTag));
    if (Grid == nullptr) {
        return;
    }

    OutActors.Reserve(FMath::Min(K, Grid->Num()));
    Grid->QueryKNearestMatching(
        Center,
        MaxSearchRadius,
        K,
        [&OutActors](const TWeakObjectPtr<AActor>& WeakActor) {
            OutActors.Add(WeakActor.Get());
        },
        [](const TWeakObjectPtr<AActor>& Item) {
            return ResolveOperationalActor(Item) != nullptr;
        }
    );
}

/**
 * @brief O(LayerSize) 清空指定空间层中的全部索引
 * @param LayerTag 目标空间层级
 */
void UGameSpatialSubsystem::ClearLayer(FGameplayTag LayerTag) {
    const FGameplayTag ResolvedTag = ResolveLayerTag(LayerTag);
    const TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolvedTag);
    if (Grid == nullptr) {
        return;
    }

    Grid->ForEachElement([this](const TWeakObjectPtr<AActor>& WeakActor) {
        TrackedActors.Remove(WeakActor);
    });
    LayeredGrids.Remove(ResolvedTag);
}

/**
 * @brief 清空所有空间层的全部索引
 */
void UGameSpatialSubsystem::ClearAll() {
    LayeredGrids.Empty();
    TrackedActors.Empty();
}

/**
 * @brief O(LayerSize) 清理指定空间层中已被垃圾回收或销毁的弱引用条目
 * @param LayerTag 目标空间层级
 * @return 清理的失效 Actor 数量
 */
int32 UGameSpatialSubsystem::PruneStaleActors(FGameplayTag LayerTag) {
    const FGameplayTag ResolvedTag = ResolveLayerTag(LayerTag);
    TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolvedTag);
    if (Grid == nullptr) {
        return 0;
    }

    TArray<TWeakObjectPtr<AActor>, TInlineAllocator<32>> StaleActors;
    Grid->ForEachElement([&StaleActors](const TWeakObjectPtr<AActor>& WeakActor) {
        if (ResolveOperationalActor(WeakActor) == nullptr) {
            StaleActors.Add(WeakActor);
        }
    });

    for (const TWeakObjectPtr<AActor>& StaleActor : StaleActors) {
        Grid->Remove(StaleActor);
        TrackedActors.Remove(StaleActor);
    }

    if (Grid->Num() == 0) {
        LayeredGrids.Remove(ResolvedTag);
    }

    if (!StaleActors.IsEmpty()) {
        GAME_LOG(Verbose, TEXT("[SpatialSubsystem] 图层 [%s] 清理失效弱引用 Actor 数量: %d。"), *ResolvedTag.ToString(), StaleActors.Num());
    }
    return StaleActors.Num();
}

/**
 * @brief 一键清理所有空间层中已被垃圾回收或销毁的弱引用条目
 * @return 清理的失效 Actor 总数量
 */
int32 UGameSpatialSubsystem::PruneAllStaleActors() {
    int32 PrunedCount = 0;
    for (auto It = TrackedActors.CreateIterator(); It; ++It) {
        if (ResolveOperationalActor(It.Key()) != nullptr) {
            continue;
        }
        const FGameplayTag& EntryLayer = It.Value().LayerTag;
        TSpatialHashGrid3D<TWeakObjectPtr<AActor>>& Grid = LayeredGrids.FindChecked(EntryLayer);
        Grid.Remove(It.Key());
        if (Grid.Num() == 0) {
            LayeredGrids.Remove(EntryLayer);
        }
        It.RemoveCurrent();
        ++PrunedCount;
    }

    if (PrunedCount > 0) {
        GAME_LOG(Verbose, TEXT("[SpatialSubsystem] 全局清理跨图层失效弱引用 Actor 总数: %d。"), PrunedCount);
    }
    return PrunedCount;
}

/**
 * @brief 获取指定空间层中当前索引的条目数量
 * @param LayerTag 目标空间层级
 * @return 索引元素数量
 */
int32 UGameSpatialSubsystem::GetActorCount(FGameplayTag LayerTag) const {
    const TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolveLayerTag(LayerTag));
    return Grid != nullptr ? Grid->Num() : 0;
}

/**
 * @brief 获取所有空间层中当前索引的条目总数量
 * @return 索引元素总数量
 */
int32 UGameSpatialSubsystem::GetTotalActorCount() const {
    return TrackedActors.Num();
}

/**
 * @brief 查询有效存活 Actor 是否已注册到本子系统
 * @param Actor 目标 Actor
 * @return 存活且已注册返回 true
 */
bool UGameSpatialSubsystem::IsActorRegistered(const AActor* Actor) const {
    if (!IsActorOperational(Actor)) {
        return false;
    }
    return TrackedActors.Contains(Actor);
}

/**
 * @brief 查询已注册存活 Actor 所属的空间层级标签
 * @param Actor 目标 Actor
 * @return 层级标签（未注册或已销毁返回空标签）
 */
FGameplayTag UGameSpatialSubsystem::GetActorLayer(const AActor* Actor) const {
    if (!IsActorOperational(Actor)) {
        return FGameplayTag();
    }
    const FActorSpatialEntry* Entry = TrackedActors.Find(Actor);
    return Entry != nullptr ? Entry->LayerTag : FGameplayTag();
}

/**
 * @brief 查询已注册存活 Actor 最后同步的三维世界坐标
 * @param Actor 目标 Actor
 * @param OutLocation 输出空间坐标
 * @return 存活且已注册返回 true
 */
bool UGameSpatialSubsystem::GetActorLocation(const AActor* Actor, FVector& OutLocation) const {
    OutLocation = FVector::ZeroVector;
    if (!IsActorOperational(Actor)) {
        return false;
    }
    const FActorSpatialEntry* Entry = TrackedActors.Find(Actor);
    if (Entry == nullptr) {
        return false;
    }
    OutLocation = Entry->LastLocation;
    return true;
}

/**
 * @brief 获取指定空间层中当前索引的所有有效存活 Actor（零中间数组堆分配）
 * @param OutActors 输出存活的 Actor 列表
 * @param LayerTag 目标空间层级
 */
void UGameSpatialSubsystem::GetActorsInLayer(TArray<AActor*>& OutActors, FGameplayTag LayerTag) const {
    OutActors.Reset();
    const TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolveLayerTag(LayerTag));
    if (Grid == nullptr) {
        return;
    }

    OutActors.Reserve(Grid->Num());
    Grid->ForEachElement([&OutActors](const TWeakObjectPtr<AActor>& WeakActor) {
        if (AActor* Actor = ResolveOperationalActor(WeakActor)) {
            OutActors.Add(Actor);
        }
    });
}

/**
 * @brief 解析目标图层标签（传入空标签时回退至默认原生图层标签）
 * @param InTag 输入图层标签
 * @return 解析后的图层标签
 */
FGameplayTag UGameSpatialSubsystem::ResolveLayerTag(const FGameplayTag& InTag) const noexcept {
    if (InTag.IsValid()) {
        return InTag;
    }
    return GetDefaultLayerTag();
}

/**
 * @brief 获取默认空间图层原生标签（Spatial.Layer.Default）
 * @return 默认空间图层标签
 */
FGameplayTag UGameSpatialSubsystem::GetDefaultLayerTag() noexcept {
    return TAG_Spatial_Layer_Default.GetTag();
}

/**
 * @brief 校验目标 Actor 指针是否非空、有效且未进入销毁流程
 * @param Actor 待校验的 Actor 指针
 * @return 处于存活可用态返回 true
 */
bool UGameSpatialSubsystem::IsActorOperational(const AActor* Actor) noexcept {
    return ::IsValid(Actor) && !Actor->IsActorBeingDestroyed();
}
