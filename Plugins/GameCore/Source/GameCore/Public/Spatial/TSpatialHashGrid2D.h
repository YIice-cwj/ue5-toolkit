/**
 * @file TSpatialHashGrid2D.h
 * @brief 纯逻辑轻量 2D 均匀空间哈希网格模板（脱离物理引擎，纳秒级范围检索与近邻查询）
 */

#pragma once

#include "CoreMinimal.h"
#include "Math/Box2D.h"
#include "Math/Vector2D.h"

/**
 * @brief 2D 纯逻辑均匀空间哈希网格容器模板
 * @tparam TElement 空间对象键或句柄类型（须支持 GetTypeHash 与 operator==）
 */
template <typename TElement>
class TSpatialHashGrid2D {
public:
    static constexpr float DEFAULT_CELL_SIZE = 500.0f;

public:
    /**
     * @brief 构造空间哈希网格并指定单元格跨度
     * @param InCellSize 单元格边长（必须大于 1.0f）
     */
    explicit TSpatialHashGrid2D(float InCellSize = DEFAULT_CELL_SIZE) noexcept
        : CellSize(InCellSize > 1.0f ? InCellSize : DEFAULT_CELL_SIZE)
        , InvCellSize(1.0f / (InCellSize > 1.0f ? InCellSize : DEFAULT_CELL_SIZE)) {}

    /**
     * @brief 向网格中插入新元素及其空间坐标
     * @param Element 目标元素标识
     * @param Location 空间二维坐标
     * @return 插入成功返回 true，若已存在同名元素则返回 false
     */
    bool Insert(const TElement& Element, const FVector2D& Location) {
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
     * @brief 更新已有元素的空间坐标（若未跨越单元格边界则无移动开销）
     * @param Element 目标元素标识
     * @param NewLocation 新空间二维坐标
     * @return 元素存在且更新成功返回 true，不存在返回 false
     */
    bool Update(const TElement& Element, const FVector2D& NewLocation) {
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
     * @brief 获取目标元素的当前坐标
     * @param Element 待查询的元素标识
     * @param OutLocation 输出空间二维坐标
     * @return 找到有效元素返回 true
     */
    bool GetLocation(const TElement& Element, FVector2D& OutLocation) const {
        if (const FVector2D* Found = ElementLocations.Find(Element)) {
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
     * @brief 半径球形/圆形范围查询
     * @param Center 圆心坐标
     * @param Radius 查询半径（须大于 0）
     * @param OutResults 输出落入半径内的元素集合
     */
    void QueryRadius(const FVector2D& Center, float Radius, TArray<TElement>& OutResults) const {
        OutResults.Reset();
        if (Radius <= 0.0f || ElementToCell.IsEmpty()) {
            return;
        }

        const float RadiusSq = Radius * Radius;
        const int32 MinX = FMath::FloorToInt((Center.X - Radius) * InvCellSize);
        const int32 MaxX = FMath::FloorToInt((Center.X + Radius) * InvCellSize);
        const int32 MinY = FMath::FloorToInt((Center.Y - Radius) * InvCellSize);
        const int32 MaxY = FMath::FloorToInt((Center.Y + Radius) * InvCellSize);

        ForEachCellInBox(MinX, MaxX, MinY, MaxY, [this, &Center, RadiusSq, &OutResults](uint64 Key) {
            ForEachElementInCell(Key, [&Center, RadiusSq, &OutResults](const TElement& Item, const FVector2D& Loc) {
                if (FVector2D::DistSquared(Center, Loc) <= RadiusSq) {
                    OutResults.Add(Item);
                }
            });
        });
    }

    /**
     * @brief 轴对齐矩形 AABB 范围查询
     * @param Box 查询矩形包围盒
     * @param OutResults 输出落入矩形内的元素集合
     */
    void QueryAABB(const FBox2D& Box, TArray<TElement>& OutResults) const {
        OutResults.Reset();
        if (!Box.bIsValid || ElementToCell.IsEmpty()) {
            return;
        }

        const int32 MinX = FMath::FloorToInt(Box.Min.X * InvCellSize);
        const int32 MaxX = FMath::FloorToInt(Box.Max.X * InvCellSize);
        const int32 MinY = FMath::FloorToInt(Box.Min.Y * InvCellSize);
        const int32 MaxY = FMath::FloorToInt(Box.Max.Y * InvCellSize);

        ForEachCellInBox(MinX, MaxX, MinY, MaxY, [this, &Box, &OutResults](uint64 Key) {
            ForEachElementInCell(Key, [&Box, &OutResults](const TElement& Item, const FVector2D& Loc) {
                if (Loc.X >= Box.Min.X && Loc.X <= Box.Max.X &&
                    Loc.Y >= Box.Min.Y && Loc.Y <= Box.Max.Y) {
                    OutResults.Add(Item);
                }
            });
        });
    }

    /**
     * @brief 查找距离给定中心点最近的目标元素
     * @param Center 中心点二维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param OutNearest 输出最近元素
     * @return 存在有效元素返回 true，指定半径内无元素返回 false
     */
    bool QueryNearest(const FVector2D& Center, float MaxSearchRadius, TElement& OutNearest) const {
        TArray<TElement> Candidates;
        QueryRadius(Center, MaxSearchRadius, Candidates);
        if (Candidates.IsEmpty()) {
            return false;
        }

        float ClosestDistSq = TNumericLimits<float>::Max();
        const TElement* BestCandidate = nullptr;

        for (const TElement& Item : Candidates) {
            const FVector2D* Loc = ElementLocations.Find(Item);
            if (Loc == nullptr) {
                continue;
            }
            const float DistSq = FVector2D::DistSquared(Center, *Loc);
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
    void ForEachCellInBox(int32 MinX, int32 MaxX, int32 MinY, int32 MaxY, TPredicate Predicate) const {
        for (int32 X = MinX; X <= MaxX; ++X) {
            for (int32 Y = MinY; Y <= MaxY; ++Y) {
                Predicate(PackCellKey(X, Y));
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
            const FVector2D* Loc = ElementLocations.Find(Item);
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

    [[nodiscard]] static constexpr uint64 PackCellKey(int32 CellX, int32 CellY) noexcept {
        const uint64 UX = static_cast<uint64>(static_cast<uint32>(CellX));
        const uint64 UY = static_cast<uint64>(static_cast<uint32>(CellY));
        return (UX << 32) | UY;
    }

    [[nodiscard]] uint64 ComputeCellKey(const FVector2D& Location) const noexcept {
        const int32 CellX = FMath::FloorToInt(Location.X * InvCellSize);
        const int32 CellY = FMath::FloorToInt(Location.Y * InvCellSize);
        return PackCellKey(CellX, CellY);
    }

private:
    TMap<uint64, TArray<TElement>> Cells;
    TMap<TElement, uint64> ElementToCell;
    TMap<TElement, FVector2D> ElementLocations;
    float CellSize = DEFAULT_CELL_SIZE;
    float InvCellSize = 1.0f / DEFAULT_CELL_SIZE;
};
