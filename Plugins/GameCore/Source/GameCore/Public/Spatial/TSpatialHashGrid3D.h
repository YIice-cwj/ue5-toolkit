/**
 * @file TSpatialHashGrid3D.h
 * @brief 纯逻辑轻量 3D 均匀空间哈希网格模板（脱离物理引擎，纳秒级 3D 范围检索与近邻查询）
 */

#pragma once

#include "CoreMinimal.h"
#include "Math/Box.h"
#include "Math/Vector.h"

/**
 * @brief 3D 纯逻辑均匀空间哈希网格容器模板
 * @tparam TElement 空间对象键或句柄类型（须支持 GetTypeHash 与 operator==）
 */
template <typename TElement>
class TSpatialHashGrid3D {
public:
    static constexpr float DEFAULT_CELL_SIZE = 500.0f;
    static constexpr int32 COORD_BITS = 21;
    static constexpr int32 COORD_OFFSET = (1 << (COORD_BITS - 1));
    static constexpr uint64 COORD_MASK = (1ULL << COORD_BITS) - 1ULL;
    static constexpr int32 MIN_CELL_COORD = -COORD_OFFSET;
    static constexpr int32 MAX_CELL_COORD = COORD_OFFSET - 1;

public:
    /**
     * @brief 构造三维空间哈希网格并指定单元格边长跨度
     * @param InCellSize 单元格立方体边长（必须大于 1.0f）
     */
    explicit TSpatialHashGrid3D(float InCellSize = DEFAULT_CELL_SIZE) noexcept
        : CellSize(InCellSize > 1.0f ? InCellSize : DEFAULT_CELL_SIZE)
        , InvCellSize(1.0f / (InCellSize > 1.0f ? InCellSize : DEFAULT_CELL_SIZE)) {}

    /**
     * @brief 向网格中插入新元素及其三维空间坐标
     * @param Element 目标元素标识
     * @param Location 三维空间坐标
     * @return 插入成功返回 true，若已存在同名元素则返回 false
     */
    bool Insert(const TElement& Element, const FVector& Location) {
        if (ElementToCell.Contains(Element)) {
            return false;
        }

        const uint64 CellKey = ComputeCellKey(Location);
        Cells.FindOrAdd(CellKey).Add(Element);
        ElementToCell.Add(Element, CellKey);
        ElementLocations.Add(Element, Location);
        return true;
    }

    /**
     * @brief 更新已有元素的三维空间坐标（若未跨越单元格边界则无跨桶开销）
     * @param Element 目标元素标识
     * @param NewLocation 新三维空间坐标
     * @return 元素存在且更新成功返回 true，不存在返回 false
     */
    bool Update(const TElement& Element, const FVector& NewLocation) {
        uint64* ExistingCellKey = ElementToCell.Find(Element);
        if (ExistingCellKey == nullptr) {
            return false;
        }

        const uint64 NewCellKey = ComputeCellKey(NewLocation);
        if (*ExistingCellKey != NewCellKey) {
            RemoveFromBucket(*ExistingCellKey, Element);
            Cells.FindOrAdd(NewCellKey).Add(Element);
            *ExistingCellKey = NewCellKey;
        }

        ElementLocations.Add(Element, NewLocation);
        return true;
    }

    /**
     * @brief 从网格中移除目标元素
     * @param Element 待移除的元素标识
     * @return 成功移除返回 true，元素不存在返回 false
     */
    bool Remove(const TElement& Element) {
        uint64 RemovedCellKey = 0;
        if (!ElementToCell.RemoveAndCopyValue(Element, RemovedCellKey)) {
            return false;
        }

        ElementLocations.Remove(Element);
        RemoveFromBucket(RemovedCellKey, Element);
        return true;
    }

    /**
     * @brief 检查网格中是否包含目标元素
     * @param Element 待查询的元素标识
     * @return 包含返回 true
     */
    [[nodiscard]] bool Contains(const TElement& Element) const noexcept {
        return ElementToCell.Contains(Element);
    }

    /**
     * @brief 获取目标元素的当前三维坐标
     * @param Element 待查询的元素标识
     * @param OutLocation 输出三维空间坐标
     * @return 找到有效元素返回 true
     */
    bool GetLocation(const TElement& Element, FVector& OutLocation) const {
        if (const FVector* Found = ElementLocations.Find(Element)) {
            OutLocation = *Found;
            return true;
        }
        return false;
    }

    /**
     * @brief 清空网格中所有已注册元素与单元格桶
     */
    void Clear() {
        Cells.Empty();
        ElementToCell.Empty();
        ElementLocations.Empty();
    }

    /**
     * @brief 查询当前网格中容纳的元素总数
     * @return 元素总数
     */
    [[nodiscard]] int32 Num() const noexcept {
        return ElementToCell.Num();
    }

    /**
     * @brief 查询当前分配并处于活跃态的单元格桶数量
     * @return 活跃桶数量
     */
    [[nodiscard]] int32 GetBucketCount() const noexcept {
        return Cells.Num();
    }

    /**
     * @brief 查询当前网格设置的单元格边长
     * @return 边长尺寸
     */
    [[nodiscard]] float GetCellSize() const noexcept {
        return CellSize;
    }

    /**
     * @brief 三维半径球形范围查询
     * @param Center 球心坐标
     * @param Radius 查询半径（须大于 0）
     * @param OutResults 输出落入球体内的元素集合
     */
    void QueryRadius(const FVector& Center, float Radius, TArray<TElement>& OutResults) const {
        OutResults.Reset();
        if (Radius <= 0.0f || ElementToCell.IsEmpty()) {
            return;
        }

        const double RadiusSq = static_cast<double>(Radius) * static_cast<double>(Radius);
        const int32 MinX = ClampCoord(FMath::FloorToInt((Center.X - Radius) * InvCellSize));
        const int32 MaxX = ClampCoord(FMath::FloorToInt((Center.X + Radius) * InvCellSize));
        const int32 MinY = ClampCoord(FMath::FloorToInt((Center.Y - Radius) * InvCellSize));
        const int32 MaxY = ClampCoord(FMath::FloorToInt((Center.Y + Radius) * InvCellSize));
        const int32 MinZ = ClampCoord(FMath::FloorToInt((Center.Z - Radius) * InvCellSize));
        const int32 MaxZ = ClampCoord(FMath::FloorToInt((Center.Z + Radius) * InvCellSize));

        ForEachCellInBox(MinX, MaxX, MinY, MaxY, MinZ, MaxZ, [this, &Center, RadiusSq, &OutResults](uint64 Key) {
            ForEachElementInCell(Key, [&Center, RadiusSq, &OutResults](const TElement& Item, const FVector& Loc) {
                if (FVector::DistSquared(Center, Loc) <= RadiusSq) {
                    OutResults.Add(Item);
                }
            });
        });
    }

    /**
     * @brief 轴对齐三维包围盒 AABB 范围查询
     * @param Box 查询轴对齐包围盒
     * @param OutResults 输出落入包围盒内的元素集合
     */
    void QueryAABB(const FBox& Box, TArray<TElement>& OutResults) const {
        OutResults.Reset();
        if (!Box.IsValid || ElementToCell.IsEmpty()) {
            return;
        }

        const int32 MinX = ClampCoord(FMath::FloorToInt(Box.Min.X * InvCellSize));
        const int32 MaxX = ClampCoord(FMath::FloorToInt(Box.Max.X * InvCellSize));
        const int32 MinY = ClampCoord(FMath::FloorToInt(Box.Min.Y * InvCellSize));
        const int32 MaxY = ClampCoord(FMath::FloorToInt(Box.Max.Y * InvCellSize));
        const int32 MinZ = ClampCoord(FMath::FloorToInt(Box.Min.Z * InvCellSize));
        const int32 MaxZ = ClampCoord(FMath::FloorToInt(Box.Max.Z * InvCellSize));

        ForEachCellInBox(MinX, MaxX, MinY, MaxY, MinZ, MaxZ, [this, &Box, &OutResults](uint64 Key) {
            ForEachElementInCell(Key, [&Box, &OutResults](const TElement& Item, const FVector& Loc) {
                if (Loc.X >= Box.Min.X && Loc.X <= Box.Max.X &&
                    Loc.Y >= Box.Min.Y && Loc.Y <= Box.Max.Y &&
                    Loc.Z >= Box.Min.Z && Loc.Z <= Box.Max.Z) {
                    OutResults.Add(Item);
                }
            });
        });
    }

    /**
     * @brief 三维直立圆柱体范围查询（常用于地面战斗判定与垂直范围伤害）
     * @param Center 圆柱底面/中心水平原点及高度参考中心
     * @param Radius 水平截面半径
     * @param HalfHeight 垂直半高（总高度为 2 * HalfHeight）
     * @param OutResults 输出落入圆柱体内的元素集合
     */
    void QueryCylinder(const FVector& Center, float Radius, float HalfHeight, TArray<TElement>& OutResults) const {
        OutResults.Reset();
        if (Radius <= 0.0f || HalfHeight <= 0.0f || ElementToCell.IsEmpty()) {
            return;
        }

        const double RadiusSq = static_cast<double>(Radius) * static_cast<double>(Radius);
        const int32 MinX = ClampCoord(FMath::FloorToInt((Center.X - Radius) * InvCellSize));
        const int32 MaxX = ClampCoord(FMath::FloorToInt((Center.X + Radius) * InvCellSize));
        const int32 MinY = ClampCoord(FMath::FloorToInt((Center.Y - Radius) * InvCellSize));
        const int32 MaxY = ClampCoord(FMath::FloorToInt((Center.Y + Radius) * InvCellSize));
        const int32 MinZ = ClampCoord(FMath::FloorToInt((Center.Z - HalfHeight) * InvCellSize));
        const int32 MaxZ = ClampCoord(FMath::FloorToInt((Center.Z + HalfHeight) * InvCellSize));

        ForEachCellInBox(MinX, MaxX, MinY, MaxY, MinZ, MaxZ, [this, &Center, RadiusSq, HalfHeight, &OutResults](uint64 Key) {
            ForEachElementInCell(Key, [&Center, RadiusSq, HalfHeight, &OutResults](const TElement& Item, const FVector& Loc) {
                const double Dz = FMath::Abs(Loc.Z - Center.Z);
                if (Dz > HalfHeight) {
                    return;
                }
                const double Dx = Loc.X - Center.X;
                const double Dy = Loc.Y - Center.Y;
                if ((Dx * Dx + Dy * Dy) <= RadiusSq) {
                    OutResults.Add(Item);
                }
            });
        });
    }

    /**
     * @brief 查找距离给定中心三维点最近的目标元素
     * @param Center 中心点三维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param OutNearest 输出最近元素
     * @return 存在有效元素返回 true，指定半径内无元素返回 false
     */
    bool QueryNearest(const FVector& Center, float MaxSearchRadius, TElement& OutNearest) const {
        TArray<TElement> Candidates;
        QueryRadius(Center, MaxSearchRadius, Candidates);
        if (Candidates.IsEmpty()) {
            return false;
        }

        double ClosestDistSq = TNumericLimits<double>::Max();
        const TElement* BestCandidate = nullptr;

        for (const TElement& Item : Candidates) {
            const FVector* Loc = ElementLocations.Find(Item);
            if (Loc == nullptr) {
                continue;
            }
            const double DistSq = FVector::DistSquared(Center, *Loc);
            if (DistSq < ClosestDistSq) {
                ClosestDistSq = DistSq;
                BestCandidate = &Item;
            }
        }

        if (BestCandidate == nullptr) {
            return false;
        }
        OutNearest = *BestCandidate;
        return true;
    }

private:
    template <typename TPredicate>
    void ForEachCellInBox(
        int32 MinX, int32 MaxX,
        int32 MinY, int32 MaxY,
        int32 MinZ, int32 MaxZ,
        TPredicate Predicate
    ) const {
        for (int32 X = MinX; X <= MaxX; ++X) {
            for (int32 Y = MinY; Y <= MaxY; ++Y) {
                for (int32 Z = MinZ; Z <= MaxZ; ++Z) {
                    Predicate(PackCellKey(X, Y, Z));
                }
            }
        }
    }

    template <typename TPredicate>
    void ForEachElementInCell(uint64 CellKey, TPredicate Predicate) const {
        const TArray<TElement>* Bucket = Cells.Find(CellKey);
        if (Bucket == nullptr) {
            return;
        }

        for (const TElement& Item : *Bucket) {
            const FVector* Loc = ElementLocations.Find(Item);
            if (Loc != nullptr) {
                Predicate(Item, *Loc);
            }
        }
    }

    void RemoveFromBucket(uint64 CellKey, const TElement& Element) {
        TArray<TElement>* Bucket = Cells.Find(CellKey);
        if (Bucket == nullptr) {
            return;
        }
        Bucket->RemoveSingleSwap(Element);
        if (Bucket->IsEmpty()) {
            Cells.Remove(CellKey);
        }
    }

    [[nodiscard]] static constexpr int32 ClampCoord(int32 Value) noexcept {
        return FMath::Clamp(Value, MIN_CELL_COORD, MAX_CELL_COORD);
    }

    [[nodiscard]] static constexpr uint64 PackCellKey(int32 CellX, int32 CellY, int32 CellZ) noexcept {
        const uint64 UX = static_cast<uint64>(static_cast<int64>(CellX) + COORD_OFFSET) & COORD_MASK;
        const uint64 UY = static_cast<uint64>(static_cast<int64>(CellY) + COORD_OFFSET) & COORD_MASK;
        const uint64 UZ = static_cast<uint64>(static_cast<int64>(CellZ) + COORD_OFFSET) & COORD_MASK;
        return (UX << 42) | (UY << 21) | UZ;
    }

    [[nodiscard]] uint64 ComputeCellKey(const FVector& Location) const noexcept {
        const int32 CellX = ClampCoord(FMath::FloorToInt(Location.X * InvCellSize));
        const int32 CellY = ClampCoord(FMath::FloorToInt(Location.Y * InvCellSize));
        const int32 CellZ = ClampCoord(FMath::FloorToInt(Location.Z * InvCellSize));
        return PackCellKey(CellX, CellY, CellZ);
    }

private:
    TMap<uint64, TArray<TElement>> Cells;
    TMap<TElement, uint64> ElementToCell;
    TMap<TElement, FVector> ElementLocations;
    float CellSize = DEFAULT_CELL_SIZE;
    float InvCellSize = 1.0f / DEFAULT_CELL_SIZE;
};
