/**
 * @file FSpatialNearestQuery3D.h
 * @brief 3D 空间近邻与 K 近邻自适应向外壳层扩展算法（中心格快路径早退 + 零浮点位解包稀疏回退）
 */

#pragma once

#include "CoreMinimal.h"
#include "Math/Vector.h"
#include "Core/SpatialQueryCommon.h"
#include "Grid/Internal/TSpatialHashContainerBase.h"
#include "Grid/Internal/TSpatialKNearestBuffer.h"

namespace Game::Spatial::Detail {

    /**
     * @brief 3D 空间近邻与 K 近邻查询纯静态内部算法工具类
     */
    struct FSpatialNearestQuery3D {
    private:
        struct FShellSearchContext3D {
            FVector Center;
            double CellSize = 0.0;
            double& InOutRadiusSq;
            bool& bRingsHadElements;
        };

    public:
        /**
         * @brief 查找距离给定三维中心点最近的目标元素（中心格快路径早退 + 三维切比雪夫自适应向外壳层扩展）
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
        [[nodiscard]] static bool QueryNearest(
            const TSpatialHashContainer3D<TElement>& Container,
            const FVector& Center,
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

            ExpandNearestSearch(Container, Center, ClosestDistSq, ConsiderElement);
            if (BestCandidate == nullptr) {
                return false;
            }
            OutNearest = *BestCandidate;
            return true;
        }

        /**
         * @brief 流式查找距离给定三维中心点最近的 K 个目标元素（K <= 16 零堆分配，按距离升序消费）
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
            if (K <= 0 || !IsValidNearestInput(Container, Center, MaxSearchRadius)) {
                return;
            }

            TSpatialKNearestBuffer<TElement, 16> Buffer(K);
            FillKNearestBuffer(Container, Center, MaxSearchRadius, K, Buffer, Forward<TPredicate>(Predicate));
            Buffer.Emit(Forward<TConsumer>(Consumer));
        }

        /**
         * @brief 查找距离给定三维中心点最近的 K 个目标元素（按距离升序输出，按实际命中数精准预留输出数组）
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
            const TSpatialHashContainer3D<TElement>& Container,
            const FVector& Center,
            float MaxSearchRadius
        ) noexcept {
            return !Container.IsEmpty()
                && Game::Spatial::Private::IsFiniteVector(Center)
                && Game::Spatial::Private::IsFiniteCoord(MaxSearchRadius)
                && MaxSearchRadius > 0.0f;
        }

        [[nodiscard]] static FORCEINLINE bool IsContainedStrictlyInCenterCell(
            const FVector& Center,
            int32 CenterCellX,
            int32 CenterCellY,
            int32 CenterCellZ,
            double CellSize,
            double RadiusSq
        ) noexcept {
            if (RadiusSq <= 0.0) {
                return true;
            }
            constexpr int32 MinCoord = FSpatialCoordPolicy3D::MIN_CELL_COORD;
            constexpr int32 MaxCoord = FSpatialCoordPolicy3D::MAX_CELL_COORD;
            if (CenterCellX <= MinCoord || CenterCellX >= MaxCoord
                || CenterCellY <= MinCoord || CenterCellY >= MaxCoord
                || CenterCellZ <= MinCoord || CenterCellZ >= MaxCoord) {
                return false;
            }
            const double MinX = static_cast<double>(CenterCellX) * CellSize;
            const double MinY = static_cast<double>(CenterCellY) * CellSize;
            const double MinZ = static_cast<double>(CenterCellZ) * CellSize;
            const double EdgeX = FMath::Min(Center.X - MinX, (MinX + CellSize) - Center.X);
            const double EdgeY = FMath::Min(Center.Y - MinY, (MinY + CellSize) - Center.Y);
            const double EdgeZ = FMath::Min(Center.Z - MinZ, (MinZ + CellSize) - Center.Z);
            const double SafeEdge = FMath::Min3(EdgeX, EdgeY, EdgeZ) - UE_DOUBLE_KINDA_SMALL_NUMBER;
            return SafeEdge > 0.0 && RadiusSq < (SafeEdge * SafeEdge);
        }

        [[nodiscard]] static FORCEINLINE int64 ComputeShellCellCount(
            int32 CenterCellX,
            int32 CenterCellY,
            int32 CenterCellZ,
            int32 Ring,
            const FCellBounds3D& Bounds
        ) noexcept {
            const int64 LoX = FMath::Max(static_cast<int64>(Bounds.MinX), static_cast<int64>(CenterCellX) - Ring);
            const int64 HiX = FMath::Min(static_cast<int64>(Bounds.MaxX), static_cast<int64>(CenterCellX) + Ring);
            const int64 LoY = FMath::Max(static_cast<int64>(Bounds.MinY), static_cast<int64>(CenterCellY) - Ring);
            const int64 HiY = FMath::Min(static_cast<int64>(Bounds.MaxY), static_cast<int64>(CenterCellY) + Ring);
            const int64 LoZ = FMath::Max(static_cast<int64>(Bounds.MinZ), static_cast<int64>(CenterCellZ) - Ring);
            const int64 HiZ = FMath::Min(static_cast<int64>(Bounds.MaxZ), static_cast<int64>(CenterCellZ) + Ring);
            const int64 OuterCells = (HiX - LoX + 1LL) * (HiY - LoY + 1LL) * (HiZ - LoZ + 1LL);

            const int64 InnerLoX = FMath::Max(static_cast<int64>(Bounds.MinX), static_cast<int64>(CenterCellX) - Ring + 1LL);
            const int64 InnerHiX = FMath::Min(static_cast<int64>(Bounds.MaxX), static_cast<int64>(CenterCellX) + Ring - 1LL);
            const int64 InnerLoY = FMath::Max(static_cast<int64>(Bounds.MinY), static_cast<int64>(CenterCellY) - Ring + 1LL);
            const int64 InnerHiY = FMath::Min(static_cast<int64>(Bounds.MaxY), static_cast<int64>(CenterCellY) + Ring - 1LL);
            const int64 InnerLoZ = FMath::Max(static_cast<int64>(Bounds.MinZ), static_cast<int64>(CenterCellZ) - Ring + 1LL);
            const int64 InnerHiZ = FMath::Min(static_cast<int64>(Bounds.MaxZ), static_cast<int64>(CenterCellZ) + Ring - 1LL);
            const int64 InnerCells = (InnerLoX <= InnerHiX && InnerLoY <= InnerHiY && InnerLoZ <= InnerHiZ)
                ? ((InnerHiX - InnerLoX + 1LL) * (InnerHiY - InnerLoY + 1LL) * (InnerHiZ - InnerLoZ + 1LL))
                : 0LL;
            return OuterCells - InnerCells;
        }

        [[nodiscard]] static FORCEINLINE double ComputeShellMinDist(
            const FShellSearchContext3D& Ctx,
            const FCellBounds3D& ShellSpan,
            const FCellBounds3D& Bounds
        ) noexcept {
            constexpr int32 MinCoord = FSpatialCoordPolicy3D::MIN_CELL_COORD;
            constexpr int32 MaxCoord = FSpatialCoordPolicy3D::MAX_CELL_COORD;
            double MinDist = TNumericLimits<double>::Max();
            if (ShellSpan.MinX >= Bounds.MinX) {
                MinDist = FMath::Min(MinDist, Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.X, ShellSpan.MinX, Ctx.CellSize, MinCoord, MaxCoord));
            }
            if (ShellSpan.MaxX <= Bounds.MaxX) {
                MinDist = FMath::Min(MinDist, Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.X, ShellSpan.MaxX, Ctx.CellSize, MinCoord, MaxCoord));
            }
            if (ShellSpan.MinY >= Bounds.MinY) {
                MinDist = FMath::Min(MinDist, Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.Y, ShellSpan.MinY, Ctx.CellSize, MinCoord, MaxCoord));
            }
            if (ShellSpan.MaxY <= Bounds.MaxY) {
                MinDist = FMath::Min(MinDist, Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.Y, ShellSpan.MaxY, Ctx.CellSize, MinCoord, MaxCoord));
            }
            if (ShellSpan.MinZ >= Bounds.MinZ) {
                MinDist = FMath::Min(MinDist, Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.Z, ShellSpan.MinZ, Ctx.CellSize, MinCoord, MaxCoord));
            }
            if (ShellSpan.MaxZ <= Bounds.MaxZ) {
                MinDist = FMath::Min(MinDist, Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.Z, ShellSpan.MaxZ, Ctx.CellSize, MinCoord, MaxCoord));
            }
            return MinDist;
        }

        template <typename TElement, typename TPredicate>
        static FORCEINLINE void FillKNearestBuffer(
            const TSpatialHashContainer3D<TElement>& Container,
            const FVector& Center,
            float MaxSearchRadius,
            int32 K,
            TSpatialKNearestBuffer<TElement, 16>& Buffer,
            TPredicate&& Predicate
        ) {
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
            ExpandNearestSearch(Container, Center, MaxValidDistSq, ConsiderElement);
        }

        template <typename TElement, typename TConsider>
        static FORCEINLINE void ExpandNearestSearch(
            const TSpatialHashContainer3D<TElement>& Container,
            const FVector& Center,
            double& InOutRadiusSq,
            TConsider&& ConsiderElement
        ) {
            const int32 CenterCellX = Container.ComputeCellCoord(Center.X);
            const int32 CenterCellY = Container.ComputeCellCoord(Center.Y);
            const int32 CenterCellZ = Container.ComputeCellCoord(Center.Z);
            const uint64 CenterCellKey = FSpatialCoordPolicy3D::PackCellKey(CenterCellX, CenterCellY, CenterCellZ);
            const bool bCenterHadElements = (Container.ForEachElementInCell(CenterCellKey, ConsiderElement) > 0);

            if (IsContainedStrictlyInCenterCell(
                Center,
                CenterCellX,
                CenterCellY,
                CenterCellZ,
                static_cast<double>(Container.GetCellSize()),
                InOutRadiusSq
            )) {
                return;
            }

            ExpandNearestOutward(
                Container,
                Center,
                FIntVector(CenterCellX, CenterCellY, CenterCellZ),
                CenterCellKey,
                bCenterHadElements,
                InOutRadiusSq,
                ConsiderElement
            );
        }

        template <typename TElement, typename TConsider>
        static void ScanRemainingSparse(
            const TSpatialHashContainer3D<TElement>& Container,
            uint64 CenterCellKey,
            const FCellBounds3D& DoneBounds,
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
                const FVector& Loc,
                uint64 CellKey
            ) {
                int32 CellX = 0;
                int32 CellY = 0;
                int32 CellZ = 0;
                FSpatialCoordPolicy3D::UnpackCellCoord(CellKey, CellX, CellY, CellZ);
                if (!DoneBounds.Contains(CellX, CellY, CellZ)) {
                    ConsiderElement(Item, Loc);
                }
            });
        }

        template <typename TElement, typename TConsider>
        static void ExpandNearestOutward(
            const TSpatialHashContainer3D<TElement>& Container,
            const FVector& Center,
            const FIntVector& CenterCell,
            uint64 CenterCellKey,
            bool bCenterHadElements,
            double& InOutRadiusSq,
            TConsider&& ConsiderElement
        ) {
            const FCellBounds3D Bounds = FSpatialCoordPolicy3D::ComputeRadiusBounds(Center, FMath::Sqrt(InOutRadiusSq), Container.GetInvCellSize());
            if (Bounds.IsSingleCell(CenterCell.X, CenterCell.Y, CenterCell.Z)) {
                return;
            }

            const double CellSize = static_cast<double>(Container.GetCellSize());
            const int32 MaxRingX = FMath::Max(CenterCell.X - Bounds.MinX, Bounds.MaxX - CenterCell.X);
            const int32 MaxRingY = FMath::Max(CenterCell.Y - Bounds.MinY, Bounds.MaxY - CenterCell.Y);
            const int32 MaxRingZ = FMath::Max(CenterCell.Z - Bounds.MinZ, Bounds.MaxZ - CenterCell.Z);
            const int32 MaxRing = FMath::Max3(MaxRingX, MaxRingY, MaxRingZ);
            const int64 SparseThreshold = static_cast<int64>(Container.Num());
            constexpr int64 CellProbeCostWeight = 4LL;
            int64 CumulativeCells = 1LL;
            bool bRingsHadElements = false;
            const FShellSearchContext3D Ctx{Center, CellSize, InOutRadiusSq, bRingsHadElements};

            for (int32 Ring = 1; Ring <= MaxRing; ++Ring) {
                const FCellBounds3D ShellSpan{
                    CenterCell.X - Ring,
                    CenterCell.X + Ring,
                    CenterCell.Y - Ring,
                    CenterCell.Y + Ring,
                    CenterCell.Z - Ring,
                    CenterCell.Z + Ring
                };

                const double MinFaceDist = ComputeShellMinDist(Ctx, ShellSpan, Bounds);
                if (MinFaceDist * MinFaceDist > InOutRadiusSq) {
                    return;
                }

                const int64 ShellCells = ComputeShellCellCount(CenterCell.X, CenterCell.Y, CenterCell.Z, Ring, Bounds);
                if ((CumulativeCells + ShellCells) * CellProbeCostWeight > SparseThreshold) {
                    const int32 DoneRing = Ring - 1;
                    const FCellBounds3D DoneBounds{
                        FMath::Max(Bounds.MinX, CenterCell.X - DoneRing),
                        FMath::Min(Bounds.MaxX, CenterCell.X + DoneRing),
                        FMath::Max(Bounds.MinY, CenterCell.Y - DoneRing),
                        FMath::Min(Bounds.MaxY, CenterCell.Y + DoneRing),
                        FMath::Max(Bounds.MinZ, CenterCell.Z - DoneRing),
                        FMath::Min(Bounds.MaxZ, CenterCell.Z + DoneRing)
                    };
                    ScanRemainingSparse(Container, CenterCellKey, DoneBounds, bCenterHadElements, bRingsHadElements, ConsiderElement);
                    return;
                }
                CumulativeCells += ShellCells;

                VisitShell(Container, ShellSpan, Bounds, Ctx, ConsiderElement);
            }
        }

        template <typename TElement, typename TConsider>
        static void VisitShell(
            const TSpatialHashContainer3D<TElement>& Container,
            const FCellBounds3D& ShellSpan,
            const FCellBounds3D& Bounds,
            const FShellSearchContext3D& Ctx,
            TConsider&& ConsiderElement
        ) {
            const int32 LoY = FMath::Max(Bounds.MinY, ShellSpan.MinY);
            const int32 HiY = FMath::Min(Bounds.MaxY, ShellSpan.MaxY);
            const int32 LoZ = FMath::Max(Bounds.MinZ, ShellSpan.MinZ);
            const int32 HiZ = FMath::Min(Bounds.MaxZ, ShellSpan.MaxZ);
            if (ShellSpan.MinX >= Bounds.MinX) {
                VisitShellFaceX(Container, ShellSpan.MinX, FCellBounds2D{LoY, HiY, LoZ, HiZ}, Ctx, ConsiderElement);
            }
            if (ShellSpan.MaxX <= Bounds.MaxX) {
                VisitShellFaceX(Container, ShellSpan.MaxX, FCellBounds2D{LoY, HiY, LoZ, HiZ}, Ctx, ConsiderElement);
            }

            const int32 InnerLoX = FMath::Max(Bounds.MinX, ShellSpan.MinX + 1);
            const int32 InnerHiX = FMath::Min(Bounds.MaxX, ShellSpan.MaxX - 1);
            if (InnerLoX > InnerHiX) {
                return;
            }
            if (ShellSpan.MinY >= Bounds.MinY) {
                VisitShellFaceY(Container, ShellSpan.MinY, FCellBounds2D{InnerLoX, InnerHiX, LoZ, HiZ}, Ctx, ConsiderElement);
            }
            if (ShellSpan.MaxY <= Bounds.MaxY) {
                VisitShellFaceY(Container, ShellSpan.MaxY, FCellBounds2D{InnerLoX, InnerHiX, LoZ, HiZ}, Ctx, ConsiderElement);
            }

            const int32 InnerLoY = FMath::Max(Bounds.MinY, ShellSpan.MinY + 1);
            const int32 InnerHiY = FMath::Min(Bounds.MaxY, ShellSpan.MaxY - 1);
            if (InnerLoY > InnerHiY) {
                return;
            }
            if (ShellSpan.MinZ >= Bounds.MinZ) {
                VisitShellFaceZ(Container, ShellSpan.MinZ, FCellBounds2D{InnerLoX, InnerHiX, InnerLoY, InnerHiY}, Ctx, ConsiderElement);
            }
            if (ShellSpan.MaxZ <= Bounds.MaxZ) {
                VisitShellFaceZ(Container, ShellSpan.MaxZ, FCellBounds2D{InnerLoX, InnerHiX, InnerLoY, InnerHiY}, Ctx, ConsiderElement);
            }
        }

        template <typename TElement, typename TConsider>
        static FORCEINLINE void VisitShellZRange(
            const TSpatialHashContainer3D<TElement>& Container,
            int32 X,
            int32 Y,
            FIntPoint ZSpan,
            double DxySq,
            const FShellSearchContext3D& Ctx,
            TConsider&& ConsiderElement
        ) {
            constexpr int32 MinCoord = FSpatialCoordPolicy3D::MIN_CELL_COORD;
            constexpr int32 MaxCoord = FSpatialCoordPolicy3D::MAX_CELL_COORD;
            for (int32 Z = ZSpan.X; Z <= ZSpan.Y; ++Z) {
                const double Dz = Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.Z, Z, Ctx.CellSize, MinCoord, MaxCoord);
                if (DxySq + Dz * Dz <= Ctx.InOutRadiusSq
                    && Container.ForEachElementInCell(FSpatialCoordPolicy3D::PackCellKey(X, Y, Z), ConsiderElement) > 0) {
                    Ctx.bRingsHadElements = true;
                }
            }
        }

        template <typename TElement, typename TConsider>
        static void VisitShellFaceX(
            const TSpatialHashContainer3D<TElement>& Container,
            int32 X,
            const FCellBounds2D& YzSpan,
            const FShellSearchContext3D& Ctx,
            TConsider&& ConsiderElement
        ) {
            constexpr int32 MinCoord = FSpatialCoordPolicy3D::MIN_CELL_COORD;
            constexpr int32 MaxCoord = FSpatialCoordPolicy3D::MAX_CELL_COORD;
            const double Dx = Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.X, X, Ctx.CellSize, MinCoord, MaxCoord);
            const double DxSq = Dx * Dx;
            if (DxSq > Ctx.InOutRadiusSq) {
                return;
            }
            const FIntPoint ZSpan(YzSpan.MinY, YzSpan.MaxY);
            for (int32 Y = YzSpan.MinX; Y <= YzSpan.MaxX; ++Y) {
                const double Dy = Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.Y, Y, Ctx.CellSize, MinCoord, MaxCoord);
                const double DxySq = DxSq + Dy * Dy;
                if (DxySq <= Ctx.InOutRadiusSq) {
                    VisitShellZRange(Container, X, Y, ZSpan, DxySq, Ctx, ConsiderElement);
                }
            }
        }

        template <typename TElement, typename TConsider>
        static void VisitShellFaceY(
            const TSpatialHashContainer3D<TElement>& Container,
            int32 Y,
            const FCellBounds2D& XzSpan,
            const FShellSearchContext3D& Ctx,
            TConsider&& ConsiderElement
        ) {
            constexpr int32 MinCoord = FSpatialCoordPolicy3D::MIN_CELL_COORD;
            constexpr int32 MaxCoord = FSpatialCoordPolicy3D::MAX_CELL_COORD;
            const double Dy = Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.Y, Y, Ctx.CellSize, MinCoord, MaxCoord);
            const double DySq = Dy * Dy;
            if (DySq > Ctx.InOutRadiusSq) {
                return;
            }
            const FIntPoint ZSpan(XzSpan.MinY, XzSpan.MaxY);
            for (int32 X = XzSpan.MinX; X <= XzSpan.MaxX; ++X) {
                const double Dx = Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.X, X, Ctx.CellSize, MinCoord, MaxCoord);
                const double DxySq = Dx * Dx + DySq;
                if (DxySq <= Ctx.InOutRadiusSq) {
                    VisitShellZRange(Container, X, Y, ZSpan, DxySq, Ctx, ConsiderElement);
                }
            }
        }

        template <typename TElement, typename TConsider>
        static void VisitShellFaceZ(
            const TSpatialHashContainer3D<TElement>& Container,
            int32 Z,
            const FCellBounds2D& XySpan,
            const FShellSearchContext3D& Ctx,
            TConsider&& ConsiderElement
        ) {
            constexpr int32 MinCoord = FSpatialCoordPolicy3D::MIN_CELL_COORD;
            constexpr int32 MaxCoord = FSpatialCoordPolicy3D::MAX_CELL_COORD;
            const double Dz = Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.Z, Z, Ctx.CellSize, MinCoord, MaxCoord);
            const double DzSq = Dz * Dz;
            if (DzSq > Ctx.InOutRadiusSq) {
                return;
            }
            for (int32 X = XySpan.MinX; X <= XySpan.MaxX; ++X) {
                const double Dx = Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.X, X, Ctx.CellSize, MinCoord, MaxCoord);
                const double DxzSq = Dx * Dx + DzSq;
                if (DxzSq > Ctx.InOutRadiusSq) {
                    continue;
                }
                for (int32 Y = XySpan.MinY; Y <= XySpan.MaxY; ++Y) {
                    const double Dy = Game::Spatial::Private::Get1DMinDistanceToCell(Ctx.Center.Y, Y, Ctx.CellSize, MinCoord, MaxCoord);
                    if (DxzSq + Dy * Dy <= Ctx.InOutRadiusSq
                        && Container.ForEachElementInCell(FSpatialCoordPolicy3D::PackCellKey(X, Y, Z), ConsiderElement) > 0) {
                        Ctx.bRingsHadElements = true;
                    }
                }
            }
        }
    };

}  // namespace Game::Spatial::Detail
