/**
 * @file TSpatialHashContainerBase.h
 * @brief 2D/3D 共享底层空间哈希容器模板（雪崩散列、双精度格网映射、单次哈希插入与 O(1) Swap-Pop 桶维护）
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SpatialErrorCodes.h"
#include "Core/SpatialQueryCommon.h"
#include "Grid/Internal/SpatialCoordPolicies.h"
#include "Types/TGameResult.h"

namespace Game::Spatial::Detail {

    /**
     * @brief 2D/3D 共享纯逻辑均匀空间哈希底层容器模板
     * @tparam TElement 空间对象键或句柄类型（须支持 GetTypeHash 与 operator==）
     * @tparam TCoordPolicy 坐标映射与打包策略（FSpatialCoordPolicy2D 或 FSpatialCoordPolicy3D）
     */
    template <typename TElement, typename TCoordPolicy>
    class TSpatialHashContainerBase {
    public:
        using FVectorType = typename TCoordPolicy::FVectorType;
        using FPolicyType = TCoordPolicy;

    private:
        struct FBucketEntry {
            TElement Element;
            FVectorType Location = FVectorType::ZeroVector;
        };

        struct FElementRecord {
            uint64 CellKey = 0ULL;
            int32 BucketIndex = INDEX_NONE;
            FVectorType Location = FVectorType::ZeroVector;
        };

        using FCellMap = TMap<
            uint64,
            TArray<FBucketEntry>,
            FDefaultSetAllocator,
            Game::Spatial::Private::TSpatialCellKeyFuncs<TArray<FBucketEntry>>
        >;

    public:
        static constexpr float DEFAULT_CELL_SIZE = TCoordPolicy::DEFAULT_CELL_SIZE;
        static constexpr int32 MIN_CELL_COORD = TCoordPolicy::MIN_CELL_COORD;
        static constexpr int32 MAX_CELL_COORD = TCoordPolicy::MAX_CELL_COORD;

    public:
        /**
         * @brief 构造底层空间哈希容器
         * @param InCellSize 单元格边长（必须为有限值且大于 1.0f，非法时回退至默认边长）
         */
        explicit TSpatialHashContainerBase(float InCellSize = DEFAULT_CELL_SIZE) noexcept
            : CellSize((FMath::IsFinite(InCellSize) && InCellSize > 1.0f) ? InCellSize : DEFAULT_CELL_SIZE)
            , InvCellSize(1.0 / static_cast<double>(CellSize)) {
        }

        TSpatialHashContainerBase(const TSpatialHashContainerBase&) = default;
        TSpatialHashContainerBase& operator=(const TSpatialHashContainerBase&) = default;
        TSpatialHashContainerBase(TSpatialHashContainerBase&&) noexcept = default;
        TSpatialHashContainerBase& operator=(TSpatialHashContainerBase&&) noexcept = default;
        ~TSpatialHashContainerBase() = default;

        /**
         * @brief 预分配元素索引表容量以减少大批量插入时的哈希扩容抖动
         * @param ExpectedElementCount 预期容纳的元素数量
         */
        void Reserve(int32 ExpectedElementCount) {
            if (ExpectedElementCount > 0) {
                Elements.Reserve(ExpectedElementCount);
            }
        }

        /**
         * @brief 向容器插入新元素及其空间坐标（单次元素表哈希探查）
         * @param Element 目标元素标识
         * @param Location 空间坐标
         * @return 插入成功返回成功态；坐标含 NaN/Inf 返回 6003，已存在返回 6002
         */
        TGameStatusResult<void> Insert(const TElement& Element, const FVectorType& Location) {
            if (!Game::Spatial::Private::IsFiniteVector(Location)) {
                return TGameStatusResult<void>::Failure(
                    Game::ModuleId::Spatial,
                    Game::SpatialErrorCodes::InvalidCoordinate,
                    TCoordPolicy::InvalidCoordinateMessage
                );
            }

            FElementRecord& Record = Elements.FindOrAdd(Element);
            if (Record.BucketIndex != INDEX_NONE) {
                return TGameStatusResult<void>::Failure(
                    Game::ModuleId::Spatial,
                    Game::SpatialErrorCodes::ElementAlreadyExists,
                    TCoordPolicy::AlreadyExistsMessage
                );
            }

            const uint64 CellKey = ComputeCellKey(Location);
            const int32 BucketIndex = Cells.FindOrAdd(CellKey).Add(FBucketEntry{Element, Location});
            Record.CellKey = CellKey;
            Record.BucketIndex = BucketIndex;
            Record.Location = Location;
            return TGameStatusResult<void>::Success();
        }

        /**
         * @brief O(1) 更新已有元素的空间坐标（未跨格时原地索引直写，跨格时 O(1) 交换迁移）
         * @param Element 目标元素标识
         * @param NewLocation 新空间坐标
         * @return 更新成功返回成功态；坐标含 NaN/Inf 返回 6003，元素不存在返回 6001
         */
        TGameStatusResult<void> Update(const TElement& Element, const FVectorType& NewLocation) {
            if (!Game::Spatial::Private::IsFiniteVector(NewLocation)) {
                return TGameStatusResult<void>::Failure(
                    Game::ModuleId::Spatial,
                    Game::SpatialErrorCodes::InvalidCoordinate,
                    TCoordPolicy::InvalidCoordinateMessage
                );
            }

            FElementRecord* Record = Elements.Find(Element);
            if (Record == nullptr) {
                return TGameStatusResult<void>::Failure(
                    Game::ModuleId::Spatial,
                    Game::SpatialErrorCodes::ElementNotFound,
                    TCoordPolicy::NotFoundMessage
                );
            }

            if (Record->Location == NewLocation) {
                return TGameStatusResult<void>::Success();
            }

            const uint64 NewCellKey = ComputeCellKey(NewLocation);
            if (Record->CellKey != NewCellKey) {
                RemoveFromBucket(Record->CellKey, Record->BucketIndex);
                Record->CellKey = NewCellKey;
                Record->BucketIndex = Cells.FindOrAdd(NewCellKey).Add(FBucketEntry{Element, NewLocation});
            } else {
                TArray<FBucketEntry>& Bucket = Cells.FindChecked(Record->CellKey);
                Bucket[Record->BucketIndex].Location = NewLocation;
            }

            Record->Location = NewLocation;
            return TGameStatusResult<void>::Success();
        }

        /**
         * @brief O(1) 从容器中移除目标元素并自动回收空桶
         * @param Element 待移除的元素标识
         * @return 移除成功返回成功态，元素不存在返回携带 6001 错误码的失败态
         */
        TGameStatusResult<void> Remove(const TElement& Element) {
            FElementRecord RemovedRecord;
            if (!Elements.RemoveAndCopyValue(Element, RemovedRecord)) {
                return TGameStatusResult<void>::Failure(
                    Game::ModuleId::Spatial,
                    Game::SpatialErrorCodes::ElementNotFound,
                    TCoordPolicy::NotFoundMessage
                );
            }

            RemoveFromBucket(RemovedRecord.CellKey, RemovedRecord.BucketIndex);
            return TGameStatusResult<void>::Success();
        }

        /**
         * @brief 检查容器中是否包含目标元素
         * @param Element 待查询的元素标识
         * @return 包含返回 true
         */
        [[nodiscard]] bool Contains(const TElement& Element) const noexcept {
            return Elements.Contains(Element);
        }

        /**
         * @brief 零开销只读探查目标元素的空间坐标
         * @param Element 待查询的元素标识
         * @return 存在时返回坐标常量指针，不存在返回 nullptr
         */
        [[nodiscard]] const FVectorType* FindLocation(const TElement& Element) const noexcept {
            if (const FElementRecord* Found = Elements.Find(Element)) {
                return &Found->Location;
            }
            return nullptr;
        }

        /**
         * @brief 清空容器中所有已注册元素与单元格桶
         */
        void Clear() {
            Cells.Empty();
            Elements.Empty();
        }

        /**
         * @brief 查询当前容器中容纳的元素总数
         * @return 元素总数
         */
        [[nodiscard]] int32 Num() const noexcept {
            return Elements.Num();
        }

        /**
         * @brief 检查容器是否为空
         * @return 无已注册元素返回 true
         */
        [[nodiscard]] bool IsEmpty() const noexcept {
            return Elements.IsEmpty();
        }

        /**
         * @brief 获取当前容器内所有已注册元素的快照列表
         * @param OutElements 输出所有元素列表
         */
        void GetAllElements(TArray<TElement>& OutElements) const {
            OutElements.Reset(Elements.Num());
            for (const auto& Pair : Elements) {
                OutElements.Add(Pair.Key);
            }
        }

        /**
         * @brief 零堆分配流式遍历容器内全部已注册元素及其坐标
         * @tparam TFunc 回调类型 (const TElement&) 或 (const TElement&, const FVectorType&)
         * @param Func 逐元素执行的消费者闭包
         */
        template <typename TFunc>
        void ForEachElement(TFunc&& Func) const {
            for (const auto& Pair : Elements) {
                Game::Spatial::Private::InvokeConsumer(Func, Pair.Key, Pair.Value.Location);
            }
        }

        /**
         * @brief 零堆分配流式遍历指定单元格之外的全部已注册元素及其坐标（避免稀疏回退重复评估中心格）
         * @tparam TFunc 回调类型 (const TElement&) 或 (const TElement&, const FVectorType&)
         * @param ExcludedCellKey 需要跳过的单元格打包键
         * @param Func 逐元素执行的消费者闭包
         */
        template <typename TFunc>
        void ForEachElementExcludingCell(uint64 ExcludedCellKey, TFunc&& Func) const {
            for (const auto& Pair : Elements) {
                if (Pair.Value.CellKey != ExcludedCellKey) {
                    Game::Spatial::Private::InvokeConsumer(Func, Pair.Key, Pair.Value.Location);
                }
            }
        }

        /**
         * @brief 零堆分配流式遍历容器内全部已注册元素、坐标及其缓存的 64 位单元格键（供稀疏回退零浮点位解包去重）
         * @tparam TFunc 回调类型 (const TElement&, const FVectorType&, uint64 CellKey)
         * @param Func 逐元素执行的消费者闭包
         */
        template <typename TFunc>
        void ForEachElementWithCellKey(TFunc&& Func) const {
            for (const auto& Pair : Elements) {
                Func(Pair.Key, Pair.Value.Location, Pair.Value.CellKey);
            }
        }

        /**
         * @brief 查询当前处于活跃态的单元格桶数量
         * @return 活跃桶数量
         */
        [[nodiscard]] int32 GetBucketCount() const noexcept {
            return Cells.Num();
        }

        /**
         * @brief 查询当前容器设置的单元格边长
         * @return 边长尺寸
         */
        [[nodiscard]] float GetCellSize() const noexcept {
            return CellSize;
        }

        /**
         * @brief 查询当前容器缓存的双精度单元格边长倒数
         * @return 单元格边长倒数
         */
        [[nodiscard]] double GetInvCellSize() const noexcept {
            return InvCellSize;
        }

        /**
         * @brief 遍历指定单元格桶内的所有元素及其坐标（连续内存直读）
         * @tparam TFunc 可调用对象类型
         * @param CellKey 单元格键值
         * @param Func 逐元素执行的函数 (const TElement& Item, const FVectorType& Loc)
         * @return 本次访问到的桶内元素数量（空桶返回 0）
         */
        template <typename TFunc>
        FORCEINLINE int32 ForEachElementInCell(uint64 CellKey, TFunc&& Func) const {
            const TArray<FBucketEntry>* Bucket = Cells.Find(CellKey);
            if (Bucket == nullptr) {
                return 0;
            }

            for (const FBucketEntry& Entry : *Bucket) {
                Func(Entry.Element, Entry.Location);
            }
            return Bucket->Num();
        }

        /**
         * @brief 将单轴连续世界坐标安全映射并钳制为无溢出单元格整数索引
         * @param WorldCoord 单轴世界坐标
         * @return 钳制后的单元格整数索引
         */
        [[nodiscard]] int32 ComputeCellCoord(double WorldCoord) const noexcept {
            return TCoordPolicy::ComputeCellCoord(WorldCoord, InvCellSize);
        }

        /**
         * @brief 计算给定连续坐标所在的 64 位单元格键值
         * @param Location 世界坐标
         * @return 对应的 64 位打包单元格键
         */
        [[nodiscard]] uint64 ComputeCellKey(const FVectorType& Location) const noexcept {
            return TCoordPolicy::ComputeCellKey(Location, InvCellSize);
        }

    private:
        void RemoveFromBucket(uint64 CellKey, int32 BucketIndex) {
            TArray<FBucketEntry>& Bucket = Cells.FindChecked(CellKey);
            const int32 LastIndex = Bucket.Num() - 1;
            if (BucketIndex != LastIndex) {
                Bucket[BucketIndex] = MoveTemp(Bucket[LastIndex]);
                Elements.FindChecked(Bucket[BucketIndex].Element).BucketIndex = BucketIndex;
            }

            Bucket.Pop(EAllowShrinking::No);
            if (Bucket.IsEmpty()) {
                Cells.Remove(CellKey);
            }
        }

    private:
        FCellMap Cells;
        TMap<TElement, FElementRecord> Elements;
        float CellSize = DEFAULT_CELL_SIZE;
        double InvCellSize = 1.0 / static_cast<double>(DEFAULT_CELL_SIZE);
    };

    template <typename TElement>
    using TSpatialHashContainer2D = TSpatialHashContainerBase<TElement, FSpatialCoordPolicy2D>;

    template <typename TElement>
    using TSpatialHashContainer3D = TSpatialHashContainerBase<TElement, FSpatialCoordPolicy3D>;

}  // namespace Game::Spatial::Detail
