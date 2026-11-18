/**
 * @file SpatialQuery2D.h
 * @brief 2D 空间几何范围求交与近邻启发式搜索算法（含零堆分配流式查询原语）
 */

#pragma once

#include "CoreMinimal.h"
#include <type_traits>
#include "Math/Box2D.h"
#include "Math/Vector2D.h"
#include "Spatial/Containers/TSpatialHashContainer2D.h"

/**
 * @brief 2D 空间几何查询算法工具类
 */
struct FSpatialQuery2D {
    /**
     * @brief 计算一维坐标到单元格区间的最小投影距离
     * @param CenterCoord 中心坐标分量
     * @param CellIndex 单元格整数索引
     * @param CellSize 单元格边长
     * @return 投影最短距离（落在单元格内部时为 0.0f）
     */
    [[nodiscard]] static FORCEINLINE float Get1DMinDistanceToCell(float CenterCoord, int32 CellIndex, float CellSize) noexcept {
        const float CellMin = static_cast<float>(CellIndex) * CellSize;
        const float CellMax = CellMin + CellSize;
        if (CenterCoord < CellMin) {
            return CellMin - CenterCoord;
        }
        if (CenterCoord > CellMax) {
            return CenterCoord - CellMax;
        }
        return 0.0f;
    }

    /**
     * @brief 统一调用单参或双参谓词
     * @tparam TPredicate 谓词类型
     * @tparam TElement 元素类型
     * @param Predicate 过滤谓词
     * @param Item 目标元素
     * @param Loc 元素坐标
     * @return 谓词判定结果
     */
    template <typename TPredicate, typename TElement>
    [[nodiscard]] static FORCEINLINE bool InvokePredicate(const TPredicate& Predicate, const TElement& Item, const FVector2D& Loc) {
        if constexpr (std::is_invocable_r_v<bool, TPredicate, const TElement&, const FVector2D&>) {
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
     * @param Loc 元素坐标
     */
    template <typename TConsumer, typename TElement>
    static FORCEINLINE void InvokeConsumer(TConsumer&& Consumer, const TElement& Item, const FVector2D& Loc) {
        if constexpr (std::is_invocable_v<TConsumer, const TElement&, const FVector2D&>) {
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
     * @param Loc 元素坐标
     */
    template <typename TConsumer, typename TPredicate, typename TElement>
    static FORCEINLINE void TryConsumeElement(TConsumer&& Consumer, const TPredicate& Predicate, const TElement& Item, const FVector2D& Loc) {
        if (InvokePredicate(Predicate, Item, Loc)) {
            InvokeConsumer(Forward<TConsumer>(Consumer), Item, Loc);
        }
    }

    /**
     * @brief 半径圆形流式范围查询（零堆分配，单次遍历就地消费）
     * @tparam TElement 元素类型
     * @tparam TConsumer 消费回调类型 (const TElement&) 或 (const TElement&, const FVector2D&)
     * @tparam TPredicate 过滤谓词类型
     * @param Container 目标空间容器
     * @param Center 圆心坐标
     * @param Radius 查询半径
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TConsumer, typename TPredicate>
    static void QueryRadiusMatching(
        const TSpatialHashContainer2D<TElement>& Container,
        const FVector2D& Center,
        float Radius,
        TConsumer Consumer,
        TPredicate Predicate
    ) {
        if (Radius <= 0.0f || Container.Num() == 0) {
            return;
        }

        const float RadiusSq = Radius * Radius;
        const float InvCellSize = Container.GetInvCellSize();
        const float CellSize = Container.GetCellSize();

        const int32 MinX = FMath::FloorToInt((Center.X - Radius) * InvCellSize);
        const int32 MaxX = FMath::FloorToInt((Center.X + Radius) * InvCellSize);
        const int32 MinY = FMath::FloorToInt((Center.Y - Radius) * InvCellSize);
        const int32 MaxY = FMath::FloorToInt((Center.Y + Radius) * InvCellSize);

        for (int32 X = MinX; X <= MaxX; ++X) {
            const float Dx = Get1DMinDistanceToCell(Center.X, X, CellSize);
            const float DxSq = Dx * Dx;
            if (DxSq > RadiusSq) {
                continue;
            }

            for (int32 Y = MinY; Y <= MaxY; ++Y) {
                const float Dy = Get1DMinDistanceToCell(Center.Y, Y, CellSize);
                if (DxSq + Dy * Dy > RadiusSq) {
                    continue;
                }

                Container.ForEachElementInCell(Container.PackCellKey(X, Y), [&Center, RadiusSq, &Consumer, &Predicate](const TElement& Item, const FVector2D& Loc) {
                    if (FVector2D::DistSquared(Center, Loc) <= RadiusSq) {
                        TryConsumeElement(Consumer, Predicate, Item, Loc);
                    }
                });
            }
        }
    }

    /**
     * @brief 半径圆形范围查询（带自定义过滤谓词）
     * @tparam TElement 元素类型
     * @tparam TPredicate 谓词类型
     * @param Container 目标空间容器
     * @param Center 圆心坐标
     * @param Radius 查询半径
     * @param OutResults 输出落入圆形内的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate>
    static void QueryRadius(
        const TSpatialHashContainer2D<TElement>& Container,
        const FVector2D& Center,
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
     * @brief 轴对齐矩形 AABB 流式范围查询（零堆分配，单次遍历就地消费）
     * @tparam TElement 元素类型
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型
     * @param Container 目标空间容器
     * @param Box 查询矩形包围盒
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TConsumer, typename TPredicate>
    static void QueryAABBMatching(
        const TSpatialHashContainer2D<TElement>& Container,
        const FBox2D& Box,
        TConsumer Consumer,
        TPredicate Predicate
    ) {
        if (!Box.bIsValid || Container.Num() == 0) {
            return;
        }

        const float InvCellSize = Container.GetInvCellSize();
        const int32 MinX = FMath::FloorToInt(Box.Min.X * InvCellSize);
        const int32 MaxX = FMath::FloorToInt(Box.Max.X * InvCellSize);
        const int32 MinY = FMath::FloorToInt(Box.Min.Y * InvCellSize);
        const int32 MaxY = FMath::FloorToInt(Box.Max.Y * InvCellSize);

        Container.ForEachCellInBox(MinX, MaxX, MinY, MaxY, [&Container, &Box, &Consumer, &Predicate](uint64 Key) {
            Container.ForEachElementInCell(Key, [&Box, &Consumer, &Predicate](const TElement& Item, const FVector2D& Loc) {
                if (Loc.X >= Box.Min.X && Loc.X <= Box.Max.X &&
                    Loc.Y >= Box.Min.Y && Loc.Y <= Box.Max.Y) {
                    TryConsumeElement(Consumer, Predicate, Item, Loc);
                }
            });
        });
    }

    /**
     * @brief 轴对齐矩形 AABB 范围查询（带自定义过滤谓词）
     * @tparam TElement 元素类型
     * @tparam TPredicate 谓词类型
     * @param Container 目标空间容器
     * @param Box 查询矩形包围盒
     * @param OutResults 输出落入矩形内的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate>
    static void QueryAABB(
        const TSpatialHashContainer2D<TElement>& Container,
        const FBox2D& Box,
        TArray<TElement>& OutResults,
        TPredicate Predicate
    ) {
        OutResults.Reset();
        QueryAABBMatching(Container, Box, [&OutResults](const TElement& Item) {
            OutResults.Add(Item);
        }, MoveTemp(Predicate));
    }

    /**
     * @brief 2D 扇形流式范围查询（零堆分配，单次遍历就地消费）
     * @tparam TElement 元素类型
     * @tparam TConsumer 消费回调类型
     * @tparam TPredicate 过滤谓词类型
     * @param Container 目标空间容器
     * @param Center 扇形圆心
     * @param Forward 扇形基准朝向
     * @param Radius 扇形半径
     * @param HalfAngleDegrees 扇形半角（度数）
     * @param Consumer 命中元素就地消费者
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TConsumer, typename TPredicate>
    static void QuerySectorMatching(
        const TSpatialHashContainer2D<TElement>& Container,
        const FVector2D& Center,
        const FVector2D& Forward,
        float Radius,
        float HalfAngleDegrees,
        TConsumer Consumer,
        TPredicate Predicate
    ) {
        if (Radius <= 0.0f || HalfAngleDegrees <= 0.0f || Container.Num() == 0) {
            return;
        }

        if (HalfAngleDegrees >= 180.0f) {
            QueryRadiusMatching(Container, Center, Radius, MoveTemp(Consumer), MoveTemp(Predicate));
            return;
        }

        const float RadiusSq = Radius * Radius;
        const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(HalfAngleDegrees, 0.0f, 180.0f)));
        const FVector2D SafeForward = Forward.GetSafeNormal();

        const float InvCellSize = Container.GetInvCellSize();
        const float CellSize = Container.GetCellSize();

        const int32 MinX = FMath::FloorToInt((Center.X - Radius) * InvCellSize);
        const int32 MaxX = FMath::FloorToInt((Center.X + Radius) * InvCellSize);
        const int32 MinY = FMath::FloorToInt((Center.Y - Radius) * InvCellSize);
        const int32 MaxY = FMath::FloorToInt((Center.Y + Radius) * InvCellSize);

        for (int32 X = MinX; X <= MaxX; ++X) {
            const float Dx = Get1DMinDistanceToCell(Center.X, X, CellSize);
            const float DxSq = Dx * Dx;
            if (DxSq > RadiusSq) {
                continue;
            }

            for (int32 Y = MinY; Y <= MaxY; ++Y) {
                const float Dy = Get1DMinDistanceToCell(Center.Y, Y, CellSize);
                if (DxSq + Dy * Dy > RadiusSq) {
                    continue;
                }

                Container.ForEachElementInCell(Container.PackCellKey(X, Y), [&Center, &SafeForward, RadiusSq, CosHalfAngle, &Consumer, &Predicate](const TElement& Item, const FVector2D& Loc) {
                    const FVector2D ToLoc = Loc - Center;
                    const float DistSq = ToLoc.SizeSquared();
                    if (DistSq <= RadiusSq) {
                        const float Dist = FMath::Sqrt(DistSq);
                        const float Dot = (Dist > KINDA_SMALL_NUMBER) ? FVector2D::DotProduct(ToLoc / Dist, SafeForward) : 1.0f;
                        if (Dot >= CosHalfAngle) {
                            TryConsumeElement(Consumer, Predicate, Item, Loc);
                        }
                    }
                });
            }
        }
    }

    /**
     * @brief 2D 扇形范围查询（带自定义过滤谓词）
     * @tparam TElement 元素类型
     * @tparam TPredicate 谓词类型
     * @param Container 目标空间容器
     * @param Center 扇形圆心
     * @param Forward 扇形基准朝向
     * @param Radius 扇形半径
     * @param HalfAngleDegrees 扇形半角（度数）
     * @param OutResults 输出落入扇形内的元素集合
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate>
    static void QuerySector(
        const TSpatialHashContainer2D<TElement>& Container,
        const FVector2D& Center,
        const FVector2D& Forward,
        float Radius,
        float HalfAngleDegrees,
        TArray<TElement>& OutResults,
        TPredicate Predicate
    ) {
        OutResults.Reset();
        QuerySectorMatching(Container, Center, Forward, Radius, HalfAngleDegrees, [&OutResults](const TElement& Item) {
            OutResults.Add(Item);
        }, MoveTemp(Predicate));
    }

    /**
     * @brief 查找距离给定中心点最近的目标元素（单元格包围盒最短距离动态剪枝与谓词过滤）
     * @tparam TElement 元素类型
     * @tparam TPredicate 谓词类型
     * @param Container 目标底层空间哈希容器
     * @param Center 中心点二维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param OutNearest 输出最近元素
     * @param Predicate 过滤谓词
     * @return 存在有效元素返回 true，指定半径内无元素返回 false
     */
    template <typename TElement, typename TPredicate>
    static bool QueryNearest(
        const TSpatialHashContainer2D<TElement>& Container,
        const FVector2D& Center,
        float MaxSearchRadius,
        TElement& OutNearest,
        TPredicate Predicate
    ) {
        if (MaxSearchRadius <= 0.0f || Container.Num() == 0) {
            return false;
        }

        float ClosestDistSq = MaxSearchRadius * MaxSearchRadius;
        const TElement* BestCandidate = nullptr;

        const float InvCellSize = Container.GetInvCellSize();
        const float CellSize = Container.GetCellSize();

        const int32 CenterCellX = FMath::FloorToInt(Center.X * InvCellSize);
        const int32 CenterCellY = FMath::FloorToInt(Center.Y * InvCellSize);

        Container.ForEachElementInCell(Container.PackCellKey(CenterCellX, CenterCellY), [&Center, &ClosestDistSq, &BestCandidate, &Predicate](const TElement& Item, const FVector2D& Loc) {
            if (InvokePredicate(Predicate, Item, Loc)) {
                const float DistSq = FVector2D::DistSquared(Center, Loc);
                if (DistSq <= ClosestDistSq) {
                    ClosestDistSq = DistSq;
                    BestCandidate = &Item;
                }
            }
        });

        const float SearchRadius = FMath::Sqrt(ClosestDistSq);
        const int32 MinX = FMath::FloorToInt((Center.X - SearchRadius) * InvCellSize);
        const int32 MaxX = FMath::FloorToInt((Center.X + SearchRadius) * InvCellSize);
        const int32 MinY = FMath::FloorToInt((Center.Y - SearchRadius) * InvCellSize);
        const int32 MaxY = FMath::FloorToInt((Center.Y + SearchRadius) * InvCellSize);

        for (int32 X = MinX; X <= MaxX; ++X) {
            const float Dx = Get1DMinDistanceToCell(Center.X, X, CellSize);
            const float DxSq = Dx * Dx;
            if (DxSq > ClosestDistSq) {
                continue;
            }

            for (int32 Y = MinY; Y <= MaxY; ++Y) {
                if (X == CenterCellX && Y == CenterCellY) {
                    continue;
                }

                const float Dy = Get1DMinDistanceToCell(Center.Y, Y, CellSize);
                if (DxSq + Dy * Dy > ClosestDistSq) {
                    continue;
                }

                Container.ForEachElementInCell(Container.PackCellKey(X, Y), [&Center, &ClosestDistSq, &BestCandidate, &Predicate](const TElement& Item, const FVector2D& Loc) {
                    if (InvokePredicate(Predicate, Item, Loc)) {
                        const float DistSq = FVector2D::DistSquared(Center, Loc);
                        if (DistSq <= ClosestDistSq) {
                            ClosestDistSq = DistSq;
                            BestCandidate = &Item;
                        }
                    }
                });
            }
        }

        if (BestCandidate == nullptr) {
            return false;
        }
        OutNearest = *BestCandidate;
        return true;
    }

    /**
     * @brief 查找距离给定中心点最近的 K 个目标元素（动态剪枝与谓词过滤，按距离升序返回）
     * @tparam TElement 元素类型
     * @tparam TPredicate 谓词类型
     * @param Container 目标底层空间哈希容器
     * @param Center 中心点二维坐标
     * @param MaxSearchRadius 最大检索截断半径
     * @param K 期望获取的最大近邻数量（必须大于 0）
     * @param OutNearestList 输出最近的 K 个元素列表（按距离由近至远升序排列）
     * @param Predicate 过滤谓词
     */
    template <typename TElement, typename TPredicate>
    static void QueryKNearest(
        const TSpatialHashContainer2D<TElement>& Container,
        const FVector2D& Center,
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
            float DistSq;
        };

        TArray<FKCandidate> Heap;
        Heap.Reserve(K);

        auto TryAddCandidate = [&Heap, K, &Center, &Predicate](const TElement& Item, const FVector2D& Loc, float CurrentMaxDistSq) -> float {
            if (!InvokePredicate(Predicate, Item, Loc)) {
                return CurrentMaxDistSq;
            }

            const float DistSq = FVector2D::DistSquared(Center, Loc);
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

        float MaxValidDistSq = MaxSearchRadius * MaxSearchRadius;
        const float InvCellSize = Container.GetInvCellSize();
        const float CellSize = Container.GetCellSize();

        const int32 CenterCellX = FMath::FloorToInt(Center.X * InvCellSize);
        const int32 CenterCellY = FMath::FloorToInt(Center.Y * InvCellSize);

        Container.ForEachElementInCell(Container.PackCellKey(CenterCellX, CenterCellY), [&TryAddCandidate, &MaxValidDistSq](const TElement& Item, const FVector2D& Loc) {
            MaxValidDistSq = TryAddCandidate(Item, Loc, MaxValidDistSq);
        });

        const float SearchRadius = FMath::Sqrt(MaxValidDistSq);
        const int32 MinX = FMath::FloorToInt((Center.X - SearchRadius) * InvCellSize);
        const int32 MaxX = FMath::FloorToInt((Center.X + SearchRadius) * InvCellSize);
        const int32 MinY = FMath::FloorToInt((Center.Y - SearchRadius) * InvCellSize);
        const int32 MaxY = FMath::FloorToInt((Center.Y + SearchRadius) * InvCellSize);

        for (int32 X = MinX; X <= MaxX; ++X) {
            const float Dx = Get1DMinDistanceToCell(Center.X, X, CellSize);
            const float DxSq = Dx * Dx;
            if (DxSq > MaxValidDistSq) {
                continue;
            }

            for (int32 Y = MinY; Y <= MaxY; ++Y) {
                if (X == CenterCellX && Y == CenterCellY) {
                    continue;
                }

                const float Dy = Get1DMinDistanceToCell(Center.Y, Y, CellSize);
                if (DxSq + Dy * Dy > MaxValidDistSq) {
                    continue;
                }

                Container.ForEachElementInCell(Container.PackCellKey(X, Y), [&TryAddCandidate, &MaxValidDistSq](const TElement& Item, const FVector2D& Loc) {
                    MaxValidDistSq = TryAddCandidate(Item, Loc, MaxValidDistSq);
                });
            }
        }

        OutNearestList.Reserve(Heap.Num());
        for (const FKCandidate& Cand : Heap) {
            OutNearestList.Add(Cand.Element);
        }
    }
};
