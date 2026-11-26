/**
 * @file TSpatialHashGrid2D.h
 * @brief 2D 均匀空间哈希网格门面模板（组合封装底层容器 TSpatialHashContainer2D 与查询算法 FSpatialQuery2D）
 */

#pragma once

#include "CoreMinimal.h"
#include "Grid/FSpatialQuery2D.h"
#include "Grid/TSpatialHashContainer2D.h"

/**
 * @brief 2D 纯逻辑均匀空间哈希网格门面模板（收敛内部细节，提供统一增删改查、流式零分配与几何查询契约）
 * @tparam TElement 空间对象键或句柄类型（须支持 GetTypeHash 与 operator==）
 */
template <typename TElement>
class TSpatialHashGrid2D {
public:
    using FContainerType = TSpatialHashContainer2D<TElement>;

public:
    static constexpr float DEFAULT_CELL_SIZE = FContainerType::DEFAULT_CELL_SIZE;

public:
    /**
     * @brief 构造 2D 空间哈希网格并指定单元格跨度
     * @param InCellSize 单元格边长（必须为有限正值且大于 1.0f）
     */
    explicit TSpatialHashGrid2D(float InCellSize = DEFAULT_CELL_SIZE) noexcept
        : Container(InCellSize) {
    }

    TSpatialHashGrid2D(const TSpatialHashGrid2D&) = default;
    TSpatialHashGrid2D& operator=(const TSpatialHashGrid2D&) = default;
    TSpatialHashGrid2D(TSpatialHashGrid2D&&) noexcept = default;
    TSpatialHashGrid2D& operator=(TSpatialHashGrid2D&&) noexcept = default;
    ~TSpatialHashGrid2D() = default;

    /**
     * @brief 预分配元素索引表容量以减少扩容抖动
     * @param ExpectedElementCount 预期元素数量
     */
    void Reserve(int32 ExpectedElementCount) {
        Container.Reserve(ExpectedElementCount);
    }

    /**
     * @brief 向网格插入新元素及其二维坐标
     * @param Element 目标元素标识
     * @param Location 空间二维坐标
     * @return 成功态或携带模块错误码（6002 重复 / 6003 非法坐标）的失败态
     */
    TGameStatusResult<void> Insert(const TElement& Element, const FVector2D& Location) {
        return Container.Insert(Element, Location);
    }

    /**
     * @brief O(1) 更新已有元素的二维坐标
     * @param Element 目标元素标识
     * @param NewLocation 新二维坐标
     * @return 成功态或携带模块错误码（6001 未找到 / 6003 非法坐标）的失败态
     */
    TGameStatusResult<void> Update(const TElement& Element, const FVector2D& NewLocation) {
        return Container.Update(Element, NewLocation);
    }

    /**
     * @brief O(1) 从网格移除目标元素
     * @param Element 待移除的元素标识
     * @return 成功态或携带 6001 未找到错误码的失败态
     */
    TGameStatusResult<void> Remove(const TElement& Element) {
        return Container.Remove(Element);
    }

    /**
     * @brief 清空网格内全部元素与桶
     */
    void Clear() {
        Container.Clear();
    }

    /**
     * @brief 检查网格中是否包含目标元素
     * @param Element 目标元素标识
     * @return 包含返回 true
     */
    [[nodiscard]] bool Contains(const TElement& Element) const noexcept {
        return Container.Contains(Element);
    }

    /**
     * @brief 零开销只读探查目标元素的二维坐标
     * @param Element 目标元素标识
     * @return 存在返回坐标指针，否则返回 nullptr
     */
    [[nodiscard]] const FVector2D* FindLocation(const TElement& Element) const noexcept {
        return Container.FindLocation(Element);
    }

    /**
     * @brief 查询网格中当前容纳的元素总数
     * @return 元素总数
     */
    [[nodiscard]] int32 Num() const noexcept {
        return Container.Num();
    }

    /**
     * @brief 查询当前活跃单元格桶数量
     * @return 活跃桶数量
     */
    [[nodiscard]] int32 GetBucketCount() const noexcept {
        return Container.GetBucketCount();
    }

    /**
     * @brief 查询当前网格单元格边长
     * @return 单元格边长
     */
    [[nodiscard]] float GetCellSize() const noexcept {
        return Container.GetCellSize();
    }

    /**
     * @brief 获取网格内全部已注册元素的快照列表
     * @param OutElements 输出元素列表
     */
    void GetAllElements(TArray<TElement>& OutElements) const {
        Container.GetAllElements(OutElements);
    }

    /**
     * @brief 零堆分配流式遍历网格内全部已注册元素
     * @tparam TFunc 消费回调类型 (const TElement&) 或 (const TElement&, const FVector2D&)
     * @param Func 遍历消费者
     */
    template <typename TFunc>
    void ForEachElement(TFunc&& Func) const {
        Container.ForEachElement(Forward<TFunc>(Func));
    }

    /**
     * @brief 半径圆形流式范围查询（零堆分配，单次遍历就地消费）
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Center 圆心坐标
     * @param Radius 查询半径
     * @param Consumer 命中元素就地消费者
     * @param Predicate 可选过滤谓词
     */
    template <typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    void QueryRadiusMatching(
        const FVector2D& Center,
        float Radius,
        TConsumer&& Consumer,
        TPredicate&& Predicate = {}
    ) const {
        FSpatialQuery2D::QueryRadiusMatching(Container, Center, Radius, Forward<TConsumer>(Consumer), Forward<TPredicate>(Predicate));
    }

    /**
     * @brief 半径圆形范围查询
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Center 圆心坐标
     * @param Radius 查询半径
     * @param OutResults 输出落入圆形内的元素集合
     * @param Predicate 可选过滤谓词
     */
    template <typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    void QueryRadius(
        const FVector2D& Center,
        float Radius,
        TArray<TElement>& OutResults,
        TPredicate&& Predicate = {}
    ) const {
        FSpatialQuery2D::QueryRadius(Container, Center, Radius, OutResults, Forward<TPredicate>(Predicate));
    }

    /**
     * @brief 轴对齐矩形 AABB 流式范围查询（零堆分配）
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Box 查询矩形包围盒
     * @param Consumer 命中元素就地消费者
     * @param Predicate 可选过滤谓词
     */
    template <typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    void QueryAABBMatching(
        const FBox2D& Box,
        TConsumer&& Consumer,
        TPredicate&& Predicate = {}
    ) const {
        FSpatialQuery2D::QueryAABBMatching(Container, Box, Forward<TConsumer>(Consumer), Forward<TPredicate>(Predicate));
    }

    /**
     * @brief 轴对齐矩形 AABB 范围查询
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Box 查询矩形包围盒
     * @param OutResults 输出落入矩形内的元素集合
     * @param Predicate 可选过滤谓词
     */
    template <typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    void QueryAABB(
        const FBox2D& Box,
        TArray<TElement>& OutResults,
        TPredicate&& Predicate = {}
    ) const {
        FSpatialQuery2D::QueryAABB(Container, Box, OutResults, Forward<TPredicate>(Predicate));
    }

    /**
     * @brief 2D 扇形流式范围查询（零堆分配）
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Center 扇形圆心
     * @param ForwardDir 扇形基准朝向
     * @param Radius 扇形半径
     * @param HalfAngleDegrees 扇形半角（度数）
     * @param Consumer 命中元素就地消费者
     * @param Predicate 可选过滤谓词
     */
    template <typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    void QuerySectorMatching(
        const FVector2D& Center,
        const FVector2D& ForwardDir,
        float Radius,
        float HalfAngleDegrees,
        TConsumer&& Consumer,
        TPredicate&& Predicate = {}
    ) const {
        FSpatialQuery2D::QuerySectorMatching(
            Container,
            Center,
            ForwardDir,
            Radius,
            HalfAngleDegrees,
            Forward<TConsumer>(Consumer),
            Forward<TPredicate>(Predicate)
        );
    }

    /**
     * @brief 2D 扇形范围查询
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Center 扇形圆心
     * @param ForwardDir 扇形基准朝向
     * @param Radius 扇形半径
     * @param HalfAngleDegrees 扇形半角（度数）
     * @param OutResults 输出落入扇形内的元素集合
     * @param Predicate 可选过滤谓词
     */
    template <typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    void QuerySector(
        const FVector2D& Center,
        const FVector2D& ForwardDir,
        float Radius,
        float HalfAngleDegrees,
        TArray<TElement>& OutResults,
        TPredicate&& Predicate = {}
    ) const {
        FSpatialQuery2D::QuerySector(Container, Center, ForwardDir, Radius, HalfAngleDegrees, OutResults, Forward<TPredicate>(Predicate));
    }

    /**
     * @brief 查找距离给定二维中心点最近的目标元素
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Center 中心点二维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param OutNearest 输出最近元素
     * @param Predicate 可选过滤谓词
     * @return 存在有效元素返回 true，指定半径内无元素返回 false
     */
    template <typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    bool QueryNearest(
        const FVector2D& Center,
        float MaxSearchRadius,
        TElement& OutNearest,
        TPredicate&& Predicate = {}
    ) const {
        return FSpatialQuery2D::QueryNearest(Container, Center, MaxSearchRadius, OutNearest, Forward<TPredicate>(Predicate));
    }

    /**
     * @brief 流式查找距离给定二维中心点最近的 K 个目标元素（K <= 16 零堆分配，按距离升序消费）
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Center 中心点二维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param K 期望获取的最大近邻数量
     * @param Consumer 按距离升序接收命中元素的消费者
     * @param Predicate 可选过滤谓词
     */
    template <typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    void QueryKNearestMatching(
        const FVector2D& Center,
        float MaxSearchRadius,
        int32 K,
        TConsumer&& Consumer,
        TPredicate&& Predicate = {}
    ) const {
        FSpatialQuery2D::QueryKNearestMatching(
            Container,
            Center,
            MaxSearchRadius,
            K,
            Forward<TConsumer>(Consumer),
            Forward<TPredicate>(Predicate)
        );
    }

    /**
     * @brief 查找距离给定二维中心点最近的 K 个目标元素（按距离升序返回）
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Center 中心点二维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param K 期望获取的最大近邻数量
     * @param OutNearestList 输出最近的 K 个元素列表
     * @param Predicate 可选过滤谓词
     */
    template <typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    void QueryKNearest(
        const FVector2D& Center,
        float MaxSearchRadius,
        int32 K,
        TArray<TElement>& OutNearestList,
        TPredicate&& Predicate = {}
    ) const {
        FSpatialQuery2D::QueryKNearest(Container, Center, MaxSearchRadius, K, OutNearestList, Forward<TPredicate>(Predicate));
    }

private:
    FContainerType Container;
};
