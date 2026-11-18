#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "Spatial/TSpatialHashGrid3D.h"
#include "UGameSpatialSubsystem.generated.h"

class AActor;

/**
 * @brief 世界级空间索引子系统
 *
 * 基于无物理引擎依赖的三维空间哈希网格，提供轻量高效的 Actor 分层空间索引与多形态范围查询。
 * 内部统一采用弱引用持有 Actor，绝对避免阻碍垃圾回收或悬垂指针。
 */
UCLASS()
class GAMESPATIAL_API UGameSpatialSubsystem : public UWorldSubsystem {
    GENERATED_BODY()

public:
    struct FActorSpatialEntry {
        FGameplayTag LayerTag;
        FVector LastLocation = FVector::ZeroVector;
    };

    static const FGameplayTag& GetDefaultLayerTag();

    UGameSpatialSubsystem();
    virtual ~UGameSpatialSubsystem() override = default;

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

    /**
     * @brief 注册 Actor 到指定空间层
     * @param Actor 待注册的目标 Actor
     * @param LayerTag 空间所属层级标签（为空时归入默认层）
     * @return 注册成功返回 true，Actor 无效返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    bool RegisterActor(AActor* Actor, FGameplayTag LayerTag = FGameplayTag());

    /**
     * @brief 批量注册一组 Actor 到指定空间层
     * @param Actors 待注册目标 Actor 数组
     * @param LayerTag 空间所属层级标签（为空时归入默认层）
     * @return 实际成功注册的有效 Actor 数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    int32 RegisterActors(const TArray<AActor*>& Actors, FGameplayTag LayerTag = FGameplayTag());

    /**
     * @brief 从空间层注销 Actor
     * @param Actor 待注销的目标 Actor
     * @return 注销成功返回 true，未注册或 Actor 无效返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    bool UnregisterActor(AActor* Actor);

    /**
     * @brief 批量从空间层注销一组 Actor
     * @param Actors 待注销的目标 Actor 数组
     * @return 实际成功注销的 Actor 数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    int32 UnregisterActors(const TArray<AActor*>& Actors);

    /**
     * @brief 手动更新 Actor 的空间位置
     * @param Actor 目标 Actor
     * @param NewLocation 最新的三维世界坐标
     * @return 更新成功返回 true，未注册或 Actor 无效返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    bool UpdateActorLocation(AActor* Actor, const FVector& NewLocation);

    /**
     * @brief 自动读取 Actor 当前世界坐标并更新索引
     * @param Actor 目标 Actor
     * @return 更新成功返回 true，未注册或 Actor 无效返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    bool UpdateActor(AActor* Actor);

    /**
     * @brief 三维球形范围检索 Actor
     * @param Center 球心世界坐标
     * @param Radius 查询半径
     * @param LayerTag 目标空间层级（为空时查询默认层）
     * @param OutActors 输出查询到的有效 Actor 列表
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    void QueryActorsInRadius(
        const FVector& Center,
        float Radius,
        FGameplayTag LayerTag,
        TArray<AActor*>& OutActors
    ) const;

    /**
     * @brief 三维球形范围检索默认层 Actor
     * @param Center 球心世界坐标
     * @param Radius 查询半径
     * @param OutActors 输出查询到的有效 Actor 列表
     */
    void QueryActorsInRadius(
        const FVector& Center,
        float Radius,
        TArray<AActor*>& OutActors
    ) const {
        QueryActorsInRadius(Center, Radius, FGameplayTag(), OutActors);
    }

    /**
     * @brief 三维锥体范围检索 Actor（常用于视锥范围与前向攻击判定）
     * @param Apex 锥体顶点世界坐标
     * @param Direction 锥体对称中轴朝向
     * @param Radius 锥体最大作用距离
     * @param HalfAngleDegrees 锥体半顶角（度数）
     * @param LayerTag 目标空间层级（为空时查询默认层）
     * @param OutActors 输出查询到的有效 Actor 列表
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    void QueryActorsInCone(
        const FVector& Apex,
        const FVector& Direction,
        float Radius,
        float HalfAngleDegrees,
        FGameplayTag LayerTag,
        TArray<AActor*>& OutActors
    ) const;

    /**
     * @brief 三维锥体范围检索默认层 Actor
     * @param Apex 锥体顶点世界坐标
     * @param Direction 锥体对称中轴朝向
     * @param Radius 锥体最大作用距离
     * @param HalfAngleDegrees 锥体半顶角（度数）
     * @param OutActors 输出查询到的有效 Actor 列表
     */
    void QueryActorsInCone(
        const FVector& Apex,
        const FVector& Direction,
        float Radius,
        float HalfAngleDegrees,
        TArray<AActor*>& OutActors
    ) const {
        QueryActorsInCone(Apex, Direction, Radius, HalfAngleDegrees, FGameplayTag(), OutActors);
    }

    /**
     * @brief 三维直立圆柱体范围检索 Actor（常用于地面水平技能范围判定）
     * @param Center 圆柱中心世界坐标
     * @param Radius 水平截面半径
     * @param HalfHeight 垂直半高
     * @param LayerTag 目标空间层级（为空时查询默认层）
     * @param OutActors 输出查询到的有效 Actor 列表
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    void QueryActorsInCylinder(
        const FVector& Center,
        float Radius,
        float HalfHeight,
        FGameplayTag LayerTag,
        TArray<AActor*>& OutActors
    ) const;

    /**
     * @brief 三维直立圆柱体范围检索默认层 Actor
     * @param Center 圆柱中心世界坐标
     * @param Radius 水平截面半径
     * @param HalfHeight 垂直半高
     * @param OutActors 输出查询到的有效 Actor 列表
     */
    void QueryActorsInCylinder(
        const FVector& Center,
        float Radius,
        float HalfHeight,
        TArray<AActor*>& OutActors
    ) const {
        QueryActorsInCylinder(Center, Radius, HalfHeight, FGameplayTag(), OutActors);
    }

    /**
     * @brief 轴对齐三维包围盒 AABB 范围检索 Actor
     * @param Box 查询轴对齐包围盒
     * @param LayerTag 目标空间层级（为空时查询默认层）
     * @param OutActors 输出查询到的有效 Actor 列表
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    void QueryActorsInAABB(
        const FBox& Box,
        FGameplayTag LayerTag,
        TArray<AActor*>& OutActors
    ) const;

    /**
     * @brief 轴对齐三维包围盒 AABB 范围检索默认层 Actor
     * @param Box 查询轴对齐包围盒
     * @param OutActors 输出查询到的有效 Actor 列表
     */
    void QueryActorsInAABB(
        const FBox& Box,
        TArray<AActor*>& OutActors
    ) const {
        QueryActorsInAABB(Box, FGameplayTag(), OutActors);
    }

    /**
     * @brief 检索距离中心点最近的单一 Actor
     * @param Center 中心世界坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param LayerTag 目标空间层级（为空时查询默认层）
     * @param OutNearestActor 输出检索到的最近有效 Actor
     * @return 存在有效 Actor 返回 true，否则返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    bool QueryNearestActor(
        const FVector& Center,
        float MaxSearchRadius,
        FGameplayTag LayerTag,
        AActor*& OutNearestActor
    ) const;

    /**
     * @brief 检索默认层中距离中心点最近的单一 Actor
     * @param Center 中心世界坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param OutNearestActor 输出检索到的最近有效 Actor
     * @return 存在有效 Actor 返回 true，否则返回 false
     */
    bool QueryNearestActor(
        const FVector& Center,
        float MaxSearchRadius,
        AActor*& OutNearestActor
    ) const {
        return QueryNearestActor(Center, MaxSearchRadius, FGameplayTag(), OutNearestActor);
    }

    /**
     * @brief 检索距离中心点最近的 K 个 Actor（按距离升序排列）
     * @param Center 中心世界坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param K 期望获取的最大近邻数量
     * @param LayerTag 目标空间层级（为空时查询默认层）
     * @param OutActors 输出检索到的最近有效 Actor 列表（由近及远升序）
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    void QueryKNearestActors(
        const FVector& Center,
        float MaxSearchRadius,
        int32 K,
        FGameplayTag LayerTag,
        TArray<AActor*>& OutActors
    ) const;

    /**
     * @brief 检索默认层中距离中心点最近的 K 个 Actor（按距离升序排列）
     * @param Center 中心世界坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param K 期望获取的最大近邻数量
     * @param OutActors 输出检索到的最近有效 Actor 列表
     */
    void QueryKNearestActors(
        const FVector& Center,
        float MaxSearchRadius,
        int32 K,
        TArray<AActor*>& OutActors
    ) const {
        QueryKNearestActors(Center, MaxSearchRadius, K, FGameplayTag(), OutActors);
    }

    /**
     * @brief 清空指定空间层中的全部索引
     * @param LayerTag 目标空间层级
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    void ClearLayer(FGameplayTag LayerTag);

    /**
     * @brief 清空所有空间层的全部索引
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    void ClearAll();

    /**
     * @brief 清理指定空间层中已被垃圾回收或失效的弱指针
     * @param LayerTag 目标空间层级
     * @return 清理的失效 Actor 数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    int32 PruneStaleActors(FGameplayTag LayerTag);

    /**
     * @brief 一键清理所有空间层中已被垃圾回收或失效的弱指针
     * @return 清理的失效 Actor 总数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    int32 PruneAllStaleActors();

    /**
     * @brief 获取指定空间层中被索引的 Actor 数量
     * @param LayerTag 目标空间层级
     * @return 索引元素数量
     */
    UFUNCTION(BlueprintPure, Category = "GameSpatial|Subsystem")
    int32 GetActorCount(FGameplayTag LayerTag) const;

    /**
     * @brief 获取所有空间层中被索引的 Actor 总数量
     * @return 索引元素总数量
     */
    UFUNCTION(BlueprintPure, Category = "GameSpatial|Subsystem")
    int32 GetTotalActorCount() const;

    /**
     * @brief 查询 Actor 是否已注册到本子系统
     * @param Actor 目标 Actor
     * @return 已注册返回 true，否则返回 false
     */
    UFUNCTION(BlueprintPure, Category = "GameSpatial|Subsystem")
    bool IsActorRegistered(const AActor* Actor) const;

    /**
     * @brief 查询已注册 Actor 所属的空间层级标签
     * @param Actor 目标 Actor
     * @return 若已注册返回其对应的层级标签，未注册返回空标签
     */
    UFUNCTION(BlueprintPure, Category = "GameSpatial|Subsystem")
    FGameplayTag GetActorLayer(const AActor* Actor) const;

    /**
     * @brief 查询已注册 Actor 在空间子系统中最后同步的空间三维世界坐标
     * @param Actor 目标 Actor
     * @param OutLocation 输出空间坐标
     * @return 存在且已注册返回 true，否则返回 false 并输出零向量
     */
    UFUNCTION(BlueprintPure, Category = "GameSpatial|Subsystem")
    bool GetActorLocation(const AActor* Actor, FVector& OutLocation) const;

    /**
     * @brief 获取指定空间层中当前索引的所有有效存活 Actor
     * @param LayerTag 目标空间层级（为空时查询默认层）
     * @param OutActors 输出存活的 Actor 列表
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    void GetActorsInLayer(FGameplayTag LayerTag, TArray<AActor*>& OutActors) const;

private:
    [[nodiscard]] FGameplayTag ResolveLayerTag(const FGameplayTag& InTag) const;

private:
    TMap<FGameplayTag, TSpatialHashGrid3D<TWeakObjectPtr<AActor>>> LayeredGrids;
    TMap<TWeakObjectPtr<AActor>, FActorSpatialEntry> TrackedActors;
};
