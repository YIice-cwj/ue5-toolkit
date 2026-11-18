/**
 * @file SpatialQuery3D.h
 * @brief 3D 空间立体几何范围求交与近邻启发式搜索算法（含零堆分配流式查询原语）
 */

#pragma once

#include "CoreMinimal.h"
#include <type_traits>
#include "Math/Box.h"
#include "Math/Vector.h"
#include "Spatial/Containers/TSpatialHashContainer3D.h"

/**
 * @brief 3D 空间几何查询算法工具类
 */
struct FSpatialQuery3D {
    /**
     * @brief 计算一维坐标到单元格区间的最小投影距离
     * @param CenterCoord 中心坐标分量
     * @param CellIndex 单元格整数索引
     * @param CellSize 单元格边长
     * @return 投影最短距离（落在单元格内部时为 0.0）
     */
    [[nodiscard]] static FORCEINLINE double Get1DMinDistanceToCell(double CenterCoord, int32 CellIndex, float CellSize) noexcept {
        const double CellMin = static_cast<double>(CellIndex) * static_cast<double>(CellSize);
        const double CellMax = CellMin + static_cast<double>(CellSize);
        if (CenterCoord < CellMin) {
            return CellMin - CenterCoord;
        }
        if (CenterCoord > CellMax) {
            return CenterCoord - CellMax;
        }
        return 0.0;
    }

    /**
     * @brief 统一调用单参或双参谓词
     * @tparam TPredicate 谓词类型
     * @tparam TElement 元素类型
     * @param Predicate 过滤谓词
     * @param Item 目标元素
     * @param Loc 元素三维坐标
     * @return 谓词判定结果
     */
    template <typename TPredicate, typename TElement>
    [[nodiscard]] static FORCEINLINE bool InvokePredicate(const TPredicate& Predicate, const TElement& Item, const FVector& Loc) {
        if constexpr (std::is_invocable_r_v<bool, TPredicate, const TElement&, const FVector&>) {
            return Predicate(Item, Loc);
        } else {
            return Predicate(Item);
        }
    }

    /**
     * @brief 统一调用单参或双参消费者
     * @tparam TConsumer 消费者类型
     * @tparam TElement 元素类型
     * @param Consumer 消费者回调
     * @param Item 目标元素
     * @param Loc 元素三维坐标
     */
    template <typename TConsumer, typename TElement>
    static FORCEINLINE void InvokeConsumer(TConsumer&& Consumer, const TElement& Item, const FVector& Loc) {
        if constexpr (std::is_invocable_v<TConsumer, const TElement&, const FVector&>) {
            Consumer(Item, Loc);
        } else {
            Consumer(Item);
        }
    }

    /**
     * @brief 检验谓词并在通过时触发消费者
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 谓词类型
     * @tparam TElement 元素类型
     * @param Consumer 消费者
     * @param Predicate 谓词
     * @param Item 目标元素
     * @param Loc 元素三维坐标
     */
    template <typename TConsumer, typename TPredicate, typename TElement>
    static FORCEINLINE void TryConsumeElement(TConsumer&& Consumer, const TPredicate& Predicate, const TElement& Item, const FVector& Loc) {
        if (InvokePredicate(Predicate, Item, Loc)) {
            InvokeConsumer(Forward<TConsumer>(Consumer), Item, Loc);
        }
    }

    /**
     * @brief 三维半径球形流式范围查询（零堆分配，单次遍历就地消费）
     * @tparam TElement 元素类型
     * @tparam TConsumer 消费回调类型 (const TElement&) 或 (const TElement&, const FVector&)
     * @tparam TPredicate 过滤谓词类型
     * @param Container 目标空间容器
     * @param Center 球心坐标
     * @param Radius 查询半径
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TConsumer, typename TPredicate>
    static void QueryRadiusMatching(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Center,
        float Radius,
        TConsumer Consumer,
        TPredicate Predicate
    ) {
        if (Radius <= 0.0f || Container.Num() == 0) {
            return;
        }

        const double RadiusSq = static_cast<double>(Radius) * static_cast<double>(Radius);
        const float InvCellSize = Container.GetInvCellSize();
        const float CellSize = Container.GetCellSize();

        const int32 MinX = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.X - Radius) * InvCellSize));
        const int32 MaxX = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.X + Radius) * InvCellSize));
        const int32 MinY = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.Y - Radius) * InvCellSize));
        const int32 MaxY = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.Y + Radius) * InvCellSize));
        const int32 MinZ = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.Z - Radius) * InvCellSize));
        const int32 MaxZ = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.Z + Radius) * InvCellSize));

        for (int32 X = MinX; X <= MaxX; ++X) {
            const double Dx = Get1DMinDistanceToCell(Center.X, X, CellSize);
            const double DxSq = Dx * Dx;
            if (DxSq > RadiusSq) {
                continue;
            }

            for (int32 Y = MinY; Y <= MaxY; ++Y) {
                const double Dy = Get1DMinDistanceToCell(Center.Y, Y, CellSize);
                if (DxSq + Dy * Dy > RadiusSq) {
                    continue;
                }

                for (int32 Z = MinZ; Z <= MaxZ; ++Z) {
                    const double Dz = Get1DMinDistanceToCell(Center.Z, Z, CellSize);
                    if (DxSq + Dy * Dy + Dz * Dz > RadiusSq) {
                        continue;
                    }

                    Container.ForEachElementInCell(Container.PackCellKey(X, Y, Z), [&Center, RadiusSq, &Consumer, &Predicate](const TElement& Item, const FVector& Loc) {
                        if (FVector::DistSquared(Center, Loc) <= RadiusSq) {
                            TryConsumeElement(Consumer, Predicate, Item, Loc);
                        }
                    });
                }
            }
        }
    }

    /**
     * @brief 三维半径球形范围查询（带自定义过滤谓词）
     * @tparam TElement 元素类型
     * @tparam TPredicate 谓词类型
     * @param Container 目标空间容器
     * @param Center 球心坐标
     * @param Radius 查询半径
     * @param OutResults 输出落入球内的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate>
    static void QueryRadius(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Center,
        float Radius,
        TArray<TElement>& OutResults,
        TPredicate Predicate
    ) {
        OutResults.Reset();
        QueryRadiusMatching(Container, Center, Radius, [&OutResults](const TElement& Item) {
            OutResults.Add(Item);
        }, MoveTemp(Predicate));
    }

    /**
     * @brief 轴对齐三维包围盒 AABB 流式范围查询（零堆分配，单次遍历就地消费）
     * @tparam TElement 元素类型
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型
     * @param Container 目标空间容器
     * @param Box 查询轴对齐包围盒
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TConsumer, typename TPredicate>
    static void QueryAABBMatching(
        const TSpatialHashContainer3D<TElement>& Container,
        const FBox& Box,
        TConsumer Consumer,
        TPredicate Predicate
    ) {
        if (!Box.IsValid || Container.Num() == 0) {
            return;
        }

        const float InvCellSize = Container.GetInvCellSize();
        const int32 MinX = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt(Box.Min.X * InvCellSize));
        const int32 MaxX = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt(Box.Max.X * InvCellSize));
        const int32 MinY = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt(Box.Min.Y * InvCellSize));
        const int32 MaxY = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt(Box.Max.Y * InvCellSize));
        const int32 MinZ = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt(Box.Min.Z * InvCellSize));
        const int32 MaxZ = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt(Box.Max.Z * InvCellSize));

        Container.ForEachCellInBox(MinX, MaxX, MinY, MaxY, MinZ, MaxZ, [&Container, &Box, &Consumer, &Predicate](uint64 Key) {
            Container.ForEachElementInCell(Key, [&Box, &Consumer, &Predicate](const TElement& Item, const FVector& Loc) {
                if (Loc.X >= Box.Min.X && Loc.X <= Box.Max.X &&
                    Loc.Y >= Box.Min.Y && Loc.Y <= Box.Max.Y &&
                    Loc.Z >= Box.Min.Z && Loc.Z <= Box.Max.Z) {
                    TryConsumeElement(Consumer, Predicate, Item, Loc);
                }
            });
        });
    }

    /**
     * @brief 轴对齐三维包围盒 AABB 范围查询（带自定义过滤谓词）
     * @tparam TElement 元素类型
     * @tparam TPredicate 谓词类型
     * @param Container 目标空间容器
     * @param Box 查询轴对齐包围盒
     * @param OutResults 输出落入包围盒内的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate>
    static void QueryAABB(
        const TSpatialHashContainer3D<TElement>& Container,
        const FBox& Box,
        TArray<TElement>& OutResults,
        TPredicate Predicate
    ) {
        OutResults.Reset();
        QueryAABBMatching(Container, Box, [&OutResults](const TElement& Item) {
            OutResults.Add(Item);
        }, MoveTemp(Predicate));
    }

    /**
     * @brief 三维直立圆柱体流式范围查询（零堆分配，单次遍历就地消费）
     * @tparam TElement 元素类型
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型
     * @param Container 目标空间容器
     * @param Center 圆柱中心坐标
     * @param Radius 水平截面半径
     * @param HalfHeight 垂直半高
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TConsumer, typename TPredicate>
    static void QueryCylinderMatching(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Center,
        float Radius,
        float HalfHeight,
        TConsumer Consumer,
        TPredicate Predicate
    ) {
        if (Radius <= 0.0f || HalfHeight <= 0.0f || Container.Num() == 0) {
            return;
        }

        const double RadiusSq = static_cast<double>(Radius) * static_cast<double>(Radius);
        const float InvCellSize = Container.GetInvCellSize();
        const float CellSize = Container.GetCellSize();

        const int32 MinX = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.X - Radius) * InvCellSize));
        const int32 MaxX = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.X + Radius) * InvCellSize));
        const int32 MinY = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.Y - Radius) * InvCellSize));
        const int32 MaxY = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.Y + Radius) * InvCellSize));
        const int32 MinZ = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.Z - HalfHeight) * InvCellSize));
        const int32 MaxZ = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.Z + HalfHeight) * InvCellSize));

        for (int32 X = MinX; X <= MaxX; ++X) {
            const double Dx = Get1DMinDistanceToCell(Center.X, X, CellSize);
            const double DxSq = Dx * Dx;
            if (DxSq > RadiusSq) {
                continue;
            }

            for (int32 Y = MinY; Y <= MaxY; ++Y) {
                const double Dy = Get1DMinDistanceToCell(Center.Y, Y, CellSize);
                if (DxSq + Dy * Dy > RadiusSq) {
                    continue;
                }

                for (int32 Z = MinZ; Z <= MaxZ; ++Z) {
                    const double Dz = Get1DMinDistanceToCell(Center.Z, Z, CellSize);
                    if (Dz > static_cast<double>(HalfHeight)) {
                        continue;
                    }

                    Container.ForEachElementInCell(Container.PackCellKey(X, Y, Z), [&Center, RadiusSq, HalfHeight, &Consumer, &Predicate](const TElement& Item, const FVector& Loc) {
                        if (FMath::Abs(Loc.Z - Center.Z) <= static_cast<double>(HalfHeight)) {
                            const double DistX = Loc.X - Center.X;
                            const double DistY = Loc.Y - Center.Y;
                            if ((DistX * DistX + DistY * DistY) <= RadiusSq) {
                                TryConsumeElement(Consumer, Predicate, Item, Loc);
                            }
                        }
                    });
                }
            }
        }
    }

    /**
     * @brief 三维直立圆柱体范围查询（带自定义过滤谓词）
     * @tparam TElement 元素类型
     * @tparam TPredicate 谓词类型
     * @param Container 目标空间容器
     * @param Center 圆柱中心坐标
     * @param Radius 水平截面半径
     * @param HalfHeight 垂直半高
     * @param OutResults 输出落入圆柱内的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate>
    static void QueryCylinder(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Center,
        float Radius,
        float HalfHeight,
        TArray<TElement>& OutResults,
        TPredicate Predicate
    ) {
        OutResults.Reset();
        QueryCylinderMatching(Container, Center, Radius, HalfHeight, [&OutResults](const TElement& Item) {
            OutResults.Add(Item);
        }, MoveTemp(Predicate));
    }

    /**
     * @brief 三维空间视锥/前向光锥流式范围查询（零堆分配，单次遍历就地消费）
     * @tparam TElement 元素类型
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型
     * @param Container 目标空间容器
     * @param Apex 锥体顶点世界坐标
     * @param Direction 锥体中心对称朝向
     * @param Radius 锥体最大作用距离
     * @param HalfAngleDegrees 锥体半顶角（度数）
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TConsumer, typename TPredicate>
    static void QueryConeMatching(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Apex,
        const FVector& Direction,
        float Radius,
        float HalfAngleDegrees,
        TConsumer Consumer,
        TPredicate Predicate
    ) {
        if (Radius <= 0.0f || HalfAngleDegrees <= 0.0f || Container.Num() == 0) {
            return;
        }

        if (HalfAngleDegrees >= 180.0f) {
            QueryRadiusMatching(Container, Apex, Radius, MoveTemp(Consumer), MoveTemp(Predicate));
            return;
        }

        const double RadiusSq = static_cast<double>(Radius) * static_cast<double>(Radius);
        const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(HalfAngleDegrees, 0.0f, 180.0f)));
        const FVector SafeDirection = Direction.GetSafeNormal();

        const float InvCellSize = Container.GetInvCellSize();
        const float CellSize = Container.GetCellSize();

        const int32 MinX = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Apex.X - Radius) * InvCellSize));
        const int32 MaxX = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Apex.X + Radius) * InvCellSize));
        const int32 MinY = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Apex.Y - Radius) * InvCellSize));
        const int32 MaxY = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Apex.Y + Radius) * InvCellSize));
        const int32 MinZ = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Apex.Z - Radius) * InvCellSize));
        const int32 MaxZ = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Apex.Z + Radius) * InvCellSize));

        for (int32 X = MinX; X <= MaxX; ++X) {
            const double Dx = Get1DMinDistanceToCell(Apex.X, X, CellSize);
            const double DxSq = Dx * Dx;
            if (DxSq > RadiusSq) {
                continue;
            }

            for (int32 Y = MinY; Y <= MaxY; ++Y) {
                const double Dy = Get1DMinDistanceToCell(Apex.Y, Y, CellSize);
                if (DxSq + Dy * Dy > RadiusSq) {
                    continue;
                }

                for (int32 Z = MinZ; Z <= MaxZ; ++Z) {
                    const double Dz = Get1DMinDistanceToCell(Apex.Z, Z, CellSize);
                    if (DxSq + Dy * Dy + Dz * Dz > RadiusSq) {
                        continue;
                    }

                    Container.ForEachElementInCell(Container.PackCellKey(X, Y, Z), [&Apex, &SafeDirection, RadiusSq, CosHalfAngle, &Consumer, &Predicate](const TElement& Item, const FVector& Loc) {
                        const FVector ToLoc = Loc - Apex;
                        const double DistSq = ToLoc.SizeSquared();
                        if (DistSq <= RadiusSq) {
                            const double Dist = FMath::Sqrt(DistSq);
                            const float Dot = (Dist > KINDA_SMALL_NUMBER) ? static_cast<float>(FVector::DotProduct(ToLoc / Dist, SafeDirection)) : 1.0f;
                            if (Dot >= CosHalfAngle) {
                                TryConsumeElement(Consumer, Predicate, Item, Loc);
                            }
                        }
                    });
                }
            }
        }
    }

    /**
     * @brief 三维空间视锥/前向光锥范围查询（带自定义过滤谓词）
     * @tparam TElement 元素类型
     * @tparam TPredicate 谓词类型
     * @param Container 目标空间容器
     * @param Apex 锥体顶点世界坐标
     * @param Direction 锥体中心对称朝向
     * @param Radius 锥体最大作用距离
     * @param HalfAngleDegrees 锥体半顶角（度数）
     * @param OutResults 输出落入锥体内的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate>
    static void QueryCone(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Apex,
        const FVector& Direction,
        float Radius,
        float HalfAngleDegrees,
        TArray<TElement>& OutResults,
        TPredicate Predicate
    ) {
        OutResults.Reset();
        QueryConeMatching(Container, Apex, Direction, Radius, HalfAngleDegrees, [&OutResults](const TElement& Item) {
            OutResults.Add(Item);
        }, MoveTemp(Predicate));
    }

    /**
     * @brief 查找距离给定中心点最近的目标元素（三级轴向包围盒动态剪枝与谓词过滤）
     * @tparam TElement 元素类型
     * @tparam TPredicate 谓词类型
     * @param Container 目标底层空间哈希容器
     * @param Center 中心点三维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param OutNearest 输出最近元素
     * @param Predicate 过滤谓词
     * @return 存在有效元素返回 true，指定半径内无元素返回 false
     */
    template <typename TElement, typename TPredicate>
    static bool QueryNearest(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Center,
        float MaxSearchRadius,
        TElement& OutNearest,
        TPredicate Predicate
    ) {
        if (MaxSearchRadius <= 0.0f || Container.Num() == 0) {
            return false;
        }

        double ClosestDistSq = static_cast<double>(MaxSearchRadius) * static_cast<double>(MaxSearchRadius);
        const TElement* BestCandidate = nullptr;

        const float InvCellSize = Container.GetInvCellSize();
        const float CellSize = Container.GetCellSize();

        const int32 CenterCellX = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt(Center.X * InvCellSize));
        const int32 CenterCellY = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt(Center.Y * InvCellSize));
        const int32 CenterCellZ = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt(Center.Z * InvCellSize));

        Container.ForEachElementInCell(Container.PackCellKey(CenterCellX, CenterCellY, CenterCellZ), [&Center, &ClosestDistSq, &BestCandidate, &Predicate](const TElement& Item, const FVector& Loc) {
            if (InvokePredicate(Predicate, Item, Loc)) {
                const double DistSq = FVector::DistSquared(Center, Loc);
                if (DistSq <= ClosestDistSq) {
                    ClosestDistSq = DistSq;
                    BestCandidate = &Item;
                }
            }
        });

        const double SearchRadius = FMath::Sqrt(ClosestDistSq);
        const int32 MinX = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.X - SearchRadius) * InvCellSize));
        const int32 MaxX = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.X + SearchRadius) * InvCellSize));
        const int32 MinY = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.Y - SearchRadius) * InvCellSize));
        const int32 MaxY = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.Y + SearchRadius) * InvCellSize));
        const int32 MinZ = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.Z - SearchRadius) * InvCellSize));
        const int32 MaxZ = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.Z + SearchRadius) * InvCellSize));

        for (int32 X = MinX; X <= MaxX; ++X) {
            const double Dx = Get1DMinDistanceToCell(Center.X, X, CellSize);
            const double DxSq = Dx * Dx;
            if (DxSq > ClosestDistSq) {
                continue;
            }

            for (int32 Y = MinY; Y <= MaxY; ++Y) {
                const double Dy = Get1DMinDistanceToCell(Center.Y, Y, CellSize);
                if (DxSq + Dy * Dy > ClosestDistSq) {
                    continue;
                }

                for (int32 Z = MinZ; Z <= MaxZ; ++Z) {
                    if (X == CenterCellX && Y == CenterCellY && Z == CenterCellZ) {
                        continue;
                    }

                    const double Dz = Get1DMinDistanceToCell(Center.Z, Z, CellSize);
                    if (DxSq + Dy * Dy + Dz * Dz > ClosestDistSq) {
                        continue;
                    }

                    Container.ForEachElementInCell(Container.PackCellKey(X, Y, Z), [&Center, &ClosestDistSq, &BestCandidate, &Predicate](const TElement& Item, const FVector& Loc) {
                        if (InvokePredicate(Predicate, Item, Loc)) {
                            const double DistSq = FVector::DistSquared(Center, Loc);
                            if (DistSq <= ClosestDistSq) {
                                ClosestDistSq = DistSq;
                                BestCandidate = &Item;
                            }
                        }
                    });
                }
            }
        }

        if (BestCandidate == nullptr) {
            return false;
        }
        OutNearest = *BestCandidate;
        return true;
    }

    /**
     * @brief 查找距离给定中心点最近的 K 个目标元素（三维动态剪枝与谓词过滤，按距离升序返回）
     * @tparam TElement 元素类型
     * @tparam TPredicate 谓词类型
     * @param Container 目标底层空间哈希容器
     * @param Center 中心点三维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param K 期望获取的最大近邻数量（必须大于 0）
     * @param OutNearestList 输出最近的 K 个元素列表（按距离由近至远升序排列）
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate>
    static void QueryKNearest(
        const TSpatialHashContainer3D<TElement>& Container,
        const FVector& Center,
        float MaxSearchRadius,
        int32 K,
        TArray<TElement>& OutNearestList,
        TPredicate Predicate
    ) {
        OutNearestList.Reset();
        if (K <= 0 || MaxSearchRadius <= 0.0f || Container.Num() == 0) {
            return;
        }

        struct FKCandidate {
            TElement Element;
            double DistSq;
        };

        TArray<FKCandidate> Heap;
        Heap.Reserve(K);

        auto TryAddCandidate = [&Heap, K, &Center, &Predicate](const TElement& Item, const FVector& Loc, double CurrentMaxDistSq) -> double {
            if (!InvokePredicate(Predicate, Item, Loc)) {
                return CurrentMaxDistSq;
            }

            const double DistSq = FVector::DistSquared(Center, Loc);
            if (DistSq > CurrentMaxDistSq) {
                return CurrentMaxDistSq;
            }

            int32 InsertIdx = 0;
            while (InsertIdx < Heap.Num() && Heap[InsertIdx].DistSq < DistSq) {
                ++InsertIdx;
            }

            if (Heap.Num() < K) {
                Heap.Insert(FKCandidate{Item, DistSq}, InsertIdx);
            } else if (InsertIdx < K) {
                Heap.Insert(FKCandidate{Item, DistSq}, InsertIdx);
                Heap.Pop();
            }

            return (Heap.Num() == K) ? Heap.Last().DistSq : CurrentMaxDistSq;
        };

        double MaxValidDistSq = static_cast<double>(MaxSearchRadius) * static_cast<double>(MaxSearchRadius);
        const float InvCellSize = Container.GetInvCellSize();
        const float CellSize = Container.GetCellSize();

        const int32 CenterCellX = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt(Center.X * InvCellSize));
        const int32 CenterCellY = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt(Center.Y * InvCellSize));
        const int32 CenterCellZ = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt(Center.Z * InvCellSize));

        Container.ForEachElementInCell(Container.PackCellKey(CenterCellX, CenterCellY, CenterCellZ), [&TryAddCandidate, &MaxValidDistSq](const TElement& Item, const FVector& Loc) {
            MaxValidDistSq = TryAddCandidate(Item, Loc, MaxValidDistSq);
        });

        const double SearchRadius = FMath::Sqrt(MaxValidDistSq);
        const int32 MinX = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.X - SearchRadius) * InvCellSize));
        const int32 MaxX = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.X + SearchRadius) * InvCellSize));
        const int32 MinY = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.Y - SearchRadius) * InvCellSize));
        const int32 MaxY = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.Y + SearchRadius) * InvCellSize));
        const int32 MinZ = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.Z - SearchRadius) * InvCellSize));
        const int32 MaxZ = TSpatialHashContainer3D<TElement>::ClampCoord(FMath::FloorToInt((Center.Z + SearchRadius) * InvCellSize));

        for (int32 X = MinX; X <= MaxX; ++X) {
            const double Dx = Get1DMinDistanceToCell(Center.X, X, CellSize);
            const double DxSq = Dx * Dx;
            if (DxSq > MaxValidDistSq) {
                continue;
            }

            for (int32 Y = MinY; Y <= MaxY; ++Y) {
                const double Dy = Get1DMinDistanceToCell(Center.Y, Y, CellSize);
                if (DxSq + Dy * Dy > MaxValidDistSq) {
                    continue;
                }

                for (int32 Z = MinZ; Z <= MaxZ; ++Z) {
                    if (X == CenterCellX && Y == CenterCellY && Z == CenterCellZ) {
                        continue;
                    }

                    const double Dz = Get1DMinDistanceToCell(Center.Z, Z, CellSize);
                    if (DxSq + Dy * Dy + Dz * Dz > MaxValidDistSq) {
                        continue;
                    }

                    Container.ForEachElementInCell(Container.PackCellKey(X, Y, Z), [&TryAddCandidate, &MaxValidDistSq](const TElement& Item, const FVector& Loc) {
                        MaxValidDistSq = TryAddCandidate(Item, Loc, MaxValidDistSq);
                    });
                }
            }
        }

        OutNearestList.Reserve(Heap.Num());
        for (const FKCandidate& Cand : Heap) {
            OutNearestList.Add(Cand.Element);
        }
    }
};
