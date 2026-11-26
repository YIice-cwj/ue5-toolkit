/**
 * @file FSpatialQuery2D.h
 * @brief 2D 空间几何范围求交与近邻启发式搜索算法（全 LWC 双精度、无开方扇形判定、稀疏快路径与零分配流式原语）
 */

#pragma once

#include "CoreMinimal.h"
#include "Math/Box2D.h"
#include "Math/Vector2D.h"
#include "Core/SpatialQueryCommon.h"
#include "Grid/TSpatialHashContainer2D.h"

/**
 * @brief 2D 空间几何查询纯静态算法工具类
 */
struct FSpatialQuery2D {
public:
    /**
     * @brief 半径圆形流式范围查询（零堆分配，单次遍历就地消费）
     * @tparam TElement 元素类型
     * @tparam TConsumer 消费回调类型 (const TElement&) 或 (const TElement&, const FVector2D&)
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标空间容器
     * @param Center 圆心坐标
     * @param Radius 查询半径（须为有限正数）
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QueryRadiusMatching(
        const TSpatialHashContainer2D<TElement>& Container,
        const FVector2D& Center,
        float Radius,
        TConsumer&& Consumer,
        TPredicate&& Predicate = {}
    ) {
        if (Container.Num() == 0
            || !Game::Spatial::Private::IsFiniteVector(Center)
            || !Game::Spatial::Private::IsFiniteCoord(Radius)
            || Radius <= 0.0f) {
            return;
        }

        const double RadiusD = static_cast<double>(Radius);
        double RadiusSq = RadiusD * RadiusD;

        const int32 MinX = Container.ComputeCellCoord(Center.X - RadiusD);
        const int32 MaxX = Container.ComputeCellCoord(Center.X + RadiusD);
        const int32 MinY = Container.ComputeCellCoord(Center.Y - RadiusD);
        const int32 MaxY = Container.ComputeCellCoord(Center.Y + RadiusD);

        auto CheckAndConsume = [&Center, RadiusSq, &Consumer, &Predicate](const TElement& Item, const FVector2D& Loc) {
            if (FVector2D::DistSquared(Center, Loc) <= RadiusSq) {
                Game::Spatial::Private::TryConsumeElement(Consumer, Predicate, Item, Loc);
            }
        };

        if (Game::Spatial::Private::ShouldUseSparseScan2D(MinX, MaxX, MinY, MaxY, Container.Num())) {
            Container.ForEachElement(CheckAndConsume);
            return;
        }

        ForEachCellInRadiusBounds(
            Container,
            Center,
            MinX,
            MaxX,
            MinY,
            MaxY,
            RadiusSq,
            0,
            0,
            false,
            [&Container, &CheckAndConsume](uint64 CellKey) {
                Container.ForEachElementInCell(CellKey, CheckAndConsume);
            }
        );
    }

    /**
     * @brief 半径圆形范围查询
     * @tparam TElement 元素类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标空间容器
     * @param Center 圆心坐标
     * @param Radius 查询半径
     * @param OutResults 输出落入圆形内的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QueryRadius(
        const TSpatialHashContainer2D<TElement>& Container,
        const FVector2D& Center,
        float Radius,
        TArray<TElement>& OutResults,
        TPredicate&& Predicate = {}
    ) {
        OutResults.Reset();
        QueryRadiusMatching(
            Container,
            Center,
            Radius,
            [&OutResults](const TElement& Item) { OutResults.Add(Item); },
            Forward<TPredicate>(Predicate)
        );
    }

    /**
     * @brief 轴对齐矩形 AABB 流式范围查询（零堆分配，单次遍历就地消费）
     * @tparam TElement 元素类型
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标空间容器
     * @param Box 查询矩形包围盒
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QueryAABBMatching(
        const TSpatialHashContainer2D<TElement>& Container,
        const FBox2D& Box,
        TConsumer&& Consumer,
        TPredicate&& Predicate = {}
    ) {
        if (Container.Num() == 0
            || !Box.bIsValid
            || !Game::Spatial::Private::IsFiniteVector(Box.Min)
            || !Game::Spatial::Private::IsFiniteVector(Box.Max)
            || Box.Min.X > Box.Max.X
            || Box.Min.Y > Box.Max.Y) {
            return;
        }

        const int32 MinX = Container.ComputeCellCoord(Box.Min.X);
        const int32 MaxX = Container.ComputeCellCoord(Box.Max.X);
        const int32 MinY = Container.ComputeCellCoord(Box.Min.Y);
        const int32 MaxY = Container.ComputeCellCoord(Box.Max.Y);

        auto CheckAndConsume = [&Box, &Consumer, &Predicate](const TElement& Item, const FVector2D& Loc) {
            if (Loc.X >= Box.Min.X && Loc.X <= Box.Max.X && Loc.Y >= Box.Min.Y && Loc.Y <= Box.Max.Y) {
                Game::Spatial::Private::TryConsumeElement(Consumer, Predicate, Item, Loc);
            }
        };

        if (Game::Spatial::Private::ShouldUseSparseScan2D(MinX, MaxX, MinY, MaxY, Container.Num())) {
            Container.ForEachElement(CheckAndConsume);
            return;
        }

        Container.ForEachCellInBox(MinX, MaxX, MinY, MaxY, [&Container, &CheckAndConsume](uint64 Key) {
            Container.ForEachElementInCell(Key, CheckAndConsume);
        });
    }

    /**
     * @brief 轴对齐矩形 AABB 范围查询
     * @tparam TElement 元素类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标空间容器
     * @param Box 查询矩形包围盒
     * @param OutResults 输出落入矩形内的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QueryAABB(
        const TSpatialHashContainer2D<TElement>& Container,
        const FBox2D& Box,
        TArray<TElement>& OutResults,
        TPredicate&& Predicate = {}
    ) {
        OutResults.Reset();
        QueryAABBMatching(
            Container,
            Box,
            [&OutResults](const TElement& Item) { OutResults.Add(Item); },
            Forward<TPredicate>(Predicate)
        );
    }

    /**
     * @brief 2D 扇形流式范围查询（紧致扇形包围盒剪枝 + 无开方角判别 + 零堆分配）
     * @tparam TElement 元素类型
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标空间容器
     * @param Center 扇形圆心
     * @param ForwardDir 扇形基准朝向（零向量将被拒绝返回空）
     * @param Radius 扇形半径（须为有限正数）
     * @param HalfAngleDegrees 扇形半角（度数，须为有限正数）
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QuerySectorMatching(
        const TSpatialHashContainer2D<TElement>& Container,
        const FVector2D& Center,
        const FVector2D& ForwardDir,
        float Radius,
        float HalfAngleDegrees,
        TConsumer&& Consumer,
        TPredicate&& Predicate = {}
    ) {
        if (Container.Num() == 0
            || !Game::Spatial::Private::IsFiniteVector(Center)
            || !Game::Spatial::Private::IsFiniteVector(ForwardDir)
            || !Game::Spatial::Private::IsFiniteCoord(Radius)
            || !Game::Spatial::Private::IsFiniteCoord(HalfAngleDegrees)
            || Radius <= 0.0f
            || HalfAngleDegrees <= 0.0f
            || ForwardDir.IsNearlyZero()) {
            return;
        }

        if (HalfAngleDegrees >= 180.0f) {
            QueryRadiusMatching(Container, Center, Radius, Forward<TConsumer>(Consumer), Forward<TPredicate>(Predicate));
            return;
        }

        const double RadiusD = static_cast<double>(Radius);
        double RadiusSq = RadiusD * RadiusD;
        const double HalfAngleRad = FMath::DegreesToRadians(static_cast<double>(HalfAngleDegrees));
        const double CosHalfAngle = FMath::Cos(HalfAngleRad);
        const double SinHalfAngle = FMath::Sin(HalfAngleRad);
        const double CosHalfAngleSq = CosHalfAngle * CosHalfAngle;
        const FVector2D SafeForward = ForwardDir.GetSafeNormal();

        double MinOffsetX = 0.0;
        double MaxOffsetX = 0.0;
        double MinOffsetY = 0.0;
        double MaxOffsetY = 0.0;
        Game::Spatial::Private::ComputeAxisConeExtent(SafeForward.X, CosHalfAngle, SinHalfAngle, RadiusD, MinOffsetX, MaxOffsetX);
        Game::Spatial::Private::ComputeAxisConeExtent(SafeForward.Y, CosHalfAngle, SinHalfAngle, RadiusD, MinOffsetY, MaxOffsetY);

        const int32 MinX = Container.ComputeCellCoord(Center.X + MinOffsetX - UE_DOUBLE_KINDA_SMALL_NUMBER);
        const int32 MaxX = Container.ComputeCellCoord(Center.X + MaxOffsetX + UE_DOUBLE_KINDA_SMALL_NUMBER);
        const int32 MinY = Container.ComputeCellCoord(Center.Y + MinOffsetY - UE_DOUBLE_KINDA_SMALL_NUMBER);
        const int32 MaxY = Container.ComputeCellCoord(Center.Y + MaxOffsetY + UE_DOUBLE_KINDA_SMALL_NUMBER);

        auto CheckAndConsume = [&Center, &SafeForward, RadiusSq, CosHalfAngle, CosHalfAngleSq, &Consumer, &Predicate](
            const TElement& Item,
            const FVector2D& Loc
        ) {
            const FVector2D ToLoc = Loc - Center;
            const double DistSq = ToLoc.SizeSquared();
            if (DistSq > RadiusSq) {
                return;
            }
            const double Proj = FVector2D::DotProduct(ToLoc, SafeForward);
            if (Game::Spatial::Private::IsWithinConeAngle(Proj, DistSq, CosHalfAngle, CosHalfAngleSq)) {
                Game::Spatial::Private::TryConsumeElement(Consumer, Predicate, Item, Loc);
            }
        };

        if (Game::Spatial::Private::ShouldUseSparseScan2D(MinX, MaxX, MinY, MaxY, Container.Num())) {
            Container.ForEachElement(CheckAndConsume);
            return;
        }

        ForEachCellInRadiusBounds(
            Container,
            Center,
            MinX,
            MaxX,
            MinY,
            MaxY,
            RadiusSq,
            0,
            0,
            false,
            [&Container, &CheckAndConsume](uint64 CellKey) {
                Container.ForEachElementInCell(CellKey, CheckAndConsume);
            }
        );
    }

    /**
     * @brief 2D 扇形范围查询
     * @tparam TElement 元素类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标空间容器
     * @param Center 扇形圆心
     * @param ForwardDir 扇形基准朝向
     * @param Radius 扇形半径
     * @param HalfAngleDegrees 扇形半角（度数）
     * @param OutResults 输出落入扇形内的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QuerySector(
        const TSpatialHashContainer2D<TElement>& Container,
        const FVector2D& Center,
        const FVector2D& ForwardDir,
        float Radius,
        float HalfAngleDegrees,
        TArray<TElement>& OutResults,
        TPredicate&& Predicate = {}
    ) {
        OutResults.Reset();
        QuerySectorMatching(
            Container,
            Center,
            ForwardDir,
            Radius,
            HalfAngleDegrees,
            [&OutResults](const TElement& Item) { OutResults.Add(Item); },
            Forward<TPredicate>(Predicate)
        );
    }

    /**
     * @brief 查找距离给定二维中心点最近的目标元素（中心格优先收缩、距离前置谓词短路与稀疏快路径）
     * @tparam TElement 元素类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标底层空间哈希容器
     * @param Center 中心点二维坐标
     * @param MaxSearchRadius 最大检索截断半径（须为有限正数）
     * @param OutNearest 输出最近元素
     * @param Predicate 过滤谓词
     * @return 存在有效元素返回 true，指定半径内无元素返回 false
     */
    template <typename TElement, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static bool QueryNearest(
        const TSpatialHashContainer2D<TElement>& Container,
        const FVector2D& Center,
        float MaxSearchRadius,
        TElement& OutNearest,
        TPredicate&& Predicate = {}
    ) {
        if (Container.Num() == 0
            || !Game::Spatial::Private::IsFiniteVector(Center)
            || !Game::Spatial::Private::IsFiniteCoord(MaxSearchRadius)
            || MaxSearchRadius <= 0.0f) {
            return false;
        }

        double ClosestDistSq = static_cast<double>(MaxSearchRadius) * static_cast<double>(MaxSearchRadius);
        const TElement* BestCandidate = nullptr;

        auto ConsiderElement = [&Center, &ClosestDistSq, &BestCandidate, &Predicate](
            const TElement& Item,
            const FVector2D& Loc
        ) {
            const double DistSq = FVector2D::DistSquared(Center, Loc);
            if (DistSq > ClosestDistSq || (BestCandidate != nullptr && DistSq == ClosestDistSq)) {
                return;
            }
            if (Game::Spatial::Private::InvokePredicate(Predicate, Item, Loc)) {
                ClosestDistSq = DistSq;
                BestCandidate = &Item;
            }
        };

        const int32 CenterCellX = Container.ComputeCellCoord(Center.X);
        const int32 CenterCellY = Container.ComputeCellCoord(Center.Y);
        Container.ForEachElementInCell(Container.PackCellKey(CenterCellX, CenterCellY), ConsiderElement);

        const double SearchRadius = FMath::Sqrt(ClosestDistSq);
        const int32 MinX = Container.ComputeCellCoord(Center.X - SearchRadius);
        const int32 MaxX = Container.ComputeCellCoord(Center.X + SearchRadius);
        const int32 MinY = Container.ComputeCellCoord(Center.Y - SearchRadius);
        const int32 MaxY = Container.ComputeCellCoord(Center.Y + SearchRadius);

        if (MinX != CenterCellX || MaxX != CenterCellX || MinY != CenterCellY || MaxY != CenterCellY) {
            if (Game::Spatial::Private::ShouldUseSparseScan2D(MinX, MaxX, MinY, MaxY, Container.Num())) {
                Container.ForEachElement(ConsiderElement);
            } else {
                ForEachCellInRadiusBounds(
                    Container,
                    Center,
                    MinX,
                    MaxX,
                    MinY,
                    MaxY,
                    ClosestDistSq,
                    CenterCellX,
                    CenterCellY,
                    true,
                    [&Container, &ConsiderElement](uint64 CellKey) {
                        Container.ForEachElementInCell(CellKey, ConsiderElement);
                    }
                );
            }
        }

        if (BestCandidate == nullptr) {
            return false;
        }
        OutNearest = *BestCandidate;
        return true;
    }

    /**
     * @brief 流式查找距离给定二维中心点最近的 K 个目标元素（K <= 16 零堆分配，按距离升序流式消费）
     * @tparam TElement 元素类型
     * @tparam TConsumer 消费回调类型 (const TElement&) 或 (const TElement&, double DistSq)
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标底层空间哈希容器
     * @param Center 中心点二维坐标
     * @param MaxSearchRadius 最大检索截断半径（须为有限正数）
     * @param K 期望获取的最大近邻数量（须大于 0）
     * @param Consumer 按距离升序接收命中元素的消费者
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QueryKNearestMatching(
        const TSpatialHashContainer2D<TElement>& Container,
        const FVector2D& Center,
        float MaxSearchRadius,
        int32 K,
        TConsumer&& Consumer,
        TPredicate&& Predicate = {}
    ) {
        if (K <= 0
            || Container.Num() == 0
            || !Game::Spatial::Private::IsFiniteVector(Center)
            || !Game::Spatial::Private::IsFiniteCoord(MaxSearchRadius)
            || MaxSearchRadius <= 0.0f) {
            return;
        }

        Game::Spatial::Private::TSpatialKNearestBuffer<TElement, 16> Buffer(K);
        double MaxValidDistSq = static_cast<double>(MaxSearchRadius) * static_cast<double>(MaxSearchRadius);

        auto ConsiderElement = [&Buffer, K, &Center, &MaxValidDistSq, &Predicate](
            const TElement& Item,
            const FVector2D& Loc
        ) {
            const double DistSq = FVector2D::DistSquared(Center, Loc);
            if (DistSq > MaxValidDistSq || (Buffer.Num() == K && DistSq >= MaxValidDistSq)) {
                return;
            }
            if (Game::Spatial::Private::InvokePredicate(Predicate, Item, Loc)) {
                Buffer.TryInsert(Item, DistSq, MaxValidDistSq);
            }
        };

        const int32 CenterCellX = Container.ComputeCellCoord(Center.X);
        const int32 CenterCellY = Container.ComputeCellCoord(Center.Y);
        Container.ForEachElementInCell(Container.PackCellKey(CenterCellX, CenterCellY), ConsiderElement);

        const double SearchRadius = FMath::Sqrt(MaxValidDistSq);
        const int32 MinX = Container.ComputeCellCoord(Center.X - SearchRadius);
        const int32 MaxX = Container.ComputeCellCoord(Center.X + SearchRadius);
        const int32 MinY = Container.ComputeCellCoord(Center.Y - SearchRadius);
        const int32 MaxY = Container.ComputeCellCoord(Center.Y + SearchRadius);

        if (MinX != CenterCellX || MaxX != CenterCellX || MinY != CenterCellY || MaxY != CenterCellY) {
            if (Game::Spatial::Private::ShouldUseSparseScan2D(MinX, MaxX, MinY, MaxY, Container.Num())) {
                Buffer.Reset();
                Container.ForEachElement(ConsiderElement);
            } else {
                ForEachCellInRadiusBounds(
                    Container,
                    Center,
                    MinX,
                    MaxX,
                    MinY,
                    MaxY,
                    MaxValidDistSq,
                    CenterCellX,
                    CenterCellY,
                    true,
                    [&Container, &ConsiderElement](uint64 CellKey) {
                        Container.ForEachElementInCell(CellKey, ConsiderElement);
                    }
                );
            }
        }

        Buffer.Emit(Forward<TConsumer>(Consumer));
    }

    /**
     * @brief 查找距离给定二维中心点最近的 K 个目标元素（按距离升序输出）
     * @tparam TElement 元素类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标底层空间哈希容器
     * @param Center 中心点二维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param K 期望获取的最大近邻数量
     * @param OutNearestList 输出最近的 K 个元素列表（由近及远升序）
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QueryKNearest(
        const TSpatialHashContainer2D<TElement>& Container,
        const FVector2D& Center,
        float MaxSearchRadius,
        int32 K,
        TArray<TElement>& OutNearestList,
        TPredicate&& Predicate = {}
    ) {
        OutNearestList.Reset();
        QueryKNearestMatching(
            Container,
            Center,
            MaxSearchRadius,
            K,
            [&OutNearestList](const TElement& Item) { OutNearestList.Add(Item); },
            Forward<TPredicate>(Predicate)
        );
    }

private:
    template <typename TElement, typename TCellVisitor>
    static void ForEachCellInRadiusBounds(
        const TSpatialHashContainer2D<TElement>& Container,
        const FVector2D& Center,
        int32 MinX,
        int32 MaxX,
        int32 MinY,
        int32 MaxY,
        double& InOutRadiusSq,
        int32 SkipX,
        int32 SkipY,
        bool bSkipCenterCell,
        TCellVisitor&& CellVisitor
    ) {
        const double CellSize = static_cast<double>(Container.GetCellSize());
        constexpr int32 MinCoord = TSpatialHashContainer2D<TElement>::MIN_CELL_COORD;
        constexpr int32 MaxCoord = TSpatialHashContainer2D<TElement>::MAX_CELL_COORD;

        for (int32 X = MinX; X <= MaxX; ++X) {
            const double Dx = Game::Spatial::Private::Get1DMinDistanceToCell(Center.X, X, CellSize, MinCoord, MaxCoord);
            const double DxSq = Dx * Dx;
            if (DxSq > InOutRadiusSq) {
                continue;
            }

            for (int32 Y = MinY; Y <= MaxY; ++Y) {
                if (bSkipCenterCell && X == SkipX && Y == SkipY) {
                    continue;
                }
                const double Dy = Game::Spatial::Private::Get1DMinDistanceToCell(Center.Y, Y, CellSize, MinCoord, MaxCoord);
                if (DxSq + Dy * Dy > InOutRadiusSq) {
                    continue;
                }
                CellVisitor(Container.PackCellKey(X, Y));
            }
        }
    }
};
