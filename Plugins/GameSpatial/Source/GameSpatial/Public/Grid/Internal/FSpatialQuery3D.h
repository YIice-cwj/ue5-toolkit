/**
 * @file FSpatialQuery3D.h
 * @brief 3D 空间立体几何范围求交算法（球体、AABB、圆柱体、紧致三维锥体，由 TSpatialHashGrid3D 门面统一暴露）
 */

#pragma once

#include "CoreMinimal.h"
#include "Math/Box.h"
#include "Math/Vector.h"
#include "Core/SpatialQueryCommon.h"
#include "Grid/Internal/TSpatialHashContainerBase.h"

namespace Game::Spatial::Detail {

    /**
     * @brief 3D 空间几何范围查询纯静态内部算法工具类
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
            if (Container.IsEmpty()
                || !Game::Spatial::Private::IsFiniteVector(Center)
                || !Game::Spatial::Private::IsFiniteCoord(Radius)
                || Radius <= 0.0f) {
                return;
            }

            const double RadiusD = static_cast<double>(Radius);
            const double RadiusSq = RadiusD * RadiusD;
            const FCellBounds3D Bounds = FSpatialCoordPolicy3D::ComputeRadiusBounds(Center, RadiusD, Container.GetInvCellSize());

            auto CheckAndConsume = [&Center, RadiusSq, &Consumer, &Predicate](const TElement& Item, const FVector& Loc) {
                if (FVector::DistSquared(Center, Loc) <= RadiusSq) {
                    Game::Spatial::Private::TryConsumeElement(Consumer, Predicate, Item, Loc);
                }
            };

            if (ShouldUseSparseScan(Bounds, Container.Num())) {
                Container.ForEachElement(CheckAndConsume);
                return;
            }

            ForEachCellInRadiusBounds(Container, Center, Bounds, RadiusSq, CheckAndConsume);
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
            if (Container.IsEmpty()
                || !Box.IsValid
                || !Game::Spatial::Private::IsFiniteVector(Box.Min)
                || !Game::Spatial::Private::IsFiniteVector(Box.Max)
                || Box.Min.X > Box.Max.X
                || Box.Min.Y > Box.Max.Y
                || Box.Min.Z > Box.Max.Z) {
                return;
            }

            const FCellBounds3D Bounds{
                Container.ComputeCellCoord(Box.Min.X),
                Container.ComputeCellCoord(Box.Max.X),
                Container.ComputeCellCoord(Box.Min.Y),
                Container.ComputeCellCoord(Box.Max.Y),
                Container.ComputeCellCoord(Box.Min.Z),
                Container.ComputeCellCoord(Box.Max.Z)
            };

            auto CheckAndConsume = [&Box, &Consumer, &Predicate](const TElement& Item, const FVector& Loc) {
                if (Loc.X >= Box.Min.X && Loc.X <= Box.Max.X
                    && Loc.Y >= Box.Min.Y && Loc.Y <= Box.Max.Y
                    && Loc.Z >= Box.Min.Z && Loc.Z <= Box.Max.Z) {
                    Game::Spatial::Private::TryConsumeElement(Consumer, Predicate, Item, Loc);
                }
            };

            if (ShouldUseSparseScan(Bounds, Container.Num())) {
                Container.ForEachElement(CheckAndConsume);
                return;
            }

            for (int32 X = Bounds.MinX; X <= Bounds.MaxX; ++X) {
                for (int32 Y = Bounds.MinY; Y <= Bounds.MaxY; ++Y) {
                    VisitZColumn(Container, X, Y, Bounds.MinZ, Bounds.MaxZ, CheckAndConsume);
                }
            }
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
            if (Container.IsEmpty()
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

            const FCellBounds3D Bounds{
                Container.ComputeCellCoord(Center.X - RadiusD),
                Container.ComputeCellCoord(Center.X + RadiusD),
                Container.ComputeCellCoord(Center.Y - RadiusD),
                Container.ComputeCellCoord(Center.Y + RadiusD),
                Container.ComputeCellCoord(Center.Z - HalfHeightD),
                Container.ComputeCellCoord(Center.Z + HalfHeightD)
            };

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

            if (ShouldUseSparseScan(Bounds, Container.Num())) {
                Container.ForEachElement(CheckAndConsume);
                return;
            }

            const double CellSize = static_cast<double>(Container.GetCellSize());
            constexpr int32 MinCoord = FSpatialCoordPolicy3D::MIN_CELL_COORD;
            constexpr int32 MaxCoord = FSpatialCoordPolicy3D::MAX_CELL_COORD;
            for (int32 X = Bounds.MinX; X <= Bounds.MaxX; ++X) {
                const double Dx = Game::Spatial::Private::Get1DMinDistanceToCell(Center.X, X, CellSize, MinCoord, MaxCoord);
                const double DxSq = Dx * Dx;
                if (DxSq > RadiusSq) {
                    continue;
                }

                for (int32 Y = Bounds.MinY; Y <= Bounds.MaxY; ++Y) {
                    const double Dy = Game::Spatial::Private::Get1DMinDistanceToCell(Center.Y, Y, CellSize, MinCoord, MaxCoord);
                    if (DxSq + Dy * Dy <= RadiusSq) {
                        VisitZColumn(Container, X, Y, Bounds.MinZ, Bounds.MaxZ, CheckAndConsume);
                    }
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
            if (Container.IsEmpty()
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
            const double RadiusSq = RadiusD * RadiusD;
            const double HalfAngleRad = FMath::DegreesToRadians(static_cast<double>(HalfAngleDegrees));
            const double CosHalfAngle = FMath::Cos(HalfAngleRad);
            const double SinHalfAngle = FMath::Sin(HalfAngleRad);
            const double CosHalfAngleSq = CosHalfAngle * CosHalfAngle;
            const FVector SafeDirection = Direction.GetSafeNormal();

            const FCellBounds3D Bounds = ComputeConeBounds(Container, Apex, SafeDirection, CosHalfAngle, SinHalfAngle, RadiusD);

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

            if (ShouldUseSparseScan(Bounds, Container.Num())) {
                Container.ForEachElement(CheckAndConsume);
                return;
            }

            ForEachCellInRadiusBounds(Container, Apex, Bounds, RadiusSq, CheckAndConsume);
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

    private:
        [[nodiscard]] static FORCEINLINE bool ShouldUseSparseScan(const FCellBounds3D& Bounds, int32 ElementCount) noexcept {
            return Game::Spatial::Private::ShouldUseSparseScan3D(
                Bounds.MinX,
                Bounds.MaxX,
                Bounds.MinY,
                Bounds.MaxY,
                Bounds.MinZ,
                Bounds.MaxZ,
                ElementCount
            );
        }

        template <typename TElement>
        [[nodiscard]] static FORCEINLINE FCellBounds3D ComputeConeBounds(
            const TSpatialHashContainer3D<TElement>& Container,
            const FVector& Apex,
            const FVector& SafeDirection,
            double CosHalfAngle,
            double SinHalfAngle,
            double Radius
        ) noexcept {
            double MinOffsetX = 0.0;
            double MaxOffsetX = 0.0;
            double MinOffsetY = 0.0;
            double MaxOffsetY = 0.0;
            double MinOffsetZ = 0.0;
            double MaxOffsetZ = 0.0;
            Game::Spatial::Private::ComputeAxisConeExtent(SafeDirection.X, CosHalfAngle, SinHalfAngle, Radius, MinOffsetX, MaxOffsetX);
            Game::Spatial::Private::ComputeAxisConeExtent(SafeDirection.Y, CosHalfAngle, SinHalfAngle, Radius, MinOffsetY, MaxOffsetY);
            Game::Spatial::Private::ComputeAxisConeExtent(SafeDirection.Z, CosHalfAngle, SinHalfAngle, Radius, MinOffsetZ, MaxOffsetZ);

            return FCellBounds3D{
                Container.ComputeCellCoord(Apex.X + MinOffsetX - UE_DOUBLE_KINDA_SMALL_NUMBER),
                Container.ComputeCellCoord(Apex.X + MaxOffsetX + UE_DOUBLE_KINDA_SMALL_NUMBER),
                Container.ComputeCellCoord(Apex.Y + MinOffsetY - UE_DOUBLE_KINDA_SMALL_NUMBER),
                Container.ComputeCellCoord(Apex.Y + MaxOffsetY + UE_DOUBLE_KINDA_SMALL_NUMBER),
                Container.ComputeCellCoord(Apex.Z + MinOffsetZ - UE_DOUBLE_KINDA_SMALL_NUMBER),
                Container.ComputeCellCoord(Apex.Z + MaxOffsetZ + UE_DOUBLE_KINDA_SMALL_NUMBER)
            };
        }

        template <typename TElement, typename TElementVisitor>
        static FORCEINLINE void VisitZColumn(
            const TSpatialHashContainer3D<TElement>& Container,
            int32 X,
            int32 Y,
            int32 MinZ,
            int32 MaxZ,
            TElementVisitor&& ElementVisitor
        ) {
            for (int32 Z = MinZ; Z <= MaxZ; ++Z) {
                Container.ForEachElementInCell(FSpatialCoordPolicy3D::PackCellKey(X, Y, Z), ElementVisitor);
            }
        }

        template <typename TElement, typename TElementVisitor>
        static FORCEINLINE void VisitZColumnInRadius(
            const TSpatialHashContainer3D<TElement>& Container,
            FIntPoint XyCell,
            FIntPoint ZSpan,
            double CenterZ,
            double DxySq,
            double RadiusSq,
            TElementVisitor&& ElementVisitor
        ) {
            const double CellSize = static_cast<double>(Container.GetCellSize());
            constexpr int32 MinCoord = FSpatialCoordPolicy3D::MIN_CELL_COORD;
            constexpr int32 MaxCoord = FSpatialCoordPolicy3D::MAX_CELL_COORD;
            for (int32 Z = ZSpan.X; Z <= ZSpan.Y; ++Z) {
                const double Dz = Game::Spatial::Private::Get1DMinDistanceToCell(CenterZ, Z, CellSize, MinCoord, MaxCoord);
                if (DxySq + Dz * Dz <= RadiusSq) {
                    Container.ForEachElementInCell(FSpatialCoordPolicy3D::PackCellKey(XyCell.X, XyCell.Y, Z), ElementVisitor);
                }
            }
        }

        template <typename TElement, typename TElementVisitor>
        static void ForEachCellInRadiusBounds(
            const TSpatialHashContainer3D<TElement>& Container,
            const FVector& Center,
            const FCellBounds3D& Bounds,
            double RadiusSq,
            TElementVisitor&& ElementVisitor
        ) {
            const double CellSize = static_cast<double>(Container.GetCellSize());
            constexpr int32 MinCoord = FSpatialCoordPolicy3D::MIN_CELL_COORD;
            constexpr int32 MaxCoord = FSpatialCoordPolicy3D::MAX_CELL_COORD;
            const FIntPoint ZSpan(Bounds.MinZ, Bounds.MaxZ);

            for (int32 X = Bounds.MinX; X <= Bounds.MaxX; ++X) {
                const double Dx = Game::Spatial::Private::Get1DMinDistanceToCell(Center.X, X, CellSize, MinCoord, MaxCoord);
                const double DxSq = Dx * Dx;
                if (DxSq > RadiusSq) {
                    continue;
                }

                for (int32 Y = Bounds.MinY; Y <= Bounds.MaxY; ++Y) {
                    const double Dy = Game::Spatial::Private::Get1DMinDistanceToCell(Center.Y, Y, CellSize, MinCoord, MaxCoord);
                    const double DxySq = DxSq + Dy * Dy;
                    if (DxySq <= RadiusSq) {
                        VisitZColumnInRadius(Container, FIntPoint(X, Y), ZSpan, Center.Z, DxySq, RadiusSq, ElementVisitor);
                    }
                }
            }
        }
    };

}  // namespace Game::Spatial::Detail
