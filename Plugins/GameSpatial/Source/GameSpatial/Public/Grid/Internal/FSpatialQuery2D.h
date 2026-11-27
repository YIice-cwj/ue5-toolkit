/**
 * @file FSpatialQuery2D.h
 * @brief 2D 空间几何范围求交算法（圆形、AABB、紧致扇形，由 TSpatialHashGrid2D 门面统一暴露）
 */

#pragma once

#include "CoreMinimal.h"
#include "Math/Box2D.h"
#include "Math/Vector2D.h"
#include "Core/SpatialQueryCommon.h"
#include "Grid/Internal/TSpatialHashContainerBase.h"

namespace Game::Spatial::Detail {

    /**
     * @brief 2D 空间几何范围查询纯静态内部算法工具类
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
            if (Container.IsEmpty()
                || !Game::Spatial::Private::IsFiniteVector(Center)
                || !Game::Spatial::Private::IsFiniteCoord(Radius)
                || Radius <= 0.0f) {
                return;
            }

            const double RadiusD = static_cast<double>(Radius);
            const double RadiusSq = RadiusD * RadiusD;
            const FCellBounds2D Bounds = FSpatialCoordPolicy2D::ComputeRadiusBounds(Center, RadiusD, Container.GetInvCellSize());

            auto CheckAndConsume = [&Center, RadiusSq, &Consumer, &Predicate](const TElement& Item, const FVector2D& Loc) {
                if (FVector2D::DistSquared(Center, Loc) <= RadiusSq) {
                    Game::Spatial::Private::TryConsumeElement(Consumer, Predicate, Item, Loc);
                }
            };

            if (Game::Spatial::Private::ShouldUseSparseScan2D(Bounds.MinX, Bounds.MaxX, Bounds.MinY, Bounds.MaxY, Container.Num())) {
                Container.ForEachElement(CheckAndConsume);
                return;
            }

            ForEachCellInRadiusBounds(Container, Center, Bounds, RadiusSq, CheckAndConsume);
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
            if (Container.IsEmpty()
                || !Box.bIsValid
                || !Game::Spatial::Private::IsFiniteVector(Box.Min)
                || !Game::Spatial::Private::IsFiniteVector(Box.Max)
                || Box.Min.X > Box.Max.X
                || Box.Min.Y > Box.Max.Y) {
                return;
            }

            const FCellBounds2D Bounds{
                Container.ComputeCellCoord(Box.Min.X),
                Container.ComputeCellCoord(Box.Max.X),
                Container.ComputeCellCoord(Box.Min.Y),
                Container.ComputeCellCoord(Box.Max.Y)
            };

            auto CheckAndConsume = [&Box, &Consumer, &Predicate](const TElement& Item, const FVector2D& Loc) {
                if (Loc.X >= Box.Min.X && Loc.X <= Box.Max.X && Loc.Y >= Box.Min.Y && Loc.Y <= Box.Max.Y) {
                    Game::Spatial::Private::TryConsumeElement(Consumer, Predicate, Item, Loc);
                }
            };

            if (Game::Spatial::Private::ShouldUseSparseScan2D(Bounds.MinX, Bounds.MaxX, Bounds.MinY, Bounds.MaxY, Container.Num())) {
                Container.ForEachElement(CheckAndConsume);
                return;
            }

            for (int32 X = Bounds.MinX; X <= Bounds.MaxX; ++X) {
                for (int32 Y = Bounds.MinY; Y <= Bounds.MaxY; ++Y) {
                    Container.ForEachElementInCell(FSpatialCoordPolicy2D::PackCellKey(X, Y), CheckAndConsume);
                }
            }
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
            if (Container.IsEmpty()
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
            const double RadiusSq = RadiusD * RadiusD;
            const double HalfAngleRad = FMath::DegreesToRadians(static_cast<double>(HalfAngleDegrees));
            const double CosHalfAngle = FMath::Cos(HalfAngleRad);
            const double SinHalfAngle = FMath::Sin(HalfAngleRad);
            const double CosHalfAngleSq = CosHalfAngle * CosHalfAngle;
            const FVector2D SafeForward = ForwardDir.GetSafeNormal();

            const FCellBounds2D Bounds = ComputeSectorBounds(Container, Center, SafeForward, CosHalfAngle, SinHalfAngle, RadiusD);

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

            if (Game::Spatial::Private::ShouldUseSparseScan2D(Bounds.MinX, Bounds.MaxX, Bounds.MinY, Bounds.MaxY, Container.Num())) {
                Container.ForEachElement(CheckAndConsume);
                return;
            }

            ForEachCellInRadiusBounds(Container, Center, Bounds, RadiusSq, CheckAndConsume);
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

    private:
        template <typename TElement>
        [[nodiscard]] static FORCEINLINE FCellBounds2D ComputeSectorBounds(
            const TSpatialHashContainer2D<TElement>& Container,
            const FVector2D& Center,
            const FVector2D& SafeForward,
            double CosHalfAngle,
            double SinHalfAngle,
            double Radius
        ) noexcept {
            double MinOffsetX = 0.0;
            double MaxOffsetX = 0.0;
            double MinOffsetY = 0.0;
            double MaxOffsetY = 0.0;
            Game::Spatial::Private::ComputeAxisConeExtent(SafeForward.X, CosHalfAngle, SinHalfAngle, Radius, MinOffsetX, MaxOffsetX);
            Game::Spatial::Private::ComputeAxisConeExtent(SafeForward.Y, CosHalfAngle, SinHalfAngle, Radius, MinOffsetY, MaxOffsetY);

            return FCellBounds2D{
                Container.ComputeCellCoord(Center.X + MinOffsetX - UE_DOUBLE_KINDA_SMALL_NUMBER),
                Container.ComputeCellCoord(Center.X + MaxOffsetX + UE_DOUBLE_KINDA_SMALL_NUMBER),
                Container.ComputeCellCoord(Center.Y + MinOffsetY - UE_DOUBLE_KINDA_SMALL_NUMBER),
                Container.ComputeCellCoord(Center.Y + MaxOffsetY + UE_DOUBLE_KINDA_SMALL_NUMBER)
            };
        }

        template <typename TElement, typename TElementVisitor>
        static void ForEachCellInRadiusBounds(
            const TSpatialHashContainer2D<TElement>& Container,
            const FVector2D& Center,
            const FCellBounds2D& Bounds,
            double RadiusSq,
            TElementVisitor&& ElementVisitor
        ) {
            const double CellSize = static_cast<double>(Container.GetCellSize());
            constexpr int32 MinCoord = FSpatialCoordPolicy2D::MIN_CELL_COORD;
            constexpr int32 MaxCoord = FSpatialCoordPolicy2D::MAX_CELL_COORD;

            for (int32 X = Bounds.MinX; X <= Bounds.MaxX; ++X) {
                const double Dx = Game::Spatial::Private::Get1DMinDistanceToCell(Center.X, X, CellSize, MinCoord, MaxCoord);
                const double DxSq = Dx * Dx;
                if (DxSq > RadiusSq) {
                    continue;
                }

                for (int32 Y = Bounds.MinY; Y <= Bounds.MaxY; ++Y) {
                    const double Dy = Game::Spatial::Private::Get1DMinDistanceToCell(Center.Y, Y, CellSize, MinCoord, MaxCoord);
                    if (DxSq + Dy * Dy <= RadiusSq) {
                        Container.ForEachElementInCell(FSpatialCoordPolicy2D::PackCellKey(X, Y), ElementVisitor);
                    }
                }
            }
        }
    };

}  // namespace Game::Spatial::Detail
