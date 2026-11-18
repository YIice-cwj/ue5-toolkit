#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "UGameSpatialBlueprintLibrary.generated.h"

class AActor;
class UGameSpatialSubsystem;

/**
 * @brief 空间检索蓝图函数库
 *
 * 为蓝图提供基于 WorldContextObject 的静态空间检索与 Actor 管理便捷节点。
 */
UCLASS()
class GAMESPATIAL_API UGameSpatialBlueprintLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()

public:
    /**
     * @brief 获取世界空间索引子系统
     * @param WorldContextObject 拥有世界上下文的对象
     * @return 空间子系统实例指针，失败返回 nullptr
     */
    UFUNCTION(BlueprintPure, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static UGameSpatialSubsystem* GetGameSpatialSubsystem(const UObject* WorldContextObject);

    /**
     * @brief 注册 Actor 至空间索引子系统
     * @param WorldContextObject 拥有世界上下文的对象
     * @param Actor 待注册目标 Actor
     * @param LayerTag 空间层级标签（为空时归入默认层）
     * @return 注册成功返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static bool RegisterSpatialActor(const UObject* WorldContextObject, AActor* Actor, FGameplayTag LayerTag = FGameplayTag());

    /**
     * @brief 批量注册一组 Actor 至空间索引子系统
     * @param WorldContextObject 拥有世界上下文的对象
     * @param Actors 待注册目标 Actor 数组
     * @param LayerTag 空间层级标签（为空时归入默认层）
     * @return 实际成功注册的有效 Actor 数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static int32 RegisterSpatialActors(const UObject* WorldContextObject, const TArray<AActor*>& Actors, FGameplayTag LayerTag = FGameplayTag());

    /**
     * @brief 从空间索引子系统注销 Actor
     * @param WorldContextObject 拥有世界上下文的对象
     * @param Actor 待注销目标 Actor
     * @return 注销成功返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static bool UnregisterSpatialActor(const UObject* WorldContextObject, AActor* Actor);

    /**
     * @brief 批量从空间索引子系统注销一组 Actor
     * @param WorldContextObject 拥有世界上下文的对象
     * @param Actors 待注销目标 Actor 数组
     * @return 实际成功注销的 Actor 数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static int32 UnregisterSpatialActors(const UObject* WorldContextObject, const TArray<AActor*>& Actors);

    /**
     * @brief 更新 Actor 的空间位置
     * @param WorldContextObject 拥有世界上下文的对象
     * @param Actor 目标 Actor
     * @param NewLocation 新世界坐标
     * @return 更新成功返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static bool UpdateSpatialActorLocation(const UObject* WorldContextObject, AActor* Actor, const FVector& NewLocation);

    /**
     * @brief 自动读取 Actor 当前世界坐标并更新索引
     * @param WorldContextObject 拥有世界上下文的对象
     * @param Actor 目标 Actor
     * @return 更新成功返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static bool UpdateSpatialActor(const UObject* WorldContextObject, AActor* Actor);

    /**
     * @brief 三维球形范围检索 Actor
     * @param WorldContextObject 拥有世界上下文的对象
     * @param Center 球心世界坐标
     * @param Radius 查询半径
     * @param LayerTag 目标空间层级
     * @param OutActors 输出查询到的有效 Actor 列表
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static void QuerySpatialActorsInRadius(
        const UObject* WorldContextObject,
        const FVector& Center,
        float Radius,
        FGameplayTag LayerTag,
        TArray<AActor*>& OutActors
    );

    /**
     * @brief 三维锥体范围检索 Actor
     * @param WorldContextObject 拥有世界上下文的对象
     * @param Apex 锥体顶点世界坐标
     * @param Direction 锥体中轴朝向
     * @param Radius 锥体最大作用距离
     * @param HalfAngleDegrees 锥体半顶角（度数）
     * @param LayerTag 目标空间层级
     * @param OutActors 输出查询到的有效 Actor 列表
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static void QuerySpatialActorsInCone(
        const UObject* WorldContextObject,
        const FVector& Apex,
        const FVector& Direction,
        float Radius,
        float HalfAngleDegrees,
        FGameplayTag LayerTag,
        TArray<AActor*>& OutActors
    );

    /**
     * @brief 三维直立圆柱体范围检索 Actor
     * @param WorldContextObject 拥有世界上下文的对象
     * @param Center 圆柱中心世界坐标
     * @param Radius 水平截面半径
     * @param HalfHeight 垂直半高
     * @param LayerTag 目标空间层级
     * @param OutActors 输出查询到的有效 Actor 列表
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static void QuerySpatialActorsInCylinder(
        const UObject* WorldContextObject,
        const FVector& Center,
        float Radius,
        float HalfHeight,
        FGameplayTag LayerTag,
        TArray<AActor*>& OutActors
    );

    /**
     * @brief 轴对齐三维包围盒 AABB 范围检索 Actor
     * @param WorldContextObject 拥有世界上下文的对象
     * @param Box 查询轴对齐包围盒
     * @param LayerTag 目标空间层级
     * @param OutActors 输出查询到的有效 Actor 列表
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static void QuerySpatialActorsInAABB(
        const UObject* WorldContextObject,
        const FBox& Box,
        FGameplayTag LayerTag,
        TArray<AActor*>& OutActors
    );

    /**
     * @brief 检索距离中心点最近的单一 Actor
     * @param WorldContextObject 拥有世界上下文的对象
     * @param Center 中心世界坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param LayerTag 目标空间层级
     * @param OutNearestActor 输出检索到的最近有效 Actor
     * @return 存在有效 Actor 返回 true，否则返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static bool QueryNearestSpatialActor(
        const UObject* WorldContextObject,
        const FVector& Center,
        float MaxSearchRadius,
        FGameplayTag LayerTag,
        AActor*& OutNearestActor
    );

    /**
     * @brief 检索距离中心点最近的 K 个 Actor（按距离升序排列）
     * @param WorldContextObject 拥有世界上下文的对象
     * @param Center 中心世界坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param K 期望获取的最大近邻数量
     * @param LayerTag 目标空间层级
     * @param OutActors 输出检索到的最近有效 Actor 列表
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static void QueryKNearestSpatialActors(
        const UObject* WorldContextObject,
        const FVector& Center,
        float MaxSearchRadius,
        int32 K,
        FGameplayTag LayerTag,
        TArray<AActor*>& OutActors
    );

    /**
     * @brief 一键清理所有空间层中已被垃圾回收或失效的弱指针
     * @param WorldContextObject 拥有世界上下文的对象
     * @return 清理的失效 Actor 总数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static int32 PruneAllSpatialStaleActors(const UObject* WorldContextObject);

    /**
     * @brief 清空指定空间层中的全部索引
     * @param WorldContextObject 拥有世界上下文的对象
     * @param LayerTag 目标空间层级
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static void ClearSpatialLayer(const UObject* WorldContextObject, FGameplayTag LayerTag);

    /**
     * @brief 清空所有空间层的全部索引
     * @param WorldContextObject 拥有世界上下文的对象
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static void ClearAllSpatialLayers(const UObject* WorldContextObject);

    /**
     * @brief 清理指定空间层中已被垃圾回收或失效的弱指针
     * @param WorldContextObject 拥有世界上下文的对象
     * @param LayerTag 目标空间层级
     * @return 清理的失效 Actor 数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static int32 PruneSpatialStaleActors(const UObject* WorldContextObject, FGameplayTag LayerTag);

    /**
     * @brief 获取指定空间层中被索引的 Actor 数量
     * @param WorldContextObject 拥有世界上下文的对象
     * @param LayerTag 目标空间层级
     * @return 索引元素数量
     */
    UFUNCTION(BlueprintPure, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static int32 GetSpatialActorCount(const UObject* WorldContextObject, FGameplayTag LayerTag);

    /**
     * @brief 获取所有空间层中被索引的 Actor 总数量
     * @param WorldContextObject 拥有世界上下文的对象
     * @return 索引元素总数量
     */
    UFUNCTION(BlueprintPure, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static int32 GetTotalSpatialActorCount(const UObject* WorldContextObject);

    /**
     * @brief 查询 Actor 是否已注册到空间索引子系统
     * @param WorldContextObject 拥有世界上下文的对象
     * @param Actor 目标 Actor
     * @return 已注册返回 true，否则返回 false
     */
    UFUNCTION(BlueprintPure, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static bool IsSpatialActorRegistered(const UObject* WorldContextObject, const AActor* Actor);

    /**
     * @brief 查询已注册 Actor 所属的空间层级标签
     * @param WorldContextObject 拥有世界上下文的对象
     * @param Actor 目标 Actor
     * @return 若已注册返回其对应的层级标签，未注册返回空标签
     */
    UFUNCTION(BlueprintPure, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static FGameplayTag GetSpatialActorLayer(const UObject* WorldContextObject, const AActor* Actor);

    /**
     * @brief 查询已注册 Actor 在空间子系统中最后同步的空间三维世界坐标
     * @param WorldContextObject 拥有世界上下文的对象
     * @param Actor 目标 Actor
     * @param OutLocation 输出空间坐标
     * @return 存在且已注册返回 true，否则返回 false 并输出零向量
     */
    UFUNCTION(BlueprintPure, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static bool GetSpatialActorLocation(const UObject* WorldContextObject, const AActor* Actor, FVector& OutLocation);

    /**
     * @brief 获取指定空间层中当前索引的所有有效存活 Actor
     * @param WorldContextObject 拥有世界上下文的对象
     * @param LayerTag 目标空间层级
     * @param OutActors 输出存活的 Actor 列表
     */
    UFUNCTION(BlueprintCallable, Category = "GameSpatial|Library", meta = (WorldContext = "WorldContextObject"))
    static void GetSpatialActorsInLayer(const UObject* WorldContextObject, FGameplayTag LayerTag, TArray<AActor*>& OutActors);
};
