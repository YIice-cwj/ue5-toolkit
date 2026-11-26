/**
 * @file SpatialQueryCommon.h
 * @brief 2D/3D 空间哈希键雪崩散列、查询谓词适配器、无开方锥体求交与栈内联 K 近邻缓冲原语
 */

#pragma once

#include "CoreMinimal.h"
#include <type_traits>
#include "Types/FGameTypeFingerprint.h"

namespace Game::Spatial {

    /**
     * @brief 默认全通空间过滤谓词（编译期零开销短路）
     */
    struct FAcceptAllPredicate {
        /**
         * @brief 恒返回 true 的默认过滤调用运算符
         * @tparam TArgs 任意参数类型包
         * @return 恒返回 true
         */
        template <typename... TArgs>
        [[nodiscard]] constexpr bool operator()(const TArgs&...) const noexcept {
            return true;
        }
    };

    namespace Private {

        /**
         * @brief 64 位空间单元格哈希键雪崩散列键策略（复用 GameCore::Game::Hash::Hash64To32，消除 2D/3D 平面格网桶聚集）
         * @tparam TValue 桶映射值类型
         */
        template <typename TValue>
        struct TSpatialCellKeyFuncs : TDefaultMapHashableKeyFuncs<uint64, TValue, false> {
            /**
             * @brief 计算 64 位单元格键的均匀 32 位雪崩哈希码
             * @param Key 64 位打包单元格键
             * @return 均匀分布的 32 位哈希值
             */
            [[nodiscard]] static FORCEINLINE uint32 GetKeyHash(uint64 Key) noexcept {
                return Game::Hash::Hash64To32(Key);
            }
        };

        /**
         * @brief 判断坐标值是否为有效有限数值
         * @param Value 待检测双精度标量
         * @return 有限返回 true，NaN 或 Inf 返回 false
         */
        [[nodiscard]] FORCEINLINE bool IsFiniteCoord(double Value) noexcept {
            return FMath::IsFinite(Value);
        }

        /**
         * @brief 判断三维向量各分量是否全部有限
         * @param Vec 待检测三维向量
         * @return 有限返回 true
         */
        [[nodiscard]] FORCEINLINE bool IsFiniteVector(const FVector& Vec) noexcept {
            return FMath::IsFinite(Vec.X) && FMath::IsFinite(Vec.Y) && FMath::IsFinite(Vec.Z);
        }

        /**
         * @brief 判断二维向量各分量是否全部有限
         * @param Vec 待检测二维向量
         * @return 有限返回 true
         */
        [[nodiscard]] FORCEINLINE bool IsFiniteVector(const FVector2D& Vec) noexcept {
            return FMath::IsFinite(Vec.X) && FMath::IsFinite(Vec.Y);
        }

        /**
         * @brief 安全向下取整并将结果钳制到指定 32 位整数区间（杜绝超大坐标直接转 int32 的未定义溢出）
         * @param WorldCoord 世界坐标分量
         * @param InvCellSize 双精度单元格边长倒数
         * @param MinCoord 最小允许单元格坐标
         * @param MaxCoord 最大允许单元格坐标
         * @return 钳制后的合法 32 位单元格索引
         */
        [[nodiscard]] FORCEINLINE int32 FloorToClampedCellCoord(
            double WorldCoord,
            double InvCellSize,
            int32 MinCoord,
            int32 MaxCoord
        ) noexcept {
            const double Scaled = FMath::FloorToDouble(WorldCoord * InvCellSize);
            if (Scaled <= static_cast<double>(MinCoord)) {
                return MinCoord;
            }
            if (Scaled >= static_cast<double>(MaxCoord)) {
                return MaxCoord;
            }
            return static_cast<int32>(Scaled);
        }

        /**
         * @brief 计算一维双精度坐标到单元格区间的最小投影距离（钳制边界格对外侧半无限区间距离为 0）
         * @param CenterCoord 中心坐标分量
         * @param CellIndex 单元格整数索引
         * @param CellSize 双精度单元格边长
         * @param MinCellCoord 最小允许单元格索引
         * @param MaxCellCoord 最大允许单元格索引
         * @return 投影最短距离（落在单元格内部或钳制边界外侧时为 0.0）
         */
        [[nodiscard]] FORCEINLINE double Get1DMinDistanceToCell(
            double CenterCoord,
            int32 CellIndex,
            double CellSize,
            int32 MinCellCoord,
            int32 MaxCellCoord
        ) noexcept {
            const double CellMin = static_cast<double>(CellIndex) * CellSize;
            const double CellMax = CellMin + CellSize;
            if (CellIndex > MinCellCoord && CenterCoord < CellMin) {
                return CellMin - CenterCoord;
            }
            if (CellIndex < MaxCellCoord && CenterCoord > CellMax) {
                return CenterCoord - CellMax;
            }
            return 0.0;
        }

        /**
         * @brief 计算 2D/3D 球冠锥体沿单坐标轴的精确紧致包围盒偏移量（覆盖锐角与钝角全区间）
         * @param AxisDir 归一化锥体中心轴在当前坐标轴上的分量
         * @param CosHalfAngle 锥体半顶角余弦值
         * @param SinHalfAngle 锥体半顶角正弦值
         * @param Radius 锥体最大半径
         * @param OutMinOffset 输出沿该轴相对顶点的最小偏移量（<= 0）
         * @param OutMaxOffset 输出沿该轴相对顶点的最大偏移量（>= 0）
         */
        FORCEINLINE void ComputeAxisConeExtent(
            double AxisDir,
            double CosHalfAngle,
            double SinHalfAngle,
            double Radius,
            double& OutMinOffset,
            double& OutMaxOffset
        ) noexcept {
            const double ClampedDir = FMath::Clamp(AxisDir, -1.0, 1.0);
            const double SinAxis = FMath::Sqrt(FMath::Max(0.0, 1.0 - ClampedDir * ClampedDir));
            const double DirCos = ClampedDir * CosHalfAngle;
            const double SinProd = SinAxis * SinHalfAngle;

            OutMaxOffset = (ClampedDir >= CosHalfAngle)
                ? Radius
                : (Radius * FMath::Max(0.0, DirCos + SinProd));
            OutMinOffset = (-ClampedDir >= CosHalfAngle)
                ? -Radius
                : (Radius * FMath::Min(0.0, DirCos - SinProd));
        }

        /**
         * @brief 零开方、零除法的锥体/扇形夹角精确判定（支持锐角、直角、钝角与顶点重合情形）
         * @param Proj 相对位移向量与归一化朝向的点积
         * @param DistSq 相对位移向量长度平方
         * @param CosHalfAngle 半顶角余弦值
         * @param CosHalfAngleSq 半顶角余弦值平方
         * @return 落入锥体/扇形角度范围内返回 true
         */
        [[nodiscard]] FORCEINLINE bool IsWithinConeAngle(
            double Proj,
            double DistSq,
            double CosHalfAngle,
            double CosHalfAngleSq
        ) noexcept {
            constexpr double RightAngleEpsilon = 1.0e-12;
            constexpr double BoundaryTolerance = 1.0e-9;
            if (CosHalfAngle >= 0.0) {
                if (CosHalfAngle <= RightAngleEpsilon) {
                    return Proj >= 0.0;
                }
                return Proj >= 0.0 && (Proj * Proj >= CosHalfAngleSq * DistSq * (1.0 - BoundaryTolerance));
            }
            return Proj >= 0.0 || (Proj * Proj <= CosHalfAngleSq * DistSq * (1.0 + BoundaryTolerance));
        }

        /**
         * @brief 无溢出判定二维网格包围盒是否稀疏到应直接扫描元素表
         * @param MinX 最小 X 格索引
         * @param MaxX 最大 X 格索引
         * @param MinY 最小 Y 格索引
         * @param MaxY 最大 Y 格索引
         * @param ElementCount 容器当前元素总数
         * @return 覆盖格数大于元素总数时返回 true
         */
        [[nodiscard]] FORCEINLINE bool ShouldUseSparseScan2D(
            int32 MinX,
            int32 MaxX,
            int32 MinY,
            int32 MaxY,
            int32 ElementCount
        ) noexcept {
            if (MinX > MaxX || MinY > MaxY) {
                return false;
            }
            const int64 CountX = static_cast<int64>(MaxX) - static_cast<int64>(MinX) + 1LL;
            const int64 CountY = static_cast<int64>(MaxY) - static_cast<int64>(MinY) + 1LL;
            const int64 Threshold = static_cast<int64>(ElementCount);
            if (CountX > Threshold || CountY > Threshold) {
                return true;
            }
            return (CountX * CountY) > Threshold;
        }

        /**
         * @brief 无溢出判定三维网格包围盒是否稀疏到应直接扫描元素表
         * @param MinX 最小 X 格索引
         * @param MaxX 最大 X 格索引
         * @param MinY 最小 Y 格索引
         * @param MaxY 最大 Y 格索引
         * @param MinZ 最小 Z 格索引
         * @param MaxZ 最大 Z 格索引
         * @param ElementCount 容器当前元素总数
         * @return 覆盖格数大于元素总数时返回 true
         */
        [[nodiscard]] FORCEINLINE bool ShouldUseSparseScan3D(
            int32 MinX,
            int32 MaxX,
            int32 MinY,
            int32 MaxY,
            int32 MinZ,
            int32 MaxZ,
            int32 ElementCount
        ) noexcept {
            if (MinX > MaxX || MinY > MaxY || MinZ > MaxZ) {
                return false;
            }
            const int64 CountX = static_cast<int64>(MaxX) - static_cast<int64>(MinX) + 1LL;
            const int64 CountY = static_cast<int64>(MaxY) - static_cast<int64>(MinY) + 1LL;
            const int64 CountZ = static_cast<int64>(MaxZ) - static_cast<int64>(MinZ) + 1LL;
            const int64 Threshold = static_cast<int64>(ElementCount);
            if (CountX > Threshold || CountY > Threshold || CountZ > Threshold) {
                return true;
            }
            const int64 CountXY = CountX * CountY;
            if (CountXY > Threshold) {
                return true;
            }
            return (CountXY * CountZ) > Threshold;
        }

        /**
         * @brief 统一调用单参或双参过滤谓词（对 FAcceptAllPredicate 编译期零开销短路，支持 mutable 谓词）
         * @tparam TPredicate 谓词类型
         * @tparam TElement 元素类型
         * @tparam TVector 坐标向量类型（FVector2D 或 FVector）
         * @param Predicate 过滤谓词
         * @param Item 目标元素
         * @param Loc 元素坐标
         * @return 谓词判定结果
         */
        template <typename TPredicate, typename TElement, typename TVector>
        [[nodiscard]] FORCEINLINE bool InvokePredicate(
            TPredicate&& Predicate,
            const TElement& Item,
            const TVector& Loc
        ) {
            if constexpr (std::is_same_v<std::decay_t<TPredicate>, FAcceptAllPredicate>) {
                return true;
            } else if constexpr (std::is_invocable_r_v<bool, TPredicate&, const TElement&, const TVector&>) {
                return Predicate(Item, Loc);
            } else {
                return Predicate(Item);
            }
        }

        /**
         * @brief 统一调用单参或双参流式消费者
         * @tparam TConsumer 消费者类型
         * @tparam TElement 元素类型
         * @tparam TVector 坐标向量类型（FVector2D 或 FVector）
         * @param Consumer 消费者回调
         * @param Item 目标元素
         * @param Loc 元素坐标
         */
        template <typename TConsumer, typename TElement, typename TVector>
        FORCEINLINE void InvokeConsumer(
            TConsumer&& Consumer,
            const TElement& Item,
            const TVector& Loc
        ) {
            if constexpr (std::is_invocable_v<TConsumer&, const TElement&, const TVector&>) {
                Consumer(Item, Loc);
            } else {
                Consumer(Item);
            }
        }

        /**
         * @brief 校验过滤谓词并在通过时触发消费者
         * @tparam TConsumer 消费回调类型
         * @tparam TPredicate 过滤谓词类型
         * @tparam TElement 元素类型
         * @tparam TVector 坐标向量类型
         * @param Consumer 消费者
         * @param Predicate 谓词
         * @param Item 目标元素
         * @param Loc 元素坐标
         */
        template <typename TConsumer, typename TPredicate, typename TElement, typename TVector>
        FORCEINLINE void TryConsumeElement(
            TConsumer&& Consumer,
            TPredicate&& Predicate,
            const TElement& Item,
            const TVector& Loc
        ) {
            if (InvokePredicate(Predicate, Item, Loc)) {
                InvokeConsumer(Consumer, Item, Loc);
            }
        }

        /**
         * @brief 栈内联有序 K 近邻候选缓冲器（K <= InlineCapacity 时绝对零堆分配，且永不触发 K+1 重分配/回缩抖动）
         * @tparam TElement 空间元素类型
         * @tparam InlineCapacity 栈内联候选槽位数（默认 16）
         */
        template <typename TElement, int32 InlineCapacity = 16>
        class TSpatialKNearestBuffer {
        private:
            struct FCandidate {
                TElement Element;
                double DistSq = 0.0;
            };

        public:
            /**
             * @brief 构造 K 近邻候选缓冲器
             * @param InMaxCount 期望保留的最大近邻数量（必须大于 0）
             */
            explicit TSpatialKNearestBuffer(int32 InMaxCount)
                : MaxCount(InMaxCount) {
                if (MaxCount > InlineCapacity) {
                    Candidates.Reserve(MaxCount);
                }
            }

            /**
             * @brief 尝试将已通过距离上界与谓词检验的候选插入升序序列，并同步收缩截断上界
             * @param Item 候选元素
             * @param DistSq 候选到中心点的距离平方
             * @param InOutMaxValidDistSq 输入输出当前最大有效截断距离平方
             */
            void TryInsert(const TElement& Item, double DistSq, double& InOutMaxValidDistSq) {
                const int32 CurrentNum = Candidates.Num();
                if (CurrentNum == MaxCount && DistSq >= Candidates[CurrentNum - 1].DistSq) {
                    return;
                }

                const int32 InsertIdx = FindInsertIndex(DistSq);
                if (CurrentNum < MaxCount && InsertIdx == CurrentNum) {
                    Candidates.Emplace(FCandidate{Item, DistSq});
                } else {
                    int32 ShiftIdx = MaxCount - 1;
                    if (CurrentNum < MaxCount) {
                        Candidates.Emplace(MoveTemp(Candidates[CurrentNum - 1]));
                        ShiftIdx = CurrentNum - 1;
                    }
                    for (; ShiftIdx > InsertIdx; --ShiftIdx) {
                        Candidates[ShiftIdx] = MoveTemp(Candidates[ShiftIdx - 1]);
                    }
                    Candidates[InsertIdx] = FCandidate{Item, DistSq};
                }

                if (Candidates.Num() == MaxCount) {
                    InOutMaxValidDistSq = Candidates[MaxCount - 1].DistSq;
                }
            }

            /**
             * @brief 重置当前已收录候选缓冲（保留已分配容量以供复用）
             */
            void Reset() noexcept {
                Candidates.Reset();
            }

            /**
             * @brief 按距离由近至远升序流式输出全部候选元素
             * @tparam TConsumer 消费回调类型 (const TElement&) 或 (const TElement&, double DistSq)
             * @param Consumer 命中元素消费者
             */
            template <typename TConsumer>
            void Emit(TConsumer&& Consumer) const {
                for (const FCandidate& Cand : Candidates) {
                    if constexpr (std::is_invocable_v<TConsumer&, const TElement&, double>) {
                        Consumer(Cand.Element, Cand.DistSq);
                    } else {
                        Consumer(Cand.Element);
                    }
                }
            }

            /**
             * @brief 获取当前已收录候选数量
             * @return 候选数量
             */
            [[nodiscard]] int32 Num() const noexcept {
                return Candidates.Num();
            }

        private:
            [[nodiscard]] int32 FindInsertIndex(double DistSq) const noexcept {
                const int32 Count = Candidates.Num();
                if (Count <= 16) {
                    int32 Idx = 0;
                    while (Idx < Count && Candidates[Idx].DistSq < DistSq) {
                        ++Idx;
                    }
                    return Idx;
                }

                int32 Low = 0;
                int32 High = Count;
                while (Low < High) {
                    const int32 Mid = Low + ((High - Low) >> 1);
                    if (Candidates[Mid].DistSq < DistSq) {
                        Low = Mid + 1;
                    } else {
                        High = Mid;
                    }
                }
                return Low;
            }

        private:
            TArray<FCandidate, TInlineAllocator<InlineCapacity>> Candidates;
            int32 MaxCount = 0;
        };

    }  // namespace Private

}  // namespace Game::Spatial
