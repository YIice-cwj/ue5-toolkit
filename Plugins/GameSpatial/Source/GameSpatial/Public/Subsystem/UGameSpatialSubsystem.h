/**
 * @file UGameSpatialSubsystem.h
 * @brief 世界级无物理引擎依赖 Actor 分层空间索引子系统声明
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "Core/SpatialQueryCommon.h"
#include "Grid/TSpatialHashGrid3D.h"
#include "UGameSpatialSubsystem.generated.h"

/**
 * @brief 世界级空间索引子系统
 *
 * 基于无物理引擎依赖的三维空间哈希网格，提供轻量高效的 Actor 分层空间索引、
 * 蓝图/C++ 统一几何查询接口与 C++ 零堆分配流式查询原语。
 * 内部统一采用弱引用持有 Actor，绝对避免阻碍垃圾回收或产生悬垂指针。
 * 非线程安全，所有方法必须在 GameThread 调用。
 */
UCLASS()
class GAMESPATIAL_API UGameSpatialSubsystem : public UWorldSubsystem {
    GENERATED_BODY()

public:
    /**
     * @brief 已追踪 Actor 的图层归属与最后同步坐标快照
     */
    struct FActorSpatialEntry {
        FGameplayTag LayerTag;
        FVector LastLocation = FVector::ZeroVector;
    };

public:
    /**
     * @brief 析构函数
     */
    ~UGameSpatialSubsystem() override = default;

    /**
     * @brief 子系统初始化生命周期
     * @param Collection 子系统集合上下文
     */
    void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * @brief 子系统反初始化生命周期（清空全部图层与追踪索引）
     */
    void Deinitialize() override;

    /**
     * @brief 注册 Actor 到指定空间层（已存在同层时原地更新位置，跨层时自动迁移图层）
     * @param Actor 待注册的目标 Actor
     * @param LayerTag 空间所属层级标签（为空时归入 Spatial.Layer.Default）
     * @return 注册成功返回 true；Actor 无效、正在销毁或坐标非有限返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    bool RegisterActor(AActor* Actor, FGameplayTag LayerTag = FGameplayTag());

    /**
     * @brief 批量注册一组 Actor 到指定空间层
     * @param Actors 待注册目标 Actor 数组
     * @param LayerTag 空间所属层级标签（为空时归入 Spatial.Layer.Default）
     * @return 实际成功注册的有效 Actor 数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    int32 RegisterActors(const TArray<AActor*>& Actors, FGameplayTag LayerTag = FGameplayTag());

    /**
     * @brief 从空间层注销 Actor（允许在 Actor::EndPlay / 销毁流程中调用）
     * @param Actor 待注销的目标 Actor
     * @return 注销成功返回 true，未注册或 Actor 为空返回 false
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
     * @brief 手动更新 Actor 的空间索引坐标
     * @param Actor 目标 Actor
     * @param NewLocation 最新的三维世界坐标
     * @return 更新成功返回 true；未注册、正在销毁或坐标非有限返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    bool UpdateActorLocation(AActor* Actor, const FVector& NewLocation);

    /**
     * @brief 自动读取 Actor 当前世界坐标并同步更新索引
     * @param Actor 目标 Actor
     * @return 更新成功返回 true，未注册或 Actor 无效/正在销毁返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    bool UpdateActor(AActor* Actor);

    /**
     * @brief 三维球形范围检索存活 Actor
     * @param Center 球心世界坐标
     * @param Radius 查询半径
     * @param OutActors 输出查询到的有效存活 Actor 列表
     * @param LayerTag 目标空间层级（为空时查询 Spatial.Layer.Default）
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    void QueryActorsInRadius(
        const FVector& Center,
        float Radius,
        TArray<AActor*>& OutActors,
        FGameplayTag LayerTag = FGameplayTag()
    ) const;

    /**
     * @brief 三维锥体范围检索存活 Actor（常用于视锥范围与前向攻击判定）
     * @param Apex 锥体顶点世界坐标
     * @param Direction 锥体对称中轴朝向
     * @param Radius 锥体最大作用距离
     * @param HalfAngleDegrees 锥体半顶角（度数）
     * @param OutActors 输出查询到的有效存活 Actor 列表
     * @param LayerTag 目标空间层级（为空时查询 Spatial.Layer.Default）
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    void QueryActorsInCone(
        const FVector& Apex,
        const FVector& Direction,
        float Radius,
        float HalfAngleDegrees,
        TArray<AActor*>& OutActors,
        FGameplayTag LayerTag = FGameplayTag()
    ) const;

    /**
     * @brief 三维直立圆柱体范围检索存活 Actor（常用于地面水平技能范围判定）
     * @param Center 圆柱中心世界坐标
     * @param Radius 水平截面半径
     * @param HalfHeight 垂直半高
     * @param OutActors 输出查询到的有效存活 Actor 列表
     * @param LayerTag 目标空间层级（为空时查询 Spatial.Layer.Default）
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    void QueryActorsInCylinder(
        const FVector& Center,
        float Radius,
        float HalfHeight,
        TArray<AActor*>& OutActors,
        FGameplayTag LayerTag = FGameplayTag()
    ) const;

    /**
     * @brief 轴对齐三维包围盒 AABB 范围检索存活 Actor
     * @param Box 查询轴对齐包围盒
     * @param OutActors 输出查询到的有效存活 Actor 列表
     * @param LayerTag 目标空间层级（为空时查询 Spatial.Layer.Default）
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    void QueryActorsInAABB(
        const FBox& Box,
        TArray<AActor*>& OutActors,
        FGameplayTag LayerTag = FGameplayTag()
    ) const;

    /**
     * @brief 检索距离中心点最近的单一存活 Actor
     * @param Center 中心世界坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param OutNearestActor 输出检索到的最近有效存活 Actor
     * @param LayerTag 目标空间层级（为空时查询 Spatial.Layer.Default）
     * @return 存在有效 Actor 返回 true，否则返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    bool QueryNearestActor(
        const FVector& Center,
        float MaxSearchRadius,
        AActor*& OutNearestActor,
        FGameplayTag LayerTag = FGameplayTag()
    ) const;

    /**
     * @brief 检索距离中心点最近的 K 个存活 Actor（按距离由近至远升序排列，K <= 16 零中间堆分配）
     * @param Center 中心世界坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param K 期望获取的最大近邻数量
     * @param OutActors 输出检索到的最近有效 Actor 列表
     * @param LayerTag 目标空间层级（为空时查询 Spatial.Layer.Default）
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    void QueryKNearestActors(
        const FVector& Center,
        float MaxSearchRadius,
        int32 K,
        TArray<AActor*>& OutActors,
        FGameplayTag LayerTag = FGameplayTag()
    ) const;

    /**
     * @brief C++ 零堆分配流式球形范围遍历存活 Actor（自动过滤已销毁或销毁中弱引用）
     * @tparam TConsumer 消费回调类型 (AActor*) 或 (AActor*, const FVector&)
     * @param Center 球心世界坐标
     * @param Radius 查询半径
     * @param Consumer 就地消费者回调
     * @param LayerTag 目标空间层级（为空时查询默认层）
     */
    template <typename TConsumer>
    void ForEachActorInRadius(
        const FVector& Center,
        float Radius,
        TConsumer&& Consumer,
        FGameplayTag LayerTag = FGameplayTag()
    ) const {
        const TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolveLayerTag(LayerTag));
        if (Grid == nullptr) {
            return;
        }
        Grid->QueryRadiusMatching(Center, Radius, [&Consumer](const TWeakObjectPtr<AActor>& WeakActor, const FVector& Loc) {
            if (AActor* Actor = ResolveOperationalActor(WeakActor)) {
                Game::Spatial::Private::InvokeConsumer(Consumer, Actor, Loc);
            }
        });
    }

    /**
     * @brief C++ 零堆分配流式锥体范围遍历存活 Actor（自动过滤已销毁或销毁中弱引用）
     * @tparam TConsumer 消费回调类型 (AActor*) 或 (AActor*, const FVector&)
     * @param Apex 锥体顶点世界坐标
     * @param Direction 锥体对称中轴朝向
     * @param Radius 锥体最大作用距离
     * @param HalfAngleDegrees 锥体半顶角（度数）
     * @param Consumer 就地消费者回调
     * @param LayerTag 目标空间层级（为空时查询默认层）
     */
    template <typename TConsumer>
    void ForEachActorInCone(
        const FVector& Apex,
        const FVector& Direction,
        float Radius,
        float HalfAngleDegrees,
        TConsumer&& Consumer,
        FGameplayTag LayerTag = FGameplayTag()
    ) const {
        const TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolveLayerTag(LayerTag));
        if (Grid == nullptr) {
            return;
        }
        Grid->QueryConeMatching(Apex, Direction, Radius, HalfAngleDegrees, [&Consumer](const TWeakObjectPtr<AActor>& WeakActor, const FVector& Loc) {
            if (AActor* Actor = ResolveOperationalActor(WeakActor)) {
                Game::Spatial::Private::InvokeConsumer(Consumer, Actor, Loc);
            }
        });
    }

    /**
     * @brief C++ 零堆分配流式直立圆柱体范围遍历存活 Actor（自动过滤已销毁或销毁中弱引用）
     * @tparam TConsumer 消费回调类型 (AActor*) 或 (AActor*, const FVector&)
     * @param Center 圆柱中心世界坐标
     * @param Radius 水平截面半径
     * @param HalfHeight 垂直半高
     * @param Consumer 就地消费者回调
     * @param LayerTag 目标空间层级（为空时查询默认层）
     */
    template <typename TConsumer>
    void ForEachActorInCylinder(
        const FVector& Center,
        float Radius,
        float HalfHeight,
        TConsumer&& Consumer,
        FGameplayTag LayerTag = FGameplayTag()
    ) const {
        const TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolveLayerTag(LayerTag));
        if (Grid == nullptr) {
            return;
        }
        Grid->QueryCylinderMatching(Center, Radius, HalfHeight, [&Consumer](const TWeakObjectPtr<AActor>& WeakActor, const FVector& Loc) {
            if (AActor* Actor = ResolveOperationalActor(WeakActor)) {
                Game::Spatial::Private::InvokeConsumer(Consumer, Actor, Loc);
            }
        });
    }

    /**
     * @brief C++ 零堆分配流式 AABB 包围盒遍历存活 Actor（自动过滤已销毁或销毁中弱引用）
     * @tparam TConsumer 消费回调类型 (AActor*) 或 (AActor*, const FVector&)
     * @param Box 查询轴对齐包围盒
     * @param Consumer 就地消费者回调
     * @param LayerTag 目标空间层级（为空时查询默认层）
     */
    template <typename TConsumer>
    void ForEachActorInAABB(
        const FBox& Box,
        TConsumer&& Consumer,
        FGameplayTag LayerTag = FGameplayTag()
    ) const {
        const TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolveLayerTag(LayerTag));
        if (Grid == nullptr) {
            return;
        }
        Grid->QueryAABBMatching(Box, [&Consumer](const TWeakObjectPtr<AActor>& WeakActor, const FVector& Loc) {
            if (AActor* Actor = ResolveOperationalActor(WeakActor)) {
                Game::Spatial::Private::InvokeConsumer(Consumer, Actor, Loc);
            }
        });
    }

    /**
     * @brief O(LayerSize) 清空指定空间层中的全部索引
     * @param LayerTag 目标空间层级（为空时清空默认层）
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    void ClearLayer(FGameplayTag LayerTag = FGameplayTag());

    /**
     * @brief 清空所有空间层的全部索引
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    void ClearAll();

    /**
     * @brief O(LayerSize) 清理指定空间层中已被垃圾回收或销毁的弱引用条目
     * @param LayerTag 目标空间层级（为空时清理默认层）
     * @return 清理的失效 Actor 数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    int32 PruneStaleActors(FGameplayTag LayerTag = FGameplayTag());

    /**
     * @brief 一键清理所有空间层中已被垃圾回收或销毁的弱引用条目
     * @return 清理的失效 Actor 总数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    int32 PruneAllStaleActors();

    /**
     * @brief 获取指定空间层中当前索引的条目数量（含待惰性清理的已销毁弱引用）
     * @param LayerTag 目标空间层级（为空时查询默认层）
     * @return 索引元素数量
     */
    UFUNCTION(BlueprintPure, Category = "GameSpatial|Subsystem")
    int32 GetActorCount(FGameplayTag LayerTag = FGameplayTag()) const;

    /**
     * @brief 获取所有空间层中当前索引的条目总数量（含待惰性清理的已销毁弱引用）
     * @return 索引元素总数量
     */
    UFUNCTION(BlueprintPure, Category = "GameSpatial|Subsystem")
    int32 GetTotalActorCount() const;

    /**
     * @brief 查询有效存活 Actor 是否已注册到本子系统（对空指针或已销毁 Actor 返回 false）
     * @param Actor 目标 Actor
     * @return 存活且已注册返回 true，否则返回 false
     */
    UFUNCTION(BlueprintPure, Category = "GameSpatial|Subsystem")
    bool IsActorRegistered(const AActor* Actor) const;

    /**
     * @brief 查询已注册存活 Actor 所属的空间层级标签（对空指针或已销毁 Actor 返回空标签）
     * @param Actor 目标 Actor
     * @return 若存活且已注册返回其对应的层级标签，否则返回空标签
     */
    UFUNCTION(BlueprintPure, Category = "GameSpatial|Subsystem")
    FGameplayTag GetActorLayer(const AActor* Actor) const;

    /**
     * @brief 查询已注册存活 Actor 在空间子系统中最后同步的三维世界坐标
     * @param Actor 目标 Actor
     * @param OutLocation 输出空间坐标
     * @return 存活且已注册返回 true，否则返回 false 并输出零向量
     */
    UFUNCTION(BlueprintPure, Category = "GameSpatial|Subsystem")
    bool GetActorLocation(const AActor* Actor, FVector& OutLocation) const;

    /**
     * @brief 获取指定空间层中当前索引的所有有效存活 Actor（零中间数组堆分配）
     * @param OutActors 输出存活的 Actor 列表
     * @param LayerTag 目标空间层级（为空时查询默认层）
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Subsystem")
    void GetActorsInLayer(TArray<AActor*>& OutActors, FGameplayTag LayerTag = FGameplayTag()) const;

private:
    [[nodiscard]] FGameplayTag ResolveLayerTag(const FGameplayTag& InTag) const noexcept;

public:
    /**
     * @brief 获取默认空间图层原生标签（Spatial.Layer.Default）
     * @return 默认空间图层标签
     */
    [[nodiscard]] static FGameplayTag GetDefaultLayerTag() noexcept;

private:
    [[nodiscard]] static bool IsActorOperational(const AActor* Actor) noexcept;
    [[nodiscard]] static FORCEINLINE AActor* ResolveOperationalActor(const TWeakObjectPtr<AActor>& WeakActor) noexcept {
        AActor* Actor = WeakActor.Get();
        return (Actor != nullptr && !Actor->IsActorBeingDestroyed()) ? Actor : nullptr;
    }

private:
    TMap<FGameplayTag, TSpatialHashGrid3D<TWeakObjectPtr<AActor>>> LayeredGrids;
    TMap<TWeakObjectPtr<AActor>, FActorSpatialEntry> TrackedActors;
};
