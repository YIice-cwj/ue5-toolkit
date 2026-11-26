/**
 * @file FSpatialQuery3D.h
 * @brief 3D 空间立体几何范围求交与近邻启发式搜索算法（全 LWC 双精度、无开方紧致锥体剪枝、稀疏快路径与零分配流式原语）
 */

#pragma once

#include "CoreMinimal.h"
#include "Math/Box.h"
#include "Math/Vector.h"
#include "Core/SpatialQueryCommon.h"
#include "Grid/TSpatialHashContainer3D.h"

/**
 * @brief 3D 空间几何查询纯静态算法工具类
 */
struct FSpatialQuery3D {
public:
    /**
     * @brief 三维半径球形流式范围查询（零堆分配，单次遍历就地消费）
     * @tparam TElement 元素类型
     * @tparam TConsumer 消费回调类型 (const TElement&) 或 (const TElement&, const FVector&)
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标空间容器
     * @param Center 球心坐标
     * @param Radius 查询半径（须为有限正数）
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QueryRadiusMatching(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Center,
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
        const int32 MinZ = Container.ComputeCellCoord(Center.Z - RadiusD);
        const int32 MaxZ = Container.ComputeCellCoord(Center.Z + RadiusD);

        auto CheckAndConsume = [&Center, RadiusSq, &Consumer, &Predicate](const TElement& Item, const FVector& Loc) {
            if (FVector::DistSquared(Center, Loc) <= RadiusSq) {
                Game::Spatial::Private::TryConsumeElement(Consumer, Predicate, Item, Loc);
            }
        };

        if (Game::Spatial::Private::ShouldUseSparseScan3D(MinX, MaxX, MinY, MaxY, MinZ, MaxZ, Container.Num())) {
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
            MinZ,
            MaxZ,
            RadiusSq,
            0,
            0,
            0,
            false,
            [&Container, &CheckAndConsume](uint64 CellKey) {
                Container.ForEachElementInCell(CellKey, CheckAndConsume);
            }
        );
    }

    /**
     * @brief 三维半径球形范围查询
     * @tparam TElement 元素类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标空间容器
     * @param Center 球心坐标
     * @param Radius 查询半径
     * @param OutResults 输出落入球体内的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QueryRadius(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Center,
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
     * @brief 轴对齐三维包围盒 AABB 流式范围查询（零堆分配，单次遍历就地消费）
     * @tparam TElement 元素类型
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标空间容器
     * @param Box 查询轴对齐包围盒
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QueryAABBMatching(
        const TSpatialHashContainer3D<TElement>& Container,
        const FBox& Box,
        TConsumer&& Consumer,
        TPredicate&& Predicate = {}
    ) {
        if (Container.Num() == 0
            || !Box.IsValid
            || !Game::Spatial::Private::IsFiniteVector(Box.Min)
            || !Game::Spatial::Private::IsFiniteVector(Box.Max)
            || Box.Min.X > Box.Max.X
            || Box.Min.Y > Box.Max.Y
            || Box.Min.Z > Box.Max.Z) {
            return;
        }

        const int32 MinX = Container.ComputeCellCoord(Box.Min.X);
        const int32 MaxX = Container.ComputeCellCoord(Box.Max.X);
        const int32 MinY = Container.ComputeCellCoord(Box.Min.Y);
        const int32 MaxY = Container.ComputeCellCoord(Box.Max.Y);
        const int32 MinZ = Container.ComputeCellCoord(Box.Min.Z);
        const int32 MaxZ = Container.ComputeCellCoord(Box.Max.Z);

        auto CheckAndConsume = [&Box, &Consumer, &Predicate](const TElement& Item, const FVector& Loc) {
            if (Loc.X >= Box.Min.X && Loc.X <= Box.Max.X
                && Loc.Y >= Box.Min.Y && Loc.Y <= Box.Max.Y
                && Loc.Z >= Box.Min.Z && Loc.Z <= Box.Max.Z) {
                Game::Spatial::Private::TryConsumeElement(Consumer, Predicate, Item, Loc);
            }
        };

        if (Game::Spatial::Private::ShouldUseSparseScan3D(MinX, MaxX, MinY, MaxY, MinZ, MaxZ, Container.Num())) {
            Container.ForEachElement(CheckAndConsume);
            return;
        }

        Container.ForEachCellInBox(MinX, MaxX, MinY, MaxY, MinZ, MaxZ, [&Container, &CheckAndConsume](uint64 Key) {
            Container.ForEachElementInCell(Key, CheckAndConsume);
        });
    }

    /**
     * @brief 轴对齐三维包围盒 AABB 范围查询
     * @tparam TElement 元素类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标空间容器
     * @param Box 查询轴对齐包围盒
     * @param OutResults 输出落入包围盒内的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QueryAABB(
        const TSpatialHashContainer3D<TElement>& Container,
        const FBox& Box,
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
     * @brief 三维直立圆柱体流式范围查询（零堆分配，单次遍历就地消费）
     * @tparam TElement 元素类型
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标空间容器
     * @param Center 圆柱中心坐标
     * @param Radius 水平截面半径（须为有限正数）
     * @param HalfHeight 垂直半高（须为有限正数）
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QueryCylinderMatching(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Center,
        float Radius,
        float HalfHeight,
        TConsumer&& Consumer,
        TPredicate&& Predicate = {}
    ) {
        if (Container.Num() == 0
            || !Game::Spatial::Private::IsFiniteVector(Center)
            || !Game::Spatial::Private::IsFiniteCoord(Radius)
            || !Game::Spatial::Private::IsFiniteCoord(HalfHeight)
            || Radius <= 0.0f
            || HalfHeight <= 0.0f) {
            return;
        }

        const double RadiusD = static_cast<double>(Radius);
        const double HalfHeightD = static_cast<double>(HalfHeight);
        const double RadiusSq = RadiusD * RadiusD;

        const int32 MinX = Container.ComputeCellCoord(Center.X - RadiusD);
        const int32 MaxX = Container.ComputeCellCoord(Center.X + RadiusD);
        const int32 MinY = Container.ComputeCellCoord(Center.Y - RadiusD);
        const int32 MaxY = Container.ComputeCellCoord(Center.Y + RadiusD);
        const int32 MinZ = Container.ComputeCellCoord(Center.Z - HalfHeightD);
        const int32 MaxZ = Container.ComputeCellCoord(Center.Z + HalfHeightD);

        auto CheckAndConsume = [&Center, RadiusSq, HalfHeightD, &Consumer, &Predicate](const TElement& Item, const FVector& Loc) {
            if (FMath::Abs(Loc.Z - Center.Z) > HalfHeightD) {
                return;
            }
            const double DistX = Loc.X - Center.X;
            const double DistY = Loc.Y - Center.Y;
            if ((DistX * DistX + DistY * DistY) <= RadiusSq) {
                Game::Spatial::Private::TryConsumeElement(Consumer, Predicate, Item, Loc);
            }
        };

        if (Game::Spatial::Private::ShouldUseSparseScan3D(MinX, MaxX, MinY, MaxY, MinZ, MaxZ, Container.Num())) {
            Container.ForEachElement(CheckAndConsume);
            return;
        }

        const double CellSize = static_cast<double>(Container.GetCellSize());
        constexpr int32 MinCoord = TSpatialHashContainer3D<TElement>::MIN_CELL_COORD;
        constexpr int32 MaxCoord = TSpatialHashContainer3D<TElement>::MAX_CELL_COORD;
        for (int32 X = MinX; X <= MaxX; ++X) {
            const double Dx = Game::Spatial::Private::Get1DMinDistanceToCell(Center.X, X, CellSize, MinCoord, MaxCoord);
            const double DxSq = Dx * Dx;
            if (DxSq > RadiusSq) {
                continue;
            }

            for (int32 Y = MinY; Y <= MaxY; ++Y) {
                const double Dy = Game::Spatial::Private::Get1DMinDistanceToCell(Center.Y, Y, CellSize, MinCoord, MaxCoord);
                if (DxSq + Dy * Dy > RadiusSq) {
                    continue;
                }
                VisitCylinderZColumn(Container, X, Y, MinZ, MaxZ, CheckAndConsume);
            }
        }
    }

    /**
     * @brief 三维直立圆柱体范围查询
     * @tparam TElement 元素类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标空间容器
     * @param Center 圆柱中心坐标
     * @param Radius 水平截面半径
     * @param HalfHeight 垂直半高
     * @param OutResults 输出落入圆柱内的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QueryCylinder(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Center,
        float Radius,
        float HalfHeight,
        TArray<TElement>& OutResults,
        TPredicate&& Predicate = {}
    ) {
        OutResults.Reset();
        QueryCylinderMatching(
            Container,
            Center,
            Radius,
            HalfHeight,
            [&OutResults](const TElement& Item) { OutResults.Add(Item); },
            Forward<TPredicate>(Predicate)
        );
    }

    /**
     * @brief 三维空间视锥/前向光锥流式范围查询（紧致三维锥体 AABB 剪枝 + 无开方角判别 + 零堆分配）
     * @tparam TElement 元素类型
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标空间容器
     * @param Apex 锥体顶点世界坐标
     * @param Direction 锥体中心对称朝向（零向量将被拒绝返回空）
     * @param Radius 锥体最大作用距离（须为有限正数）
     * @param HalfAngleDegrees 锥体半顶角（度数，须为有限正数）
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QueryConeMatching(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Apex,
        const FVector& Direction,
        float Radius,
        float HalfAngleDegrees,
        TConsumer&& Consumer,
        TPredicate&& Predicate = {}
    ) {
        if (Container.Num() == 0
            || !Game::Spatial::Private::IsFiniteVector(Apex)
            || !Game::Spatial::Private::IsFiniteVector(Direction)
            || !Game::Spatial::Private::IsFiniteCoord(Radius)
            || !Game::Spatial::Private::IsFiniteCoord(HalfAngleDegrees)
            || Radius <= 0.0f
            || HalfAngleDegrees <= 0.0f
            || Direction.IsNearlyZero()) {
            return;
        }

        if (HalfAngleDegrees >= 180.0f) {
            QueryRadiusMatching(Container, Apex, Radius, Forward<TConsumer>(Consumer), Forward<TPredicate>(Predicate));
            return;
        }

        const double RadiusD = static_cast<double>(Radius);
        double RadiusSq = RadiusD * RadiusD;
        const double HalfAngleRad = FMath::DegreesToRadians(static_cast<double>(HalfAngleDegrees));
        const double CosHalfAngle = FMath::Cos(HalfAngleRad);
        const double SinHalfAngle = FMath::Sin(HalfAngleRad);
        const double CosHalfAngleSq = CosHalfAngle * CosHalfAngle;
        const FVector SafeDirection = Direction.GetSafeNormal();

        double MinOffsetX = 0.0;
        double MaxOffsetX = 0.0;
        double MinOffsetY = 0.0;
        double MaxOffsetY = 0.0;
        double MinOffsetZ = 0.0;
        double MaxOffsetZ = 0.0;
        Game::Spatial::Private::ComputeAxisConeExtent(SafeDirection.X, CosHalfAngle, SinHalfAngle, RadiusD, MinOffsetX, MaxOffsetX);
        Game::Spatial::Private::ComputeAxisConeExtent(SafeDirection.Y, CosHalfAngle, SinHalfAngle, RadiusD, MinOffsetY, MaxOffsetY);
        Game::Spatial::Private::ComputeAxisConeExtent(SafeDirection.Z, CosHalfAngle, SinHalfAngle, RadiusD, MinOffsetZ, MaxOffsetZ);

        const int32 MinX = Container.ComputeCellCoord(Apex.X + MinOffsetX - UE_DOUBLE_KINDA_SMALL_NUMBER);
        const int32 MaxX = Container.ComputeCellCoord(Apex.X + MaxOffsetX + UE_DOUBLE_KINDA_SMALL_NUMBER);
        const int32 MinY = Container.ComputeCellCoord(Apex.Y + MinOffsetY - UE_DOUBLE_KINDA_SMALL_NUMBER);
        const int32 MaxY = Container.ComputeCellCoord(Apex.Y + MaxOffsetY + UE_DOUBLE_KINDA_SMALL_NUMBER);
        const int32 MinZ = Container.ComputeCellCoord(Apex.Z + MinOffsetZ - UE_DOUBLE_KINDA_SMALL_NUMBER);
        const int32 MaxZ = Container.ComputeCellCoord(Apex.Z + MaxOffsetZ + UE_DOUBLE_KINDA_SMALL_NUMBER);

        auto CheckAndConsume = [&Apex, &SafeDirection, RadiusSq, CosHalfAngle, CosHalfAngleSq, &Consumer, &Predicate](
            const TElement& Item,
            const FVector& Loc
        ) {
            const FVector ToLoc = Loc - Apex;
            const double DistSq = ToLoc.SizeSquared();
            if (DistSq > RadiusSq) {
                return;
            }
            const double Proj = FVector::DotProduct(ToLoc, SafeDirection);
            if (Game::Spatial::Private::IsWithinConeAngle(Proj, DistSq, CosHalfAngle, CosHalfAngleSq)) {
                Game::Spatial::Private::TryConsumeElement(Consumer, Predicate, Item, Loc);
            }
        };

        if (Game::Spatial::Private::ShouldUseSparseScan3D(MinX, MaxX, MinY, MaxY, MinZ, MaxZ, Container.Num())) {
            Container.ForEachElement(CheckAndConsume);
            return;
        }

        ForEachCellInRadiusBounds(
            Container,
            Apex,
            MinX,
            MaxX,
            MinY,
            MaxY,
            MinZ,
            MaxZ,
            RadiusSq,
            0,
            0,
            0,
            false,
            [&Container, &CheckAndConsume](uint64 CellKey) {
                Container.ForEachElementInCell(CellKey, CheckAndConsume);
            }
        );
    }

    /**
     * @brief 三维空间视锥/前向光锥范围查询
     * @tparam TElement 元素类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标空间容器
     * @param Apex 锥体顶点世界坐标
     * @param Direction 锥体中心对称朝向
     * @param Radius 锥体最大作用距离
     * @param HalfAngleDegrees 锥体半顶角（度数）
     * @param OutResults 输出落入锥体内的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QueryCone(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Apex,
        const FVector& Direction,
        float Radius,
        float HalfAngleDegrees,
        TArray<TElement>& OutResults,
        TPredicate&& Predicate = {}
    ) {
        OutResults.Reset();
        QueryConeMatching(
            Container,
            Apex,
            Direction,
            Radius,
            HalfAngleDegrees,
            [&OutResults](const TElement& Item) { OutResults.Add(Item); },
            Forward<TPredicate>(Predicate)
        );
    }

    /**
     * @brief 查找距离给定三维中心点最近的目标元素（中心格优先收缩、三级轴向动态剪枝、距离前置短路与稀疏快路径）
     * @tparam TElement 元素类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标底层空间哈希容器
     * @param Center 中心点三维坐标
     * @param MaxSearchRadius 最大检索截断半径（须为有限正数）
     * @param OutNearest 输出最近元素
     * @param Predicate 过滤谓词
     * @return 存在有效元素返回 true，指定半径内无元素返回 false
     */
    template <typename TElement, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static bool QueryNearest(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Center,
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
            const FVector& Loc
        ) {
            const double DistSq = FVector::DistSquared(Center, Loc);
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
        const int32 CenterCellZ = Container.ComputeCellCoord(Center.Z);
        Container.ForEachElementInCell(Container.PackCellKey(CenterCellX, CenterCellY, CenterCellZ), ConsiderElement);

        const double SearchRadius = FMath::Sqrt(ClosestDistSq);
        const int32 MinX = Container.ComputeCellCoord(Center.X - SearchRadius);
        const int32 MaxX = Container.ComputeCellCoord(Center.X + SearchRadius);
        const int32 MinY = Container.ComputeCellCoord(Center.Y - SearchRadius);
        const int32 MaxY = Container.ComputeCellCoord(Center.Y + SearchRadius);
        const int32 MinZ = Container.ComputeCellCoord(Center.Z - SearchRadius);
        const int32 MaxZ = Container.ComputeCellCoord(Center.Z + SearchRadius);

        if (MinX != CenterCellX
            || MaxX != CenterCellX
            || MinY != CenterCellY
            || MaxY != CenterCellY
            || MinZ != CenterCellZ
            || MaxZ != CenterCellZ) {
            if (Game::Spatial::Private::ShouldUseSparseScan3D(MinX, MaxX, MinY, MaxY, MinZ, MaxZ, Container.Num())) {
                Container.ForEachElement(ConsiderElement);
            } else {
                ForEachCellInRadiusBounds(
                    Container,
                    Center,
                    MinX,
                    MaxX,
                    MinY,
                    MaxY,
                    MinZ,
                    MaxZ,
                    ClosestDistSq,
                    CenterCellX,
                    CenterCellY,
                    CenterCellZ,
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
     * @brief 流式查找距离给定三维中心点最近的 K 个目标元素（K <= 16 零堆分配，按距离升序流式消费）
     * @tparam TElement 元素类型
     * @tparam TConsumer 消费回调类型 (const TElement&) 或 (const TElement&, double DistSq)
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标底层空间哈希容器
     * @param Center 中心点三维坐标
     * @param MaxSearchRadius 最大检索截断半径（须为有限正数）
     * @param K 期望获取的最大近邻数量（须大于 0）
     * @param Consumer 按距离升序接收命中元素的消费者
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QueryKNearestMatching(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Center,
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
            const FVector& Loc
        ) {
            const double DistSq = FVector::DistSquared(Center, Loc);
            if (DistSq > MaxValidDistSq || (Buffer.Num() == K && DistSq >= MaxValidDistSq)) {
                return;
            }
            if (Game::Spatial::Private::InvokePredicate(Predicate, Item, Loc)) {
                Buffer.TryInsert(Item, DistSq, MaxValidDistSq);
            }
        };

        const int32 CenterCellX = Container.ComputeCellCoord(Center.X);
        const int32 CenterCellY = Container.ComputeCellCoord(Center.Y);
        const int32 CenterCellZ = Container.ComputeCellCoord(Center.Z);
        Container.ForEachElementInCell(Container.PackCellKey(CenterCellX, CenterCellY, CenterCellZ), ConsiderElement);

        const double SearchRadius = FMath::Sqrt(MaxValidDistSq);
        const int32 MinX = Container.ComputeCellCoord(Center.X - SearchRadius);
        const int32 MaxX = Container.ComputeCellCoord(Center.X + SearchRadius);
        const int32 MinY = Container.ComputeCellCoord(Center.Y - SearchRadius);
        const int32 MaxY = Container.ComputeCellCoord(Center.Y + SearchRadius);
        const int32 MinZ = Container.ComputeCellCoord(Center.Z - SearchRadius);
        const int32 MaxZ = Container.ComputeCellCoord(Center.Z + SearchRadius);

        if (MinX != CenterCellX
            || MaxX != CenterCellX
            || MinY != CenterCellY
            || MaxY != CenterCellY
            || MinZ != CenterCellZ
            || MaxZ != CenterCellZ) {
            if (Game::Spatial::Private::ShouldUseSparseScan3D(MinX, MaxX, MinY, MaxY, MinZ, MaxZ, Container.Num())) {
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
                    MinZ,
                    MaxZ,
                    MaxValidDistSq,
                    CenterCellX,
                    CenterCellY,
                    CenterCellZ,
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
     * @brief 查找距离给定三维中心点最近的 K 个目标元素（按距离升序输出）
     * @tparam TElement 元素类型
     * @tparam TPredicate 过滤谓词类型（默认全通）
     * @param Container 目标底层空间哈希容器
     * @param Center 中心点三维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param K 期望获取的最大近邻数量
     * @param OutNearestList 输出最近的 K 个元素列表（由近及远升序）
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
    static void QueryKNearest(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Center,
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
    static void VisitZColumnInRadius(
        const TSpatialHashContainer3D<TElement>& Container,
        int32 X,
        int32 Y,
        int32 MinZ,
        int32 MaxZ,
        double CenterZ,
        double CellSize,
        double DxySq,
        double& InOutRadiusSq,
        int32 SkipX,
        int32 SkipY,
        int32 SkipZ,
        bool bSkipCenterCell,
        TCellVisitor&& CellVisitor
    ) {
        constexpr int32 MinCoord = TSpatialHashContainer3D<TElement>::MIN_CELL_COORD;
        constexpr int32 MaxCoord = TSpatialHashContainer3D<TElement>::MAX_CELL_COORD;
        for (int32 Z = MinZ; Z <= MaxZ; ++Z) {
            if (bSkipCenterCell && X == SkipX && Y == SkipY && Z == SkipZ) {
                continue;
            }
            const double Dz = Game::Spatial::Private::Get1DMinDistanceToCell(CenterZ, Z, CellSize, MinCoord, MaxCoord);
            if (DxySq + Dz * Dz > InOutRadiusSq) {
                continue;
            }
            CellVisitor(Container.PackCellKey(X, Y, Z));
        }
    }

    template <typename TElement, typename TCellVisitor>
    static void ForEachCellInRadiusBounds(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Center,
        int32 MinX,
        int32 MaxX,
        int32 MinY,
        int32 MaxY,
        int32 MinZ,
        int32 MaxZ,
        double& InOutRadiusSq,
        int32 SkipX,
        int32 SkipY,
        int32 SkipZ,
        bool bSkipCenterCell,
        TCellVisitor&& CellVisitor
    ) {
        const double CellSize = static_cast<double>(Container.GetCellSize());
        constexpr int32 MinCoord = TSpatialHashContainer3D<TElement>::MIN_CELL_COORD;
        constexpr int32 MaxCoord = TSpatialHashContainer3D<TElement>::MAX_CELL_COORD;

        for (int32 X = MinX; X <= MaxX; ++X) {
            const double Dx = Game::Spatial::Private::Get1DMinDistanceToCell(Center.X, X, CellSize, MinCoord, MaxCoord);
            const double DxSq = Dx * Dx;
            if (DxSq > InOutRadiusSq) {
                continue;
            }

            for (int32 Y = MinY; Y <= MaxY; ++Y) {
                const double Dy = Game::Spatial::Private::Get1DMinDistanceToCell(Center.Y, Y, CellSize, MinCoord, MaxCoord);
                const double DxySq = DxSq + Dy * Dy;
                if (DxySq > InOutRadiusSq) {
                    continue;
                }
                VisitZColumnInRadius(
                    Container,
                    X,
                    Y,
                    MinZ,
                    MaxZ,
                    Center.Z,
                    CellSize,
                    DxySq,
                    InOutRadiusSq,
                    SkipX,
                    SkipY,
                    SkipZ,
                    bSkipCenterCell,
                    CellVisitor
                );
            }
        }
    }

    template <typename TElement, typename TElementVisitor>
    static void VisitCylinderZColumn(
        const TSpatialHashContainer3D<TElement>& Container,
        int32 X,
        int32 Y,
        int32 MinZ,
        int32 MaxZ,
        TElementVisitor&& ElementVisitor
    ) {
        for (int32 Z = MinZ; Z <= MaxZ; ++Z) {
            Container.ForEachElementInCell(Container.PackCellKey(X, Y, Z), ElementVisitor);
        }
    }
};
