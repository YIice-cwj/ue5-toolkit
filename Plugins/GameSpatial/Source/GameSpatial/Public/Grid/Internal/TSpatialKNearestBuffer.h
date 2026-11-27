/**
 * @file TSpatialKNearestBuffer.h
 * @brief 栈内联有序 K 近邻候选缓冲器模板（K <= InlineCapacity 零堆分配，无容器自引用移动）
 */

#pragma once

#include "CoreMinimal.h"
#include <type_traits>

namespace Game::Spatial::Detail {

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
                SyncMaxDistSq(InOutMaxValidDistSq);
                return;
            }

            int32 ShiftIdx = MaxCount - 1;
            if (CurrentNum < MaxCount) {
                FCandidate TailCopy = MoveTemp(Candidates[CurrentNum - 1]);
                Candidates.Emplace(MoveTemp(TailCopy));
                ShiftIdx = CurrentNum - 1;
            }
            for (; ShiftIdx > InsertIdx; --ShiftIdx) {
                Candidates[ShiftIdx] = MoveTemp(Candidates[ShiftIdx - 1]);
            }
            Candidates[InsertIdx] = FCandidate{Item, DistSq};
            SyncMaxDistSq(InOutMaxValidDistSq);
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
        void SyncMaxDistSq(double& InOutMaxValidDistSq) const noexcept {
            if (Candidates.Num() == MaxCount) {
                InOutMaxValidDistSq = Candidates[MaxCount - 1].DistSq;
            }
        }

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

}  // namespace Game::Spatial::Detail
