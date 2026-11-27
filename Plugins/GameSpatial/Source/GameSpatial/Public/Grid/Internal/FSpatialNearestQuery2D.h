/**
 * @file FSpatialNearestQuery2D.h
 * @brief 2D 空间近邻与 K 近邻自适应向外壳层扩展算法（中心格快路径早退 + 零浮点位解包稀疏回退）
 */

#pragma once

#include "CoreMinimal.h"
#include "Math/Vector2D.h"
#include "Core/SpatialQueryCommon.h"
#include "Grid/Internal/TSpatialHashContainerBase.h"
#include "Grid/Internal/TSpatialKNearestBuffer.h"

namespace Game::Spatial::Detail {

    /**
     * @brief 2D 空间近邻与 K 近邻查询纯静态内部算法工具类
     */
    struct FSpatialNearestQuery2D {
    private:
        struct FRingSearchContext2D {
            FVector2D Center;
            double CellSize = 0.0;
            double& InOutRadiusSq;
            bool& bRingsHadElements;
        };

    public:
        /**
         * @brief 查找距离给定二维中心点最近的目标元素（中心格快路径早退 + 切比雪夫自适应向外壳层扩展）
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
        [[nodiscard]] static bool QueryNearest(
            const TSpatialHashContainer2D<TElement>& Container,
            const FVector2D& Center,
            float MaxSearchRadius,
            TElement& OutNearest,
            TPredicate&& Predicate = {}
        ) {
            if (!IsValidNearestInput(Container, Center, MaxSearchRadius)) {
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

            ExpandNearestSearch(Container, Center, ClosestDistSq, ConsiderElement);
            if (BestCandidate == nullptr) {
                return false;
            }
            OutNearest = *BestCandidate;
            return true;
        }

        /**
         * @brief 流式查找距离给定二维中心点最近的 K 个目标元素（K <= 16 零堆分配，按距离升序消费）
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
            if (K <= 0 || !IsValidNearestInput(Container, Center, MaxSearchRadius)) {
                return;
            }

            TSpatialKNearestBuffer<TElement, 16> Buffer(K);
            FillKNearestBuffer(Container, Center, MaxSearchRadius, K, Buffer, Forward<TPredicate>(Predicate));
            Buffer.Emit(Forward<TConsumer>(Consumer));
        }

        /**
         * @brief 查找距离给定二维中心点最近的 K 个目标元素（按距离升序输出，按实际命中数精准预留输出数组）
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
            if (K <= 0 || !IsValidNearestInput(Container, Center, MaxSearchRadius)) {
                return;
            }

            TSpatialKNearestBuffer<TElement, 16> Buffer(K);
            FillKNearestBuffer(Container, Center, MaxSearchRadius, K, Buffer, Forward<TPredicate>(Predicate));
            OutNearestList.Reserve(Buffer.Num());
            Buffer.Emit([&OutNearestList](const TElement& Item) { OutNearestList.Add(Item); });
        }

    private:
        template <typename TElement>
        [[nodiscard]] static FORCEINLINE bool IsValidNearestInput(
            const TSpatialHashContainer2D<TElement>& Container,
            const FVector2D& Center,
            float MaxSearchRadius
        ) noexcept {
            return !Container.IsEmpty()
                && Game::Spatial::Private::IsFiniteVector(Center)
                && Game::Spatial::Private::IsFiniteCoord(MaxSearchRadius)
                && MaxSearchRadius > 0.0f;
        }

        [[nodiscard]] static FORCEINLINE bool IsContainedStrictlyInCenterCell(
            const FVector2D& Center,
            int32 CenterCellX,
            int32 CenterCellY,
            double CellSize,
            double RadiusSq
        ) noexcept {
            if (RadiusSq <= 0.0) {
                return true;
            }
            constexpr int32 MinCoord = FSpatialCoordPolicy2D::MIN_CELL_COORD;
            constexpr int32 MaxCoord = FSpatialCoordPolicy2D::MAX_CELL_COORD;
            if (CenterCellX <= MinCoord || CenterCellX >= MaxCoord || CenterCellY <= MinCoord || CenterCellY >= MaxCoord) {
                return false;
            }
            const double MinX = static_cast<double>(CenterCellX) * CellSize;
            const double MinY = static_cast<double>(CenterCellY) * CellSize;
            const double EdgeX = FMath::Min(Center.X - MinX, (MinX + CellSize) - Center.X);
            const double EdgeY = FMath::Min(Center.Y - MinY, (MinY + CellSize) - Center.Y);
            const double SafeEdge = FMath::Min(EdgeX, EdgeY) - UE_DOUBLE_KINDA_SMALL_NUMBER;
            return SafeEdge > 0.0 && RadiusSq < (SafeEdge * SafeEdge);
        }

        [[nodiscard]] static FORCEINLINE int64 ComputeRingCellCount(
            int32 CenterCellX,
            int32 CenterCellY,
            int32 Ring,
            const FCellBounds2D& Bounds
        ) noexcept {
            const int64 LoX = FMath::Max(static_cast<int64>(Bounds.MinX), static_cast<int64>(CenterCellX) - Ring);
            const int64 HiX = FMath::Min(static_cast<int64>(Bounds.MaxX), static_cast<int64>(CenterCellX) + Ring);
            const int64 LoY = FMath::Max(static_cast<int64>(Bounds.MinY), static_cast<int64>(CenterCellY) - Ring);
            const int64 HiY = FMath::Min(static_cast<int64>(Bounds.MaxY), static_cast<int64>(CenterCellY) + Ring);
            const int64 OuterCells = (HiX - LoX + 1LL) * (HiY - LoY + 1LL);

            const int64 InnerLoX = FMath::Max(static_cast<int64>(Bounds.MinX), static_cast<int64>(CenterCellX) - Ring + 1LL);
            const int64 InnerHiX = FMath::Min(static_cast<int64>(Bounds.MaxX), static_cast<int64>(CenterCellX) + Ring - 1LL);
            const int64 InnerLoY = FMath::Max(static_cast<int64>(Bounds.MinY), static_cast<int64>(CenterCellY) - Ring + 1LL);
            const int64 InnerHiY = FMath::Min(static_cast<int64>(Bounds.MaxY), static_cast<int64>(CenterCellY) + Ring - 1LL);
            const int64 InnerCells = (InnerLoX <= InnerHiX && InnerLoY <= InnerHiY)
                ? ((InnerHiX - InnerLoX + 1LL) * (InnerHiY - InnerLoY + 1LL))
                : 0LL;
            return OuterCells - InnerCells;
        }

        [[nodiscard]] static FORCEINLINE double ComputeRingMinDist(
            const FVector2D& Center,
            double CellSize,
            int64 NegX,
            int64 PosX,
            int64 NegY,
            int64 PosY,
            const FCellBounds2D& Bounds
        ) noexcept {
            constexpr int32 MinCoord = FSpatialCoordPolicy2D::MIN_CELL_COORD;
            constexpr int32 MaxCoord = FSpatialCoordPolicy2D::MAX_CELL_COORD;
            double MinDist = TNumericLimits<double>::Max();
            if (NegX >= Bounds.MinX) {
                MinDist = FMath::Min(MinDist, Game::Spatial::Private::Get1DMinDistanceToCell(Center.X, static_cast<int32>(NegX), CellSize, MinCoord, MaxCoord));
            }
            if (PosX <= Bounds.MaxX) {
                MinDist = FMath::Min(MinDist, Game::Spatial::Private::Get1DMinDistanceToCell(Center.X, static_cast<int32>(PosX), CellSize, MinCoord, MaxCoord));
            }
            if (NegY >= Bounds.MinY) {
                MinDist = FMath::Min(MinDist, Game::Spatial::Private::Get1DMinDistanceToCell(Center.Y, static_cast<int32>(NegY), CellSize, MinCoord, MaxCoord));
            }
            if (PosY <= Bounds.MaxY) {
                MinDist = FMath::Min(MinDist, Game::Spatial::Private::Get1DMinDistanceToCell(Center.Y, static_cast<int32>(PosY), CellSize, MinCoord, MaxCoord));
            }
            return MinDist;
        }

        template <typename TElement, typename TPredicate>
        static FORCEINLINE void FillKNearestBuffer(
            const TSpatialHashContainer2D<TElement>& Container,
            const FVector2D& Center,
            float MaxSearchRadius,
            int32 K,
            TSpatialKNearestBuffer<TElement, 16>& Buffer,
            TPredicate&& Predicate
        ) {
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
            ExpandNearestSearch(Container, Center, MaxValidDistSq, ConsiderElement);
        }

        template <typename TElement, typename TConsider>
        static FORCEINLINE void ExpandNearestSearch(
            const TSpatialHashContainer2D<TElement>& Container,
            const FVector2D& Center,
            double& InOutRadiusSq,
            TConsider&& ConsiderElement
        ) {
            const int32 CenterCellX = Container.ComputeCellCoord(Center.X);
            const int32 CenterCellY = Container.ComputeCellCoord(Center.Y);
            const uint64 CenterCellKey = FSpatialCoordPolicy2D::PackCellKey(CenterCellX, CenterCellY);
            const bool bCenterHadElements = (Container.ForEachElementInCell(CenterCellKey, ConsiderElement) > 0);

            if (IsContainedStrictlyInCenterCell(Center, CenterCellX, CenterCellY, static_cast<double>(Container.GetCellSize()), InOutRadiusSq)) {
                return;
            }

            ExpandNearestOutward(
                Container,
                Center,
                FIntPoint(CenterCellX, CenterCellY),
                CenterCellKey,
                bCenterHadElements,
                InOutRadiusSq,
                ConsiderElement
            );
        }

        template <typename TElement, typename TConsider>
        static void ScanRemainingSparse(
            const TSpatialHashContainer2D<TElement>& Container,
            uint64 CenterCellKey,
            const FCellBounds2D& DoneBounds,
            bool bCenterHadElements,
            bool bRingsHadElements,
            TConsider&& ConsiderElement
        ) {
            if (!bCenterHadElements && !bRingsHadElements) {
                Container.ForEachElement(ConsiderElement);
                return;
            }
            if (!bRingsHadElements) {
                Container.ForEachElementExcludingCell(CenterCellKey, ConsiderElement);
                return;
            }

            Container.ForEachElementWithCellKey([&DoneBounds, &ConsiderElement](
                const TElement& Item,
                const FVector2D& Loc,
                uint64 CellKey
            ) {
                int32 CellX = 0;
                int32 CellY = 0;
                FSpatialCoordPolicy2D::UnpackCellCoord(CellKey, CellX, CellY);
                if (!DoneBounds.Contains(CellX, CellY)) {
                    ConsiderElement(Item, Loc);
                }
            });
        }

        template <typename TElement, typename TConsider>
        static void ExpandNearestOutward(
            const TSpatialHashContainer2D<TElement>& Container,
            const FVector2D& Center,
            FIntPoint CenterCell,
            uint64 CenterCellKey,
            bool bCenterHadElements,
            double& InOutRadiusSq,
            TConsider&& ConsiderElement
        ) {
            const FCellBounds2D Bounds = FSpatialCoordPolicy2D::ComputeRadiusBounds(Center, FMath::Sqrt(InOutRadiusSq), Container.GetInvCellSize());
            if (Bounds.IsSingleCell(CenterCell.X, CenterCell.Y)) {
                return;
            }

            const double CellSize = static_cast<double>(Container.GetCellSize());
            const int64 MaxRingX = FMath::Max(static_cast<int64>(CenterCell.X) - Bounds.MinX, static_cast<int64>(Bounds.MaxX) - CenterCell.X);
            const int64 MaxRingY = FMath::Max(static_cast<int64>(CenterCell.Y) - Bounds.MinY, static_cast<int64>(Bounds.MaxY) - CenterCell.Y);
            const int64 MaxRing = FMath::Max(MaxRingX, MaxRingY);
            const int64 SparseThreshold = static_cast<int64>(Container.Num());
            constexpr int64 CellProbeCostWeight = 2LL;
            int64 CumulativeCells = 1LL;
            bool bRingsHadElements = false;
            const FRingSearchContext2D Ctx{Center, CellSize, InOutRadiusSq, bRingsHadElements};

            for (int32 Ring = 1; static_cast<int64>(Ring) <= MaxRing; ++Ring) {
                const int64 NegX = static_cast<int64>(CenterCell.X) - Ring;
                const int64 PosX = static_cast<int64>(CenterCell.X) + Ring;
                const int64 NegY = static_cast<int64>(CenterCell.Y) - Ring;
                const int64 PosY = static_cast<int64>(CenterCell.Y) + Ring;

                const double MinFaceDist = ComputeRingMinDist(Center, CellSize, NegX, PosX, NegY, PosY, Bounds);
                if (MinFaceDist * MinFaceDist > InOutRadiusSq) {
                    return;
                }

                const int64 RingCells = ComputeRingCellCount(CenterCell.X, CenterCell.Y, Ring, Bounds);
                if ((CumulativeCells + RingCells) * CellProbeCostWeight > SparseThreshold) {
                    const int32 DoneRing = Ring - 1;
                    const FCellBounds2D DoneBounds{
                        static_cast<int32>(FMath::Max(static_cast<int64>(Bounds.MinX), static_cast<int64>(CenterCell.X) - DoneRing)),
                        static_cast<int32>(FMath::Min(static_cast<int64>(Bounds.MaxX), static_cast<int64>(CenterCell.X) + DoneRing)),
                        static_cast<int32>(FMath::Max(static_cast<int64>(Bounds.MinY), static_cast<int64>(CenterCell.Y) - DoneRing)),
                        static_cast<int32>(FMath::Min(static_cast<int64>(Bounds.MaxY), static_cast<int64>(CenterCell.Y) + DoneRing))
                    };
                    ScanRemainingSparse(Container, CenterCellKey, DoneBounds, bCenterHadElements, bRingsHadElements, ConsiderElement);
                    return;
                }
                CumulativeCells += RingCells;

                const int32 LoY = static_cast<int32>(FMath::Max(static_cast<int64>(Bounds.MinY), NegY));
                const int32 HiY = static_cast<int32>(FMath::Min(static_cast<int64>(Bounds.MaxY), PosY));
                if (NegX >= Bounds.MinX) {
                    VisitRingColumnX(Container, static_cast<int32>(NegX), LoY, HiY, Ctx, ConsiderElement);
                }
                if (PosX <= Bounds.MaxX) {
                    VisitRingColumnX(Container, static_cast<int32>(PosX), LoY, HiY, Ctx, ConsiderElement);
                }

                const int64 InnerLoX = FMath::Max(static_cast<int64>(Bounds.MinX), NegX + 1LL);
                const int64 InnerHiX = FMath::Min(static_cast<int64>(Bounds.MaxX), PosX - 1LL);
                if (InnerLoX > InnerHiX) {
                    continue;
                }
                if (NegY >= Bounds.MinY) {
                    VisitRingRowY(Container, static_cast<int32>(NegY), static_cast<int32>(InnerLoX), static_cast<int32>(InnerHiX), Ctx, ConsiderElement);
                }
                if (PosY <= Bounds.MaxY) {
                    VisitRingRowY(Container, static_cast<int32>(PosY), static_cast<int32>(InnerLoX), static_cast<int32>(InnerHiX), Ctx, ConsiderElement);
                }
            }
        }

        template <typename TElement, typename TConsider>
        static void VisitRingColumnX(
            const TSpatialHashContainer2D<TElement>& Container,
            int32 X,
            int32 LoY,
            int32 HiY,
            const FRingSearchContext2D& Ctx,
            TConsider&& ConsiderElement
        ) {
            constexpr int32 MinCoord = FSpatialCoordPolicy2D::MIN_CELL_COORD;
            constexpr int32 MaxCoord = FSpatialCoordPolicy2D::MAX_CELL_COORD;
            const double Dx = Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.X, X, Ctx.CellSize, MinCoord, MaxCoord);
            const double DxSq = Dx * Dx;
            if (DxSq > Ctx.InOutRadiusSq) {
                return;
            }
            for (int32 Y = LoY; Y <= HiY; ++Y) {
                const double Dy = Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.Y, Y, Ctx.CellSize, MinCoord, MaxCoord);
                if (DxSq + Dy * Dy <= Ctx.InOutRadiusSq
                    && Container.ForEachElementInCell(FSpatialCoordPolicy2D::PackCellKey(X, Y), ConsiderElement) > 0) {
                    Ctx.bRingsHadElements = true;
                }
            }
        }

        template <typename TElement, typename TConsider>
        static void VisitRingRowY(
            const TSpatialHashContainer2D<TElement>& Container,
            int32 Y,
            int32 LoX,
            int32 HiX,
            const FRingSearchContext2D& Ctx,
            TConsider&& ConsiderElement
        ) {
            constexpr int32 MinCoord = FSpatialCoordPolicy2D::MIN_CELL_COORD;
            constexpr int32 MaxCoord = FSpatialCoordPolicy2D::MAX_CELL_COORD;
            const double Dy = Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.Y, Y, Ctx.CellSize, MinCoord, MaxCoord);
            const double DySq = Dy * Dy;
            if (DySq > Ctx.InOutRadiusSq) {
                return;
            }
            for (int32 X = LoX; X <= HiX; ++X) {
                const double Dx = Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.X, X, Ctx.CellSize, MinCoord, MaxCoord);
                if (Dx * Dx + DySq <= Ctx.InOutRadiusSq
                    && Container.ForEachElementInCell(FSpatialCoordPolicy2D::PackCellKey(X, Y), ConsiderElement) > 0) {
                    Ctx.bRingsHadElements = true;
                }
            }
        }
    };

}  // namespace Game::Spatial::Detail
