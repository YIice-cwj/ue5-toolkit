/**
 * @file TSpatialHashGrid2D.h
 * @brief 2D 均匀空间哈希网格门面（基于底层容器 TSpatialHashContainer2D 与求交算法 FSpatialQuery2D）
 */

#pragma once

#include "CoreMinimal.h"
#include "Spatial/Algorithms/SpatialQuery2D.h"
#include "Spatial/Containers/TSpatialHashContainer2D.h"

/**
 * @brief 2D 纯逻辑均匀空间哈希网格门面模板（向后兼容全套几何查询、流式消费与增删改查）
 * @tparam TElement 空间对象键或句柄类型（须支持 GetTypeHash 与 operator==）
 */
template <typename TElement>
class TSpatialHashGrid2D : public TSpatialHashContainer2D<TElement> {
public:
    using Super = TSpatialHashContainer2D<TElement>;

    /**
     * @brief 构造空间哈希网格并指定单元格跨度
     * @param InCellSize 单元格边长（必须大于 1.0f）
     */
    explicit TSpatialHashGrid2D(float InCellSize = Super::DEFAULT_CELL_SIZE) noexcept
        : Super(InCellSize) {}

    TSpatialHashGrid2D(const TSpatialHashGrid2D&) = default;
    TSpatialHashGrid2D& operator=(const TSpatialHashGrid2D&) = default;
    TSpatialHashGrid2D(TSpatialHashGrid2D&&) noexcept = default;
    TSpatialHashGrid2D& operator=(TSpatialHashGrid2D&&) noexcept = default;
    ~TSpatialHashGrid2D() = default;

    /**
     * @brief 半径圆形流式范围查询（零堆分配，单次遍历就地消费）
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型
     * @param Center 圆心坐标
     * @param Radius 查询半径（须大于 0）
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TConsumer, typename TPredicate>
    void QueryRadiusMatching(const FVector2D& Center, float Radius, TConsumer Consumer, TPredicate Predicate) const {
        FSpatialQuery2D::QueryRadiusMatching(*this, Center, Radius, MoveTemp(Consumer), MoveTemp(Predicate));
    }

    /**
     * @brief 半径圆形流式范围查询（无过滤谓词）
     * @tparam TConsumer 消费回调类型
     * @param Center 圆心坐标
     * @param Radius 查询半径
     * @param Consumer 命中元素就地消费者
     */
    template <typename TConsumer>
    void QueryRadiusMatching(const FVector2D& Center, float Radius, TConsumer Consumer) const {
        QueryRadiusMatching(Center, Radius, MoveTemp(Consumer), [](const TElement&) { return true; });
    }

    /**
     * @brief 半径圆形范围查询（带自定义过滤谓词）
     * @tparam TPredicate 谓词类型
     * @param Center 圆心坐标
     * @param Radius 查询半径（须大于 0）
     * @param OutResults 输出落入圆形内且通过谓词检验的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TPredicate>
    void QueryRadius(const FVector2D& Center, float Radius, TArray<TElement>& OutResults, TPredicate Predicate) const {
        FSpatialQuery2D::QueryRadius(*this, Center, Radius, OutResults, MoveTemp(Predicate));
    }

    /**
     * @brief 半径圆形范围查询
     * @param Center 圆心坐标
     * @param Radius 查询半径（须大于 0）
     * @param OutResults 输出落入圆形内的元素集合
     */
    void QueryRadius(const FVector2D& Center, float Radius, TArray<TElement>& OutResults) const {
        QueryRadius(Center, Radius, OutResults, [](const TElement&) { return true; });
    }

    /**
     * @brief 轴对齐矩形 AABB 流式范围查询（零堆分配）
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型
     * @param Box 查询矩形包围盒
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TConsumer, typename TPredicate>
    void QueryAABBMatching(const FBox2D& Box, TConsumer Consumer, TPredicate Predicate) const {
        FSpatialQuery2D::QueryAABBMatching(*this, Box, MoveTemp(Consumer), MoveTemp(Predicate));
    }

    /**
     * @brief 轴对齐矩形 AABB 流式范围查询
     * @tparam TConsumer 消费回调类型
     * @param Box 查询矩形包围盒
     * @param Consumer 命中元素就地消费者
     */
    template <typename TConsumer>
    void QueryAABBMatching(const FBox2D& Box, TConsumer Consumer) const {
        QueryAABBMatching(Box, MoveTemp(Consumer), [](const TElement&) { return true; });
    }

    /**
     * @brief 轴对齐矩形 AABB 范围查询（带自定义过滤谓词）
     * @tparam TPredicate 谓词类型
     * @param Box 查询矩形包围盒
     * @param OutResults 输出落入矩形内且通过谓词检验的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TPredicate>
    void QueryAABB(const FBox2D& Box, TArray<TElement>& OutResults, TPredicate Predicate) const {
        FSpatialQuery2D::QueryAABB(*this, Box, OutResults, MoveTemp(Predicate));
    }

    /**
     * @brief 轴对齐矩形 AABB 范围查询
     * @param Box 查询矩形包围盒
     * @param OutResults 输出落入矩形内的元素集合
     */
    void QueryAABB(const FBox2D& Box, TArray<TElement>& OutResults) const {
        QueryAABB(Box, OutResults, [](const TElement&) { return true; });
    }

    /**
     * @brief 2D 扇形流式范围查询（零堆分配）
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型
     * @param Center 扇形圆心
     * @param Forward 扇形基准朝向
     * @param Radius 扇形半径（须大于 0）
     * @param HalfAngleDegrees 扇形半角（度数）
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TConsumer, typename TPredicate>
    void QuerySectorMatching(
        const FVector2D& Center,
        const FVector2D& Forward,
        float Radius,
        float HalfAngleDegrees,
        TConsumer Consumer,
        TPredicate Predicate
    ) const {
        FSpatialQuery2D::QuerySectorMatching(*this, Center, Forward, Radius, HalfAngleDegrees, MoveTemp(Consumer), MoveTemp(Predicate));
    }

    /**
     * @brief 2D 扇形流式范围查询
     * @tparam TConsumer 消费回调类型
     * @param Center 扇形圆心
     * @param Forward 扇形基准朝向
     * @param Radius 扇形半径
     * @param HalfAngleDegrees 扇形半角
     * @param Consumer 命中元素就地消费者
     */
    template <typename TConsumer>
    void QuerySectorMatching(
        const FVector2D& Center,
        const FVector2D& Forward,
        float Radius,
        float HalfAngleDegrees,
        TConsumer Consumer
    ) const {
        QuerySectorMatching(Center, Forward, Radius, HalfAngleDegrees, MoveTemp(Consumer), [](const TElement&) { return true; });
    }

    /**
     * @brief 2D 扇形范围查询（带自定义过滤谓词）
     * @tparam TPredicate 谓词类型
     * @param Center 扇形圆心
     * @param Forward 扇形基准朝向
     * @param Radius 扇形半径（须大于 0）
     * @param HalfAngleDegrees 扇形半角（度数）
     * @param OutResults 输出落入扇形内且通过谓词检验的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TPredicate>
    void QuerySector(
        const FVector2D& Center,
        const FVector2D& Forward,
        float Radius,
        float HalfAngleDegrees,
        TArray<TElement>& OutResults,
        TPredicate Predicate
    ) const {
        FSpatialQuery2D::QuerySector(*this, Center, Forward, Radius, HalfAngleDegrees, OutResults, MoveTemp(Predicate));
    }

    /**
     * @brief 2D 扇形范围查询
     * @param Center 扇形圆心
     * @param Forward 扇形基准朝向
     * @param Radius 扇形半径（须大于 0）
     * @param HalfAngleDegrees 扇形半角（度数）
     * @param OutResults 输出落入扇形内的元素集合
     */
    void QuerySector(
        const FVector2D& Center,
        const FVector2D& Forward,
        float Radius,
        float HalfAngleDegrees,
        TArray<TElement>& OutResults
    ) const {
        QuerySector(Center, Forward, Radius, HalfAngleDegrees, OutResults, [](const TElement&) { return true; });
    }

    /**
     * @brief 查找距离给定中心点最近的目标元素（单元格包围盒最短距离动态剪枝与谓词过滤）
     * @tparam TPredicate 谓词类型
     * @param Center 中心点二维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param OutNearest 输出最近元素
     * @param Predicate 过滤谓词
     * @return 存在有效元素返回 true，指定半径内无元素返回 false
     */
    template <typename TPredicate>
    bool QueryNearest(const FVector2D& Center, float MaxSearchRadius, TElement& OutNearest, TPredicate Predicate) const {
        return FSpatialQuery2D::QueryNearest(*this, Center, MaxSearchRadius, OutNearest, MoveTemp(Predicate));
    }

    /**
     * @brief 查找距离给定中心点最近的目标元素（单元格包围盒最短距离动态剪枝）
     * @param Center 中心点二维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param OutNearest 输出最近元素
     * @return 存在有效元素返回 true，指定半径内无元素返回 false
     */
    bool QueryNearest(const FVector2D& Center, float MaxSearchRadius, TElement& OutNearest) const {
        return QueryNearest(Center, MaxSearchRadius, OutNearest, [](const TElement&) { return true; });
    }

    /**
     * @brief 查找距离给定中心点最近的 K 个目标元素（动态剪枝与谓词过滤，按距离升序返回）
     * @tparam TPredicate 谓词类型
     * @param Center 中心点二维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param K 期望获取的最大近邻数量（必须大于 0）
     * @param OutNearestList 输出最近的 K 个元素列表（按距离由近至远升序排列）
     * @param Predicate 过滤谓词
     */
    template <typename TPredicate>
    void QueryKNearest(
        const FVector2D& Center,
        float MaxSearchRadius,
        int32 K,
        TArray<TElement>& OutNearestList,
        TPredicate Predicate
    ) const {
        FSpatialQuery2D::QueryKNearest(*this, Center, MaxSearchRadius, K, OutNearestList, MoveTemp(Predicate));
    }

    /**
     * @brief 查找距离给定中心点最近的 K 个目标元素（动态剪枝，按距离升序返回）
     * @param Center 中心点二维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param K 期望获取的最大近邻数量（必须大于 0）
     * @param OutNearestList 输出最近的 K 个元素列表（按距离由近至远升序排列）
     */
    void QueryKNearest(
        const FVector2D& Center,
        float MaxSearchRadius,
        int32 K,
        TArray<TElement>& OutNearestList
    ) const {
        QueryKNearest(Center, MaxSearchRadius, K, OutNearestList, [](const TElement&) { return true; });
    }
};
