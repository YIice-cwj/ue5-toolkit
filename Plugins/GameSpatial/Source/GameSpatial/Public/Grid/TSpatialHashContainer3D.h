/**
 * @file TSpatialHashContainer3D.h
 * @brief 3D 纯逻辑均匀空间哈希底层容器模板（21 位偏置打包、雪崩散列、双精度格网映射与 O(1) 桶维护）
 */

#pragma once

#include "CoreMinimal.h"
#include "Math/Vector.h"
#include "Core/SpatialErrorCodes.h"
#include "Core/SpatialQueryCommon.h"
#include "Types/TGameResult.h"

/**
 * @brief 3D 纯逻辑均匀空间哈希底层容器模板
 * @tparam TElement 空间对象键或句柄类型（须支持 GetTypeHash 与 operator==）
 */
template <typename TElement>
class TSpatialHashContainer3D {
public:
    /**
     * @brief 单个三维单元格桶内的紧凑元素记录
     */
    struct FBucketEntry {
        TElement Element;
        FVector Location = FVector::ZeroVector;
    };

    /**
     * @brief 元素反查定位记录（持有所属桶键、桶内索引与当前三维坐标，保障 O(1) 更新与移除）
     */
    struct FElementRecord {
        uint64 CellKey = 0ULL;
        int32 BucketIndex = INDEX_NONE;
        FVector Location = FVector::ZeroVector;
    };

private:
    using FCellMap = TMap<
        uint64,
        TArray<FBucketEntry>,
        FDefaultSetAllocator,
        Game::Spatial::Private::TSpatialCellKeyFuncs<TArray<FBucketEntry>>
    >;

public:
    static constexpr float DEFAULT_CELL_SIZE = 500.0f;
    static constexpr int32 COORD_BITS = 21;
    static constexpr int64 COORD_OFFSET = 1LL << (COORD_BITS - 1);
    static constexpr uint64 COORD_MASK = (1ULL << COORD_BITS) - 1ULL;
    static constexpr int32 MIN_CELL_COORD = -static_cast<int32>(COORD_OFFSET);
    static constexpr int32 MAX_CELL_COORD = static_cast<int32>(COORD_OFFSET - 1);

public:
    /**
     * @brief 构造底层 3D 空间哈希容器
     * @param InCellSize 单元格边长（必须为有限值且大于 1.0f，非法时回退至默认边长）
     */
    explicit TSpatialHashContainer3D(float InCellSize = DEFAULT_CELL_SIZE) noexcept
        : CellSize((FMath::IsFinite(InCellSize) && InCellSize > 1.0f) ? InCellSize : DEFAULT_CELL_SIZE)
        , InvCellSize(1.0 / static_cast<double>(CellSize)) {
    }

    TSpatialHashContainer3D(const TSpatialHashContainer3D&) = default;
    TSpatialHashContainer3D& operator=(const TSpatialHashContainer3D&) = default;
    TSpatialHashContainer3D(TSpatialHashContainer3D&&) noexcept = default;
    TSpatialHashContainer3D& operator=(TSpatialHashContainer3D&&) noexcept = default;
    ~TSpatialHashContainer3D() = default;

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
     * @brief 向容器插入新元素及其三维空间坐标
     * @param Element 目标元素标识
     * @param Location 空间三维坐标
     * @return 插入成功返回成功态；坐标含 NaN/Inf 返回 6003，已存在返回 6002
     */
    TGameStatusResult<void> Insert(const TElement& Element, const FVector& Location) {
        if (!Game::Spatial::Private::IsFiniteVector(Location)) {
            return TGameStatusResult<void>::Failure(
                FGameError::Make(
                    Game::ModuleId::Spatial,
                    Game::SpatialErrorCodes::InvalidCoordinate,
                    TEXT("Non-finite coordinate rejected in 3D spatial grid")
                )
            );
        }

        if (Elements.Contains(Element)) {
            return TGameStatusResult<void>::Failure(
                FGameError::Make(
                    Game::ModuleId::Spatial,
                    Game::SpatialErrorCodes::ElementAlreadyExists,
                    TEXT("Element already exists in 3D spatial grid")
                )
            );
        }

        const uint64 CellKey = ComputeCellKey(Location);
        const int32 BucketIndex = Cells.FindOrAdd(CellKey).Add(FBucketEntry{Element, Location});
        Elements.Add(Element, FElementRecord{CellKey, BucketIndex, Location});
        return TGameStatusResult<void>::Success();
    }

    /**
     * @brief O(1) 更新已有元素的三维空间坐标（未跨格时原地索引直写，跨格时 O(1) 交换迁移）
     * @param Element 目标元素标识
     * @param NewLocation 新空间三维坐标
     * @return 更新成功返回成功态；坐标含 NaN/Inf 返回 6003，元素不存在返回 6001
     */
    TGameStatusResult<void> Update(const TElement& Element, const FVector& NewLocation) {
        if (!Game::Spatial::Private::IsFiniteVector(NewLocation)) {
            return TGameStatusResult<void>::Failure(
                FGameError::Make(
                    Game::ModuleId::Spatial,
                    Game::SpatialErrorCodes::InvalidCoordinate,
                    TEXT("Non-finite coordinate rejected in 3D spatial grid")
                )
            );
        }

        FElementRecord* Record = Elements.Find(Element);
        if (Record == nullptr) {
            return TGameStatusResult<void>::Failure(
                FGameError::Make(
                    Game::ModuleId::Spatial,
                    Game::SpatialErrorCodes::ElementNotFound,
                    TEXT("Element not found in 3D spatial grid")
                )
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
                FGameError::Make(
                    Game::ModuleId::Spatial,
                    Game::SpatialErrorCodes::ElementNotFound,
                    TEXT("Element not found in 3D spatial grid")
                )
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
     * @brief 零开销只读探查目标元素的三维坐标
     * @param Element 待查询的元素标识
     * @return 存在时返回坐标常量指针，不存在返回 nullptr
     */
    [[nodiscard]] const FVector* FindLocation(const TElement& Element) const noexcept {
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
     * @brief 零堆分配流式遍历容器内全部已注册元素及其三维坐标
     * @tparam TFunc 回调类型 (const TElement&) 或 (const TElement&, const FVector&)
     * @param Func 逐元素执行的消费者闭包
     */
    template <typename TFunc>
    void ForEachElement(TFunc&& Func) const {
        for (const auto& Pair : Elements) {
            Game::Spatial::Private::InvokeConsumer(Func, Pair.Key, Pair.Value.Location);
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
     * @brief 遍历指定三维包围盒覆盖的所有网格键
     * @tparam TFunc 可调用对象类型
     * @param MinX 最小 X 单元格坐标
     * @param MaxX 最大 X 单元格坐标
     * @param MinY 最小 Y 单元格坐标
     * @param MaxY 最大 Y 单元格坐标
     * @param MinZ 最小 Z 单元格坐标
     * @param MaxZ 最大 Z 单元格坐标
     * @param Func 逐单元格执行的函数 (uint64 CellKey)
     */
    template <typename TFunc>
    void ForEachCellInBox(
        int32 MinX,
        int32 MaxX,
        int32 MinY,
        int32 MaxY,
        int32 MinZ,
        int32 MaxZ,
        TFunc&& Func
    ) const {
        for (int32 X = MinX; X <= MaxX; ++X) {
            for (int32 Y = MinY; Y <= MaxY; ++Y) {
                for (int32 Z = MinZ; Z <= MaxZ; ++Z) {
                    Func(PackCellKey(X, Y, Z));
                }
            }
        }
    }

    /**
     * @brief 遍历指定单元格桶内的所有元素及其三维坐标（连续内存直读）
     * @tparam TFunc 可调用对象类型
     * @param CellKey 单元格键值
     * @param Func 逐元素执行的函数 (const TElement& Item, const FVector& Loc)
     */
    template <typename TFunc>
    void ForEachElementInCell(uint64 CellKey, TFunc&& Func) const {
        const TArray<FBucketEntry>* Bucket = Cells.Find(CellKey);
        if (Bucket == nullptr) {
            return;
        }

        for (const FBucketEntry& Entry : *Bucket) {
            Func(Entry.Element, Entry.Location);
        }
    }

    /**
     * @brief 将单轴连续世界坐标安全映射并钳制为合法 21 位有符号单元格整数索引
     * @param WorldCoord 单轴世界坐标
     * @return 钳制后的单元格整数索引
     */
    [[nodiscard]] int32 ComputeCellCoord(double WorldCoord) const noexcept {
        return Game::Spatial::Private::FloorToClampedCellCoord(WorldCoord, InvCellSize, MIN_CELL_COORD, MAX_CELL_COORD);
    }

    /**
     * @brief 计算给定连续三维坐标所在的 64 位网格键值
     * @param Location 三维浮点世界坐标
     * @return 对应的 64 位打包单元格键
     */
    [[nodiscard]] uint64 ComputeCellKey(const FVector& Location) const noexcept {
        return PackCellKey(
            ComputeCellCoord(Location.X),
            ComputeCellCoord(Location.Y),
            ComputeCellCoord(Location.Z)
        );
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

public:
    /**
     * @brief 边界钳制坐标分量至合法 21 位有符号区间
     * @param Value 原始整数坐标
     * @return 处于合法位宽区间的坐标
     */
    [[nodiscard]] static constexpr int32 ClampCoord(int32 Value) noexcept {
        return FMath::Clamp(Value, MIN_CELL_COORD, MAX_CELL_COORD);
    }

    /**
     * @brief 静态将三维离散整数坐标打包为 64 位无符号键值（21 位偏置位压缩）
     * @param CellX 单元格 X 坐标
     * @param CellY 单元格 Y 坐标
     * @param CellZ 单元格 Z 坐标
     * @return 打包后的 64 位单元格键
     */
    [[nodiscard]] static constexpr uint64 PackCellKey(int32 CellX, int32 CellY, int32 CellZ) noexcept {
        const uint64 UX = static_cast<uint64>(static_cast<int64>(ClampCoord(CellX)) + COORD_OFFSET) & COORD_MASK;
        const uint64 UY = static_cast<uint64>(static_cast<int64>(ClampCoord(CellY)) + COORD_OFFSET) & COORD_MASK;
        const uint64 UZ = static_cast<uint64>(static_cast<int64>(ClampCoord(CellZ)) + COORD_OFFSET) & COORD_MASK;
        return (UX << 42) | (UY << 21) | UZ;
    }

private:
    FCellMap Cells;
    TMap<TElement, FElementRecord> Elements;
    float CellSize = DEFAULT_CELL_SIZE;
    double InvCellSize = 1.0 / static_cast<double>(DEFAULT_CELL_SIZE);
};
