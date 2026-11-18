/**
 * @file TSpatialHashGrid3D.h
 * @brief 3D 均匀空间哈希网格门面（基于底层容器 TSpatialHashContainer3D 与求交算法 FSpatialQuery3D）
 */

#pragma once

#include "CoreMinimal.h"
#include "Spatial/Algorithms/SpatialQuery3D.h"
#include "Spatial/Containers/TSpatialHashContainer3D.h"

/**
 * @brief 3D 纯逻辑均匀空间哈希网格门面模板（向后兼容全套空间立体查询、流式消费与增删改查）
 * @tparam TElement 空间对象键或句柄类型（须支持 GetTypeHash 与 operator==）
 */
template <typename TElement>
class TSpatialHashGrid3D : public TSpatialHashContainer3D<TElement> {
public:
    using Super = TSpatialHashContainer3D<TElement>;

    /**
     * @brief 构造空间哈希网格并指定单元格跨度
     * @param InCellSize 单元格边长（必须大于 1.0f）
     */
    explicit TSpatialHashGrid3D(float InCellSize = Super::DEFAULT_CELL_SIZE) noexcept
        : Super(InCellSize) {}

    TSpatialHashGrid3D(const TSpatialHashGrid3D&) = default;
    TSpatialHashGrid3D& operator=(const TSpatialHashGrid3D&) = default;
    TSpatialHashGrid3D(TSpatialHashGrid3D&&) noexcept = default;
    TSpatialHashGrid3D& operator=(TSpatialHashGrid3D&&) noexcept = default;
    ~TSpatialHashGrid3D() = default;

    /**
     * @brief 三维半径球形流式范围查询（零堆分配，单次遍历就地消费）
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型
     * @param Center 球心坐标
     * @param Radius 查询半径（须大于 0）
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TConsumer, typename TPredicate>
    void QueryRadiusMatching(const FVector& Center, float Radius, TConsumer Consumer, TPredicate Predicate) const {
        FSpatialQuery3D::QueryRadiusMatching(*this, Center, Radius, MoveTemp(Consumer), MoveTemp(Predicate));
    }

    /**
     * @brief 三维半径球形流式范围查询
     * @tparam TConsumer 消费回调类型
     * @param Center 球心坐标
     * @param Radius 查询半径
     * @param Consumer 命中元素就地消费者
     */
    template <typename TConsumer>
    void QueryRadiusMatching(const FVector& Center, float Radius, TConsumer Consumer) const {
        QueryRadiusMatching(Center, Radius, MoveTemp(Consumer), [](const TElement&) { return true; });
    }

    /**
     * @brief 三维半径球形范围查询（带自定义过滤谓词）
     * @tparam TPredicate 谓词类型
     * @param Center 球心坐标
     * @param Radius 查询半径（须大于 0）
     * @param OutResults 输出落入球体内且通过谓词检验的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TPredicate>
    void QueryRadius(const FVector& Center, float Radius, TArray<TElement>& OutResults, TPredicate Predicate) const {
        FSpatialQuery3D::QueryRadius(*this, Center, Radius, OutResults, MoveTemp(Predicate));
    }

    /**
     * @brief 三维半径球形范围查询
     * @param Center 球心坐标
     * @param Radius 查询半径（须大于 0）
     * @param OutResults 输出落入球体内的元素集合
     */
    void QueryRadius(const FVector& Center, float Radius, TArray<TElement>& OutResults) const {
        QueryRadius(Center, Radius, OutResults, [](const TElement&) { return true; });
    }

    /**
     * @brief 轴对齐三维包围盒 AABB 流式范围查询（零堆分配）
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型
     * @param Box 查询轴对齐包围盒
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TConsumer, typename TPredicate>
    void QueryAABBMatching(const FBox& Box, TConsumer Consumer, TPredicate Predicate) const {
        FSpatialQuery3D::QueryAABBMatching(*this, Box, MoveTemp(Consumer), MoveTemp(Predicate));
    }

    /**
     * @brief 轴对齐三维包围盒 AABB 流式范围查询
     * @tparam TConsumer 消费回调类型
     * @param Box 查询轴对齐包围盒
     * @param Consumer 命中元素就地消费者
     */
    template <typename TConsumer>
    void QueryAABBMatching(const FBox& Box, TConsumer Consumer) const {
        QueryAABBMatching(Box, MoveTemp(Consumer), [](const TElement&) { return true; });
    }

    /**
     * @brief 轴对齐三维包围盒 AABB 范围查询（带自定义过滤谓词）
     * @tparam TPredicate 谓词类型
     * @param Box 查询轴对齐包围盒
     * @param OutResults 输出落入包围盒内且通过谓词检验的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TPredicate>
    void QueryAABB(const FBox& Box, TArray<TElement>& OutResults, TPredicate Predicate) const {
        FSpatialQuery3D::QueryAABB(*this, Box, OutResults, MoveTemp(Predicate));
    }

    /**
     * @brief 轴对齐三维包围盒 AABB 范围查询
     * @param Box 查询轴对齐包围盒
     * @param OutResults 输出落入包围盒内的元素集合
     */
    void QueryAABB(const FBox& Box, TArray<TElement>& OutResults) const {
        QueryAABB(Box, OutResults, [](const TElement&) { return true; });
    }

    /**
     * @brief 三维直立圆柱体流式范围查询（零堆分配）
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型
     * @param Center 圆柱中心坐标
     * @param Radius 水平截面半径
     * @param HalfHeight 垂直半高
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TConsumer, typename TPredicate>
    void QueryCylinderMatching(
        const FVector& Center,
        float Radius,
        float HalfHeight,
        TConsumer Consumer,
        TPredicate Predicate
    ) const {
        FSpatialQuery3D::QueryCylinderMatching(*this, Center, Radius, HalfHeight, MoveTemp(Consumer), MoveTemp(Predicate));
    }

    /**
     * @brief 三维直立圆柱体流式范围查询
     * @tparam TConsumer 消费回调类型
     * @param Center 圆柱中心坐标
     * @param Radius 水平截面半径
     * @param HalfHeight 垂直半高
     * @param Consumer 命中元素就地消费者
     */
    template <typename TConsumer>
    void QueryCylinderMatching(
        const FVector& Center,
        float Radius,
        float HalfHeight,
        TConsumer Consumer
    ) const {
        QueryCylinderMatching(Center, Radius, HalfHeight, MoveTemp(Consumer), [](const TElement&) { return true; });
    }

    /**
     * @brief 三维直立圆柱体范围查询（带自定义过滤谓词）
     * @tparam TPredicate 谓词类型
     * @param Center 圆柱中心坐标
     * @param Radius 水平截面半径
     * @param HalfHeight 垂直半高
     * @param OutResults 输出落入圆柱体内且通过谓词检验的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TPredicate>
    void QueryCylinder(const FVector& Center, float Radius, float HalfHeight, TArray<TElement>& OutResults, TPredicate Predicate) const {
        FSpatialQuery3D::QueryCylinder(*this, Center, Radius, HalfHeight, OutResults, MoveTemp(Predicate));
    }

    /**
     * @brief 三维直立圆柱体范围查询
     * @param Center 圆柱中心坐标
     * @param Radius 水平截面半径
     * @param HalfHeight 垂直半高
     * @param OutResults 输出落入圆柱体内的元素集合
     */
    void QueryCylinder(const FVector& Center, float Radius, float HalfHeight, TArray<TElement>& OutResults) const {
        QueryCylinder(Center, Radius, HalfHeight, OutResults, [](const TElement&) { return true; });
    }

    /**
     * @brief 三维锥体流式范围查询（零堆分配）
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型
     * @param Apex 锥体顶点位置
     * @param Direction 锥体中轴朝向
     * @param Radius 锥体最大作用距离（须大于 0）
     * @param HalfAngleDegrees 锥体半顶角（度数）
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TConsumer, typename TPredicate>
    void QueryConeMatching(
        const FVector& Apex,
        const FVector& Direction,
        float Radius,
        float HalfAngleDegrees,
        TConsumer Consumer,
        TPredicate Predicate
    ) const {
        FSpatialQuery3D::QueryConeMatching(*this, Apex, Direction, Radius, HalfAngleDegrees, MoveTemp(Consumer), MoveTemp(Predicate));
    }

    /**
     * @brief 三维锥体流式范围查询
     * @tparam TConsumer 消费回调类型
     * @param Apex 锥体顶点位置
     * @param Direction 锥体中轴朝向
     * @param Radius 锥体最大作用距离
     * @param HalfAngleDegrees 锥体半顶角
     * @param Consumer 命中元素就地消费者
     */
    template <typename TConsumer>
    void QueryConeMatching(
        const FVector& Apex,
        const FVector& Direction,
        float Radius,
        float HalfAngleDegrees,
        TConsumer Consumer
    ) const {
        QueryConeMatching(Apex, Direction, Radius, HalfAngleDegrees, MoveTemp(Consumer), [](const TElement&) { return true; });
    }

    /**
     * @brief 三维锥体范围查询（带自定义过滤谓词）
     * @tparam TPredicate 谓词类型
     * @param Apex 锥体顶点位置
     * @param Direction 锥体中轴朝向
     * @param Radius 锥体最大作用距离（须大于 0）
     * @param HalfAngleDegrees 锥体半顶角（度数）
     * @param OutResults 输出落入锥体内且通过谓词检验的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TPredicate>
    void QueryCone(
        const FVector& Apex,
        const FVector& Direction,
        float Radius,
        float HalfAngleDegrees,
        TArray<TElement>& OutResults,
        TPredicate Predicate
    ) const {
        FSpatialQuery3D::QueryCone(*this, Apex, Direction, Radius, HalfAngleDegrees, OutResults, MoveTemp(Predicate));
    }

    /**
     * @brief 三维锥体范围查询
     * @param Apex 锥体顶点位置
     * @param Direction 锥体中轴朝向
     * @param Radius 锥体最大作用距离（须大于 0）
     * @param HalfAngleDegrees 锥体半顶角（度数）
     * @param OutResults 输出落入锥体内的元素集合
     */
    void QueryCone(
        const FVector& Apex,
        const FVector& Direction,
        float Radius,
        float HalfAngleDegrees,
        TArray<TElement>& OutResults
    ) const {
        QueryCone(Apex, Direction, Radius, HalfAngleDegrees, OutResults, [](const TElement&) { return true; });
    }

    /**
     * @brief 查找距离给定中心三维点最近的目标元素（单元格包围盒最短距离动态剪枝与谓词过滤）
     * @tparam TPredicate 谓词类型
     * @param Center 中心点三维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param OutNearest 输出最近元素
     * @param Predicate 过滤谓词
     * @return 存在有效元素返回 true，指定半径内无元素返回 false
     */
    template <typename TPredicate>
    bool QueryNearest(const FVector& Center, float MaxSearchRadius, TElement& OutNearest, TPredicate Predicate) const {
        return FSpatialQuery3D::QueryNearest(*this, Center, MaxSearchRadius, OutNearest, MoveTemp(Predicate));
    }

    /**
     * @brief 查找距离给定中心三维点最近的目标元素（单元格包围盒最短距离动态剪枝）
     * @param Center 中心点三维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param OutNearest 输出最近元素
     * @return 存在有效元素返回 true，指定半径内无元素返回 false
     */
    bool QueryNearest(const FVector& Center, float MaxSearchRadius, TElement& OutNearest) const {
        return QueryNearest(Center, MaxSearchRadius, OutNearest, [](const TElement&) { return true; });
    }

    /**
     * @brief 查找距离给定中心三维点最近的 K 个目标元素（动态剪枝与谓词过滤，按距离升序返回）
     * @tparam TPredicate 谓词类型
     * @param Center 中心点三维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param K 期望获取的最大近邻数量（必须大于 0）
     * @param OutNearestList 输出最近的 K 个元素列表（按距离由近至远升序排列）
     * @param Predicate 过滤谓词
     */
    template <typename TPredicate>
    void QueryKNearest(
        const FVector& Center,
        float MaxSearchRadius,
        int32 K,
        TArray<TElement>& OutNearestList,
        TPredicate Predicate
    ) const {
        FSpatialQuery3D::QueryKNearest(*this, Center, MaxSearchRadius, K, OutNearestList, MoveTemp(Predicate));
    }

    /**
     * @brief 查找距离给定中心三维点最近的 K 个目标元素（动态剪枝，按距离升序返回）
     * @param Center 中心点三维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param K 期望获取的最大近邻数量（必须大于 0）
     * @param OutNearestList 输出最近的 K 个元素列表（按距离由近至远升序排列）
     */
    void QueryKNearest(
        const FVector& Center,
        float MaxSearchRadius,
        int32 K,
        TArray<TElement>& OutNearestList
    ) const {
        QueryKNearest(Center, MaxSearchRadius, K, OutNearestList, [](const TElement&) { return true; });
    }
};
