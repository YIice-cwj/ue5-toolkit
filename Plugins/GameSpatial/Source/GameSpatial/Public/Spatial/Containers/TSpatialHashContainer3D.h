/**
 * @file TSpatialHashContainer3D.h
 * @brief 3D 纯逻辑均匀空间哈希底层容器模板（仅负责 21 位高维偏置位压缩、桶映射与拓扑生命周期）
 */

#pragma once

#include "CoreMinimal.h"
#include "Math/Vector.h"
#include "Spatial/SpatialErrorCodes.h"
#include "Types/TGameResult.h"

/**
 * @brief 3D 纯逻辑均匀空间哈希底层容器模板
 * @tparam TElement 空间对象键或句柄类型（须支持 GetTypeHash 与 operator==）
 */
template <typename TElement>
class TSpatialHashContainer3D {
public:
    static constexpr float DEFAULT_CELL_SIZE = 500.0f;
    static constexpr int32 COORD_BITS = 21;
    static constexpr int64 COORD_OFFSET = 1LL << (COORD_BITS - 1);
    static constexpr uint64 COORD_MASK = (1ULL << COORD_BITS) - 1ULL;
    static constexpr int32 MIN_CELL_COORD = -static_cast<int32>(COORD_OFFSET);
    static constexpr int32 MAX_CELL_COORD = static_cast<int32>(COORD_OFFSET - 1);

    struct FBucketEntry {
        TElement Element;
        FVector Location = FVector::ZeroVector;
    };

    struct FElementRecord {
        uint64 CellKey = 0;
        FVector Location = FVector::ZeroVector;
    };

public:
    /**
     * @brief 构造底层 3D 空间哈希容器
     * @param InCellSize 单元格边长（必须大于 1.0f）
     */
    explicit TSpatialHashContainer3D(float InCellSize = DEFAULT_CELL_SIZE) noexcept
        : CellSize(InCellSize > 1.0f ? InCellSize : DEFAULT_CELL_SIZE)
        , InvCellSize(1.0f / (InCellSize > 1.0f ? InCellSize : DEFAULT_CELL_SIZE)) {}

    TSpatialHashContainer3D(const TSpatialHashContainer3D&) = default;
    TSpatialHashContainer3D& operator=(const TSpatialHashContainer3D&) = default;
    TSpatialHashContainer3D(TSpatialHashContainer3D&&) noexcept = default;
    TSpatialHashContainer3D& operator=(TSpatialHashContainer3D&&) noexcept = default;
    ~TSpatialHashContainer3D() = default;

    /**
     * @brief 向容器插入新元素及其三维空间坐标
     * @param Element 目标元素标识
     * @param Location 空间三维坐标
     * @return 插入成功返回成功态，若已存在同名元素返回携带 6002 错误码的失败态
     */
    TGameStatusResult<void> Insert(const TElement& Element, const FVector& Location) {
        if (Elements.Contains(Element)) {
            return TGameStatusResult<void>::Failure(
                FGameError::Make(Game::ModuleId::Spatial, Game::SpatialErrorCodes::ElementAlreadyExists, TEXT("Element already exists in 3D spatial grid"))
            );
        }

        const uint64 CellKey = ComputeCellKey(Location);
        Cells.FindOrAdd(CellKey).Add(FBucketEntry{Element, Location});
        Elements.Add(Element, FElementRecord{CellKey, Location});
        return TGameStatusResult<void>::Success();
    }

    /**
     * @brief 更新已有元素的三维空间坐标（若未跨越单元格边界则无移动开销）
     * @param Element 目标元素标识
     * @param NewLocation 新空间三维坐标
     * @return 元素存在且更新成功返回成功态，不存在返回携带 6001 错误码的失败态
     */
    TGameStatusResult<void> Update(const TElement& Element, const FVector& NewLocation) {
        FElementRecord* Record = Elements.Find(Element);
        if (Record == nullptr) {
            return TGameStatusResult<void>::Failure(
                FGameError::Make(Game::ModuleId::Spatial, Game::SpatialErrorCodes::ElementNotFound, TEXT("Element not found in 3D spatial grid"))
            );
        }

        if (Record->Location == NewLocation) {
            return TGameStatusResult<void>::Success();
        }

        const uint64 NewCellKey = ComputeCellKey(NewLocation);
        if (Record->CellKey != NewCellKey) {
            RemoveFromBucket(Record->CellKey, Element);
            Cells.FindOrAdd(NewCellKey).Add(FBucketEntry{Element, NewLocation});
            Record->CellKey = NewCellKey;
        } else {
            TArray<FBucketEntry>* Bucket = Cells.Find(Record->CellKey);
            if (Bucket != nullptr) {
                for (FBucketEntry& Entry : *Bucket) {
                    if (Entry.Element == Element) {
                        Entry.Location = NewLocation;
                        break;
                    }
                }
            }
        }

        Record->Location = NewLocation;
        return TGameStatusResult<void>::Success();
    }

    /**
     * @brief 从容器中移除目标元素
     * @param Element 待移除的元素标识
     * @return 成功移除返回成功态，元素不存在返回携带 6001 错误码的失败态
     */
    TGameStatusResult<void> Remove(const TElement& Element) {
        FElementRecord RemovedRecord;
        if (!Elements.RemoveAndCopyValue(Element, RemovedRecord)) {
            return TGameStatusResult<void>::Failure(
                FGameError::Make(Game::ModuleId::Spatial, Game::SpatialErrorCodes::ElementNotFound, TEXT("Element not found in 3D spatial grid"))
            );
        }

        RemoveFromBucket(RemovedRecord.CellKey, Element);
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
     * @brief 获取目标元素的三维坐标
     * @param Element 待查询的元素标识
     * @return 成功返回包含三维坐标的成功态，元素不存在返回携带 6001 错误码的失败态
     */
    [[nodiscard]] TGameStatusResult<FVector> GetLocation(const TElement& Element) const {
        if (const FElementRecord* Found = Elements.Find(Element)) {
            return TGameStatusResult<FVector>::Success(Found->Location);
        }
        return TGameStatusResult<FVector>::Failure(
            FGameError::Make(Game::ModuleId::Spatial, Game::SpatialErrorCodes::ElementNotFound, TEXT("Element not found in 3D spatial grid"))
        );
    }

    /**
     * @brief 轻量只读探查目标元素的三维坐标（零错误包装开销）
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
     * @brief 查询当前分配并处于活跃态的单元格桶数量
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
     * @brief 查询当前容器单元格边长的倒数
     * @return 边长倒数
     */
    [[nodiscard]] float GetInvCellSize() const noexcept {
        return InvCellSize;
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
        int32 MinX, int32 MaxX,
        int32 MinY, int32 MaxY,
        int32 MinZ, int32 MaxZ,
        TFunc Func
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
     * @brief 遍历指定单元格桶内的所有元素及其三维坐标（零哈希查表，连续内存直读）
     * @tparam TFunc 可调用对象类型
     * @param CellKey 单元格键值
     * @param Func 逐元素执行的函数 (const TElement& Item, const FVector& Loc)
     */
    template <typename TFunc>
    void ForEachElementInCell(uint64 CellKey, TFunc Func) const {
        const TArray<FBucketEntry>* Bucket = Cells.Find(CellKey);
        if (Bucket == nullptr) {
            return;
        }

        for (const FBucketEntry& Entry : *Bucket) {
            Func(Entry.Element, Entry.Location);
        }
    }

    /**
     * @brief 边界钳制坐标分量至合法位宽区间
     * @param Value 原始整数坐标
     * @return 处于合法区间的坐标
     */
    [[nodiscard]] static constexpr int32 ClampCoord(int32 Value) noexcept {
        return FMath::Clamp(Value, MIN_CELL_COORD, MAX_CELL_COORD);
    }

    /**
     * @brief 静态将三维离散整数坐标打包为 64 位无符号键值（21 位偏置位压缩）
     * @param CellX 单元格 X 坐标
     * @param CellY 单元格 Y 坐标
     * @param CellZ 单元格 Z 坐标
     * @return 打包后的 64 位哈希键
     */
    [[nodiscard]] static constexpr uint64 PackCellKey(int32 CellX, int32 CellY, int32 CellZ) noexcept {
        const uint64 UX = static_cast<uint64>(static_cast<int64>(CellX) + COORD_OFFSET) & COORD_MASK;
        const uint64 UY = static_cast<uint64>(static_cast<int64>(CellY) + COORD_OFFSET) & COORD_MASK;
        const uint64 UZ = static_cast<uint64>(static_cast<int64>(CellZ) + COORD_OFFSET) & COORD_MASK;
        return (UX << 42) | (UY << 21) | UZ;
    }

    /**
     * @brief 计算给定连续三维坐标所在的 64 位网格键值
     * @param Location 三维浮点世界坐标
     * @return 对应的 64 位哈希键
     */
    [[nodiscard]] uint64 ComputeCellKey(const FVector& Location) const noexcept {
        const int32 CellX = ClampCoord(FMath::FloorToInt(Location.X * InvCellSize));
        const int32 CellY = ClampCoord(FMath::FloorToInt(Location.Y * InvCellSize));
        const int32 CellZ = ClampCoord(FMath::FloorToInt(Location.Z * InvCellSize));
        return PackCellKey(CellX, CellY, CellZ);
    }

private:
    void RemoveFromBucket(uint64 CellKey, const TElement& Element) {
        TArray<FBucketEntry>* Bucket = Cells.Find(CellKey);
        if (Bucket == nullptr) {
            return;
        }
        const int32 FoundIdx = Bucket->IndexOfByPredicate([&Element](const FBucketEntry& Entry) {
            return Entry.Element == Element;
        });
        if (FoundIdx != INDEX_NONE) {
            Bucket->RemoveAtSwap(FoundIdx);
            if (Bucket->IsEmpty()) {
                Cells.Remove(CellKey);
            }
        }
    }

private:
    TMap<uint64, TArray<FBucketEntry>> Cells;
    TMap<TElement, FElementRecord> Elements;
    float CellSize = DEFAULT_CELL_SIZE;
    float InvCellSize = 1.0f / DEFAULT_CELL_SIZE;
};
