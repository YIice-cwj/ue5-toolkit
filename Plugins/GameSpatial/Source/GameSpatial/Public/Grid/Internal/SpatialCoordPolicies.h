/**
 * @file SpatialCoordPolicies.h
 * @brief 2D/3D 空间哈希坐标映射、位打包/位解包策略与单元格包围盒值类型（纯无状态内部契约）
 */

#pragma once

#include "CoreMinimal.h"
#include "Math/Vector.h"
#include "Math/Vector2D.h"
#include "Core/SpatialQueryCommon.h"

namespace Game::Spatial::Detail {

    /**
     * @brief 二维离散单元格包围盒值结构体
     */
    struct FCellBounds2D {
        int32 MinX = 0;
        int32 MaxX = 0;
        int32 MinY = 0;
        int32 MaxY = 0;

        /**
         * @brief 检查指定二维单元格坐标是否落于包围盒闭区间内
         * @param CellX 单元格 X 索引
         * @param CellY 单元格 Y 索引
         * @return 落于闭区间内返回 true
         */
        [[nodiscard]] FORCEINLINE bool Contains(int32 CellX, int32 CellY) const noexcept {
            return CellX >= MinX && CellX <= MaxX && CellY >= MinY && CellY <= MaxY;
        }

        /**
         * @brief 检查包围盒是否退化为指定单一中心单元格
         * @param CenterCellX 中心格 X 索引
         * @param CenterCellY 中心格 Y 索引
         * @return 仅覆盖中心单格返回 true
         */
        [[nodiscard]] FORCEINLINE bool IsSingleCell(int32 CenterCellX, int32 CenterCellY) const noexcept {
            return MinX == CenterCellX && MaxX == CenterCellX && MinY == CenterCellY && MaxY == CenterCellY;
        }
    };

    /**
     * @brief 三维离散单元格包围盒值结构体
     */
    struct FCellBounds3D {
        int32 MinX = 0;
        int32 MaxX = 0;
        int32 MinY = 0;
        int32 MaxY = 0;
        int32 MinZ = 0;
        int32 MaxZ = 0;

        /**
         * @brief 检查指定三维单元格坐标是否落于包围盒闭区间内
         * @param CellX 单元格 X 索引
         * @param CellY 单元格 Y 索引
         * @param CellZ 单元格 Z 索引
         * @return 落于闭区间内返回 true
         */
        [[nodiscard]] FORCEINLINE bool Contains(int32 CellX, int32 CellY, int32 CellZ) const noexcept {
            return CellX >= MinX && CellX <= MaxX
                && CellY >= MinY && CellY <= MaxY
                && CellZ >= MinZ && CellZ <= MaxZ;
        }

        /**
         * @brief 检查包围盒是否退化为指定单一中心单元格
         * @param CenterCellX 中心格 X 索引
         * @param CenterCellY 中心格 Y 索引
         * @param CenterCellZ 中心格 Z 索引
         * @return 仅覆盖中心单格返回 true
         */
        [[nodiscard]] FORCEINLINE bool IsSingleCell(int32 CenterCellX, int32 CenterCellY, int32 CenterCellZ) const noexcept {
            return MinX == CenterCellX && MaxX == CenterCellX
                && MinY == CenterCellY && MaxY == CenterCellY
                && MinZ == CenterCellZ && MaxZ == CenterCellZ;
        }
    };

    /**
     * @brief 二维空间哈希坐标映射与 64 位打包/解包策略
     */
    struct FSpatialCoordPolicy2D {
        using FVectorType = FVector2D;

        static constexpr float DEFAULT_CELL_SIZE = 500.0f;
        static constexpr int32 MIN_CELL_COORD = MIN_int32 + 1;
        static constexpr int32 MAX_CELL_COORD = MAX_int32 - 1;
        static constexpr const TCHAR* InvalidCoordinateMessage = TEXT("Non-finite coordinate rejected in 2D spatial grid");
        static constexpr const TCHAR* AlreadyExistsMessage = TEXT("Element already exists in 2D spatial grid");
        static constexpr const TCHAR* NotFoundMessage = TEXT("Element not found in 2D spatial grid");

        /**
         * @brief 将单轴连续世界坐标安全映射并钳制为无溢出 32 位单元格整数索引
         * @param WorldCoord 单轴世界坐标
         * @param InvCellSize 双精度单元格边长倒数
         * @return 钳制后的合法单元格索引
         */
        [[nodiscard]] static FORCEINLINE int32 ComputeCellCoord(double WorldCoord, double InvCellSize) noexcept {
            return Game::Spatial::Private::FloorToClampedCellCoord(WorldCoord, InvCellSize, MIN_CELL_COORD, MAX_CELL_COORD);
        }

        /**
         * @brief 将二维离散整数坐标打包为 64 位无符号单元格键值
         * @param CellX 单元格 X 坐标
         * @param CellY 单元格 Y 坐标
         * @return 打包后的 64 位单元格键
         */
        [[nodiscard]] static constexpr uint64 PackCellKey(int32 CellX, int32 CellY) noexcept {
            const uint64 UX = static_cast<uint64>(static_cast<uint32>(CellX));
            const uint64 UY = static_cast<uint64>(static_cast<uint32>(CellY));
            return (UX << 32) | UY;
        }

        /**
         * @brief 将 64 位无符号单元格键值零浮点开销精准位解包为二维单元格整数坐标
         * @param CellKey 64 位打包单元格键
         * @param OutCellX 输出单元格 X 索引
         * @param OutCellY 输出单元格 Y 索引
         */
        static constexpr void UnpackCellCoord(uint64 CellKey, int32& OutCellX, int32& OutCellY) noexcept {
            OutCellX = static_cast<int32>(static_cast<uint32>(CellKey >> 32));
            OutCellY = static_cast<int32>(static_cast<uint32>(CellKey));
        }

        /**
         * @brief 计算给定连续二维坐标所在的 64 位单元格键值
         * @param Location 二维浮点世界坐标
         * @param InvCellSize 双精度单元格边长倒数
         * @return 对应的 64 位打包单元格键
         */
        [[nodiscard]] static FORCEINLINE uint64 ComputeCellKey(const FVector2D& Location, double InvCellSize) noexcept {
            return PackCellKey(ComputeCellCoord(Location.X, InvCellSize), ComputeCellCoord(Location.Y, InvCellSize));
        }

        /**
         * @brief 计算给定圆心与半径覆盖的二维单元格包围盒
         * @param Center 圆心二维坐标
         * @param Radius 查询半径
         * @param InvCellSize 双精度单元格边长倒数
         * @return 覆盖半径区域的二维离散单元格包围盒
         */
        [[nodiscard]] static FORCEINLINE FCellBounds2D ComputeRadiusBounds(
            const FVector2D& Center,
            double Radius,
            double InvCellSize
        ) noexcept {
            return FCellBounds2D{
                ComputeCellCoord(Center.X - Radius, InvCellSize),
                ComputeCellCoord(Center.X + Radius, InvCellSize),
                ComputeCellCoord(Center.Y - Radius, InvCellSize),
                ComputeCellCoord(Center.Y + Radius, InvCellSize)
            };
        }
    };

    /**
     * @brief 三维空间哈希坐标映射与 21 位偏置 64 位打包/解包策略
     */
    struct FSpatialCoordPolicy3D {
        using FVectorType = FVector;

        static constexpr float DEFAULT_CELL_SIZE = 500.0f;
        static constexpr int32 COORD_BITS = 21;
        static constexpr int64 COORD_OFFSET = 1LL << (COORD_BITS - 1);
        static constexpr uint64 COORD_MASK = (1ULL << COORD_BITS) - 1ULL;
        static constexpr int32 MIN_CELL_COORD = -static_cast<int32>(COORD_OFFSET);
        static constexpr int32 MAX_CELL_COORD = static_cast<int32>(COORD_OFFSET - 1);
        static constexpr const TCHAR* InvalidCoordinateMessage = TEXT("Non-finite coordinate rejected in 3D spatial grid");
        static constexpr const TCHAR* AlreadyExistsMessage = TEXT("Element already exists in 3D spatial grid");
        static constexpr const TCHAR* NotFoundMessage = TEXT("Element not found in 3D spatial grid");

        /**
         * @brief 边界钳制坐标分量至合法 21 位有符号区间
         * @param Value 原始整数坐标
         * @return 处于合法位宽区间的坐标
         */
        [[nodiscard]] static constexpr int32 ClampCoord(int32 Value) noexcept {
            return FMath::Clamp(Value, MIN_CELL_COORD, MAX_CELL_COORD);
        }

        /**
         * @brief 将单轴连续世界坐标安全映射并钳制为合法 21 位有符号单元格整数索引
         * @param WorldCoord 单轴世界坐标
         * @param InvCellSize 双精度单元格边长倒数
         * @return 钳制后的合法单元格索引
         */
        [[nodiscard]] static FORCEINLINE int32 ComputeCellCoord(double WorldCoord, double InvCellSize) noexcept {
            return Game::Spatial::Private::FloorToClampedCellCoord(WorldCoord, InvCellSize, MIN_CELL_COORD, MAX_CELL_COORD);
        }

        /**
         * @brief 将三维离散整数坐标打包为 64 位无符号单元格键值（21 位偏置位压缩）
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

        /**
         * @brief 将 64 位无符号单元格键值零浮点开销精准位解包为三维单元格整数坐标
         * @param CellKey 64 位打包单元格键
         * @param OutCellX 输出单元格 X 索引
         * @param OutCellY 输出单元格 Y 索引
         * @param OutCellZ 输出单元格 Z 索引
         */
        static constexpr void UnpackCellCoord(uint64 CellKey, int32& OutCellX, int32& OutCellY, int32& OutCellZ) noexcept {
            OutCellX = static_cast<int32>(static_cast<int64>((CellKey >> 42) & COORD_MASK) - COORD_OFFSET);
            OutCellY = static_cast<int32>(static_cast<int64>((CellKey >> 21) & COORD_MASK) - COORD_OFFSET);
            OutCellZ = static_cast<int32>(static_cast<int64>(CellKey & COORD_MASK) - COORD_OFFSET);
        }

        /**
         * @brief 计算给定连续三维坐标所在的 64 位单元格键值
         * @param Location 三维浮点世界坐标
         * @param InvCellSize 双精度单元格边长倒数
         * @return 对应的 64 位打包单元格键
         */
        [[nodiscard]] static FORCEINLINE uint64 ComputeCellKey(const FVector& Location, double InvCellSize) noexcept {
            return PackCellKey(
                ComputeCellCoord(Location.X, InvCellSize),
                ComputeCellCoord(Location.Y, InvCellSize),
                ComputeCellCoord(Location.Z, InvCellSize)
            );
        }

        /**
         * @brief 计算给定球心与半径覆盖的三维单元格包围盒
         * @param Center 球心三维坐标
         * @param Radius 查询半径
         * @param InvCellSize 双精度单元格边长倒数
         * @return 覆盖半径区域的三维离散单元格包围盒
         */
        [[nodiscard]] static FORCEINLINE FCellBounds3D ComputeRadiusBounds(
            const FVector& Center,
            double Radius,
            double InvCellSize
        ) noexcept {
            return FCellBounds3D{
                ComputeCellCoord(Center.X - Radius, InvCellSize),
                ComputeCellCoord(Center.X + Radius, InvCellSize),
                ComputeCellCoord(Center.Y - Radius, InvCellSize),
                ComputeCellCoord(Center.Y + Radius, InvCellSize),
                ComputeCellCoord(Center.Z - Radius, InvCellSize),
                ComputeCellCoord(Center.Z + Radius, InvCellSize)
            };
        }
    };

}  // namespace Game::Spatial::Detail
