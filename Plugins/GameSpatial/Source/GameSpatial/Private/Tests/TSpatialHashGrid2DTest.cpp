/**
 * @file TSpatialHashGrid2DTest.cpp
 * @brief TSpatialHashGrid2D 纯逻辑二维空间哈希网格全路径与边界单元测试
 */

#include "Grid/TSpatialHashGrid2D.h"

#if WITH_AUTOMATION_TESTS

#include <limits>
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialHashGrid2DLifecycleAndBucketsTest,
    "Game.Spatial.HashGrid2D.LifecycleAndBuckets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 2D 网格构造参数回退、插入/更新/移除错误码、O(1) 同桶交换索引一致性与容器遍历
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialHashGrid2DLifecycleAndBucketsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const float NaNVal = std::numeric_limits<float>::quiet_NaN();
    const float InfVal = std::numeric_limits<float>::infinity();

    const TSpatialHashGrid2D<int32> FallbackZero(0.0f);
    const TSpatialHashGrid2D<int32> FallbackNeg(-100.0f);
    const TSpatialHashGrid2D<int32> FallbackNaN(NaNVal);
    const TSpatialHashGrid2D<int32> FallbackInf(InfVal);
    TestEqual(TEXT("0 边长回退至默认值"), FallbackZero.GetCellSize(), TSpatialHashGrid2D<int32>::DEFAULT_CELL_SIZE);
    TestEqual(TEXT("负边长回退至默认值"), FallbackNeg.GetCellSize(), TSpatialHashGrid2D<int32>::DEFAULT_CELL_SIZE);
    TestEqual(TEXT("NaN 边长回退至默认值"), FallbackNaN.GetCellSize(), TSpatialHashGrid2D<int32>::DEFAULT_CELL_SIZE);
    TestEqual(TEXT("Inf 边长回退至默认值"), FallbackInf.GetCellSize(), TSpatialHashGrid2D<int32>::DEFAULT_CELL_SIZE);

    TSpatialHashGrid2D<int32> Grid(500.0f);
    Grid.Reserve(64);
    TestEqual(TEXT("初始元素数量为 0"), Grid.Num(), 0);
    TestEqual(TEXT("网格单元边长匹配"), Grid.GetCellSize(), 500.0f);

    const TGameStatusResult<void> NaNInsert = Grid.Insert(99, FVector2D(NaNVal, 0.0));
    TestTrue(TEXT("NaN 坐标插入被拦截"), NaNInsert.IsFailure());
    TestEqual(TEXT("NaN 坐标错误码为 6003"), NaNInsert.GetError().ErrorCode, Game::SpatialErrorCodes::InvalidCoordinate);

    const TGameStatusResult<void> InfInsert = Grid.Insert(99, FVector2D(0.0, InfVal));
    TestTrue(TEXT("Inf 坐标插入被拦截"), InfInsert.IsFailure());
    TestEqual(TEXT("Inf 坐标错误码为 6003"), InfInsert.GetError().ErrorCode, Game::SpatialErrorCodes::InvalidCoordinate);

    TestTrue(TEXT("插入元素 1 成功"), Grid.Insert(1, FVector2D(100.0, 100.0)).IsSuccess());

    const TGameStatusResult<void> DuplicateInsert = Grid.Insert(1, FVector2D(200.0, 200.0));
    TestTrue(TEXT("重复插入同名元素被拦截"), DuplicateInsert.IsFailure());
    TestEqual(TEXT("重复插入错误码为 6002"), DuplicateInsert.GetError().ErrorCode, Game::SpatialErrorCodes::ElementAlreadyExists);

    TestTrue(TEXT("Contains 包含元素 1"), Grid.Contains(1));
    TestEqual(TEXT("元素总数为 1"), Grid.Num(), 1);
    TestEqual(TEXT("活跃桶数量为 1"), Grid.GetBucketCount(), 1);

    const FVector2D* FoundLoc = Grid.FindLocation(1);
    TestNotNull(TEXT("FindLocation 返回有效指针"), FoundLoc);
    if (FoundLoc != nullptr) {
        TestEqual(TEXT("坐标 X 匹配"), FoundLoc->X, 100.0);
        TestEqual(TEXT("坐标 Y 匹配"), FoundLoc->Y, 100.0);
    }
    TestNull(TEXT("不存在元素 FindLocation 返回 nullptr"), Grid.FindLocation(999));

    const TGameStatusResult<void> MissingUpdate = Grid.Update(999, FVector2D(10.0, 10.0));
    TestTrue(TEXT("更新不存在元素返回失败"), MissingUpdate.IsFailure());
    TestEqual(TEXT("更新不存在元素错误码为 6001"), MissingUpdate.GetError().ErrorCode, Game::SpatialErrorCodes::ElementNotFound);

    const TGameStatusResult<void> NaNUpdate = Grid.Update(1, FVector2D(NaNVal, 100.0));
    TestTrue(TEXT("更新 NaN 坐标返回失败"), NaNUpdate.IsFailure());
    TestEqual(TEXT("更新 NaN 坐标错误码为 6003"), NaNUpdate.GetError().ErrorCode, Game::SpatialErrorCodes::InvalidCoordinate);

    TestTrue(TEXT("同坐标无变更更新成功"), Grid.Update(1, FVector2D(100.0, 100.0)).IsSuccess());
    TestTrue(TEXT("同单元格微移更新成功"), Grid.Update(1, FVector2D(150.0, 160.0)).IsSuccess());
    TestEqual(TEXT("同单元格更新不增加新桶"), Grid.GetBucketCount(), 1);
    if (const FVector2D* UpdatedLoc = Grid.FindLocation(1)) {
        TestEqual(TEXT("同单元格微移后 X 同步"), UpdatedLoc->X, 150.0);
        TestEqual(TEXT("同单元格微移后 Y 同步"), UpdatedLoc->Y, 160.0);
    }

    TestTrue(TEXT("跨单元格迁移更新成功"), Grid.Update(1, FVector2D(2500.0, 2500.0)).IsSuccess());
    TestEqual(TEXT("跨桶迁移后旧空桶自动回收"), Grid.GetBucketCount(), 1);

    TestTrue(TEXT("同桶插入 2"), Grid.Insert(2, FVector2D(2510.0, 2510.0)).IsSuccess());
    TestTrue(TEXT("同桶插入 3"), Grid.Insert(3, FVector2D(2520.0, 2520.0)).IsSuccess());
    TestTrue(TEXT("同桶插入 4"), Grid.Insert(4, FVector2D(2530.0, 2530.0)).IsSuccess());
    TestEqual(TEXT("四元素同桶时桶数为 1"), Grid.GetBucketCount(), 1);

    TestTrue(TEXT("移除首元素 1 触发尾部 4 交换补位"), Grid.Remove(1).IsSuccess());
    TestTrue(TEXT("被换入槽 0 的元素 4 原地微移成功"), Grid.Update(4, FVector2D(2535.0, 2535.0)).IsSuccess());
    TArray<int32> CheckBucketHits;
    Grid.QueryRadius(FVector2D(2535.0, 2535.0), 1.0f, CheckBucketHits);
    TestEqual(TEXT("换位元素 4 桶内坐标同步一致"), CheckBucketHits.Num(), 1);
    if (!CheckBucketHits.IsEmpty()) {
        TestEqual(TEXT("精准命中换位后的元素 4"), CheckBucketHits[0], 4);
    }

    TestTrue(TEXT("换位元素 4 跨格迁移成功"), Grid.Update(4, FVector2D(-1000.0, -1000.0)).IsSuccess());
    TestEqual(TEXT("跨格迁移后活跃桶数为 2"), Grid.GetBucketCount(), 2);

    TArray<int32> AllElems;
    Grid.GetAllElements(AllElems);
    TestEqual(TEXT("GetAllElements 导出 3 个存活元素"), AllElems.Num(), 3);

    int32 IterSum = 0;
    Grid.ForEachElement([&IterSum](int32 Id, const FVector2D&) { IterSum += Id; });
    TestEqual(TEXT("ForEachElement 双参遍历累计 ID 正确"), IterSum, 2 + 3 + 4);

    const TGameStatusResult<void> MissingRemove = Grid.Remove(999);
    TestTrue(TEXT("移除不存在元素返回失败"), MissingRemove.IsFailure());
    TestEqual(TEXT("移除不存在元素错误码为 6001"), MissingRemove.GetError().ErrorCode, Game::SpatialErrorCodes::ElementNotFound);

    TestTrue(TEXT("移除 2"), Grid.Remove(2).IsSuccess());
    TestTrue(TEXT("移除 3"), Grid.Remove(3).IsSuccess());
    TestTrue(TEXT("移除 4"), Grid.Remove(4).IsSuccess());
    TestEqual(TEXT("全部移除后元素数归零"), Grid.Num(), 0);
    TestEqual(TEXT("全部移除后活跃桶归零"), Grid.GetBucketCount(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialHashGrid2DGeometricQueriesTest,
    "Game.Spatial.HashGrid2D.GeometricQueries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 2D 网格圆形、AABB、最近邻查询及 NaN/颠倒包围盒/负象限鲁棒性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialHashGrid2DGeometricQueriesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSpatialHashGrid2D<int32> Grid(200.0f);
    Grid.Insert(101, FVector2D(0.0, 0.0));
    Grid.Insert(102, FVector2D(50.0, 0.0));
    Grid.Insert(103, FVector2D(0.0, 80.0));
    Grid.Insert(104, FVector2D(-480.0, -480.0));
    Grid.Insert(105, FVector2D(1000.0, 1000.0));

    TArray<int32> CircleResults;
    Grid.QueryRadius(FVector2D(0.0, 0.0), 100.0f, CircleResults);
    TestEqual(TEXT("半径 100 命中 3 个近处目标"), CircleResults.Num(), 3);
    TestTrue(TEXT("命中 101"), CircleResults.Contains(101));
    TestTrue(TEXT("命中 102"), CircleResults.Contains(102));
    TestTrue(TEXT("命中 103"), CircleResults.Contains(103));

    TArray<int32> PredRadiusResults;
    Grid.QueryRadius(FVector2D(0.0, 0.0), 100.0f, PredRadiusResults, [](int32, const FVector2D& Loc) {
        return Loc.Y > 50.0;
    });
    TestEqual(TEXT("双参谓词仅命中 103"), PredRadiusResults.Num(), 1);
    if (!PredRadiusResults.IsEmpty()) {
        TestEqual(TEXT("命中项为 103"), PredRadiusResults[0], 103);
    }

    TArray<int32> MutablePredResults;
    Grid.QueryRadius(
        FVector2D(0.0, 0.0),
        100.0f,
        MutablePredResults,
        [Quota = 2](int32, const FVector2D&) mutable {
            return Quota-- > 0;
        }
    );
    TestEqual(TEXT("mutable 双参谓词按配额精确命中 2 个"), MutablePredResults.Num(), 2);

    TArray<int32> BoxResults;
    Grid.QueryAABB(FBox2D(FVector2D(-500.0, -500.0), FVector2D(60.0, 10.0)), BoxResults);
    TestEqual(TEXT("跨原点 AABB 命中 101、102、104"), BoxResults.Num(), 3);
    TestTrue(TEXT("包含负象限 104"), BoxResults.Contains(104));

    int32 NearestTarget = 0;
    TestTrue(TEXT("中心格内近邻查询命中"), Grid.QueryNearest(FVector2D(10.0, 10.0), 200.0f, NearestTarget));
    TestEqual(TEXT("最近为 101"), NearestTarget, 101);

    TestTrue(
        TEXT("空中心格向周边格搜索近邻命中"),
        Grid.QueryNearest(FVector2D(-250.0, -250.0), 400.0f, NearestTarget)
    );
    TestEqual(TEXT("距 (-250,-250) 最近为 104"), NearestTarget, 104);

    int32 PredNearest = 0;
    TestTrue(
        TEXT("谓词排除 101 后命中次近 102"),
        Grid.QueryNearest(FVector2D(10.0, 10.0), 200.0f, PredNearest, [](int32 Id) { return Id != 101; })
    );
    TestEqual(TEXT("次近目标为 102"), PredNearest, 102);

    TestFalse(TEXT("半径内无目标返回 false"), Grid.QueryNearest(FVector2D(5000.0, 5000.0), 100.0f, NearestTarget));

    const float NaNVal = std::numeric_limits<float>::quiet_NaN();
    TArray<int32> InvalidResults;
    Grid.QueryRadius(FVector2D::ZeroVector, -10.0f, InvalidResults);
    TestTrue(TEXT("负半径返回空"), InvalidResults.IsEmpty());
    Grid.QueryRadius(FVector2D::ZeroVector, NaNVal, InvalidResults);
    TestTrue(TEXT("NaN 半径返回空"), InvalidResults.IsEmpty());
    Grid.QueryRadius(FVector2D(NaNVal, 0.0), 100.0f, InvalidResults);
    TestTrue(TEXT("NaN 圆心返回空"), InvalidResults.IsEmpty());

    Grid.QueryAABB(FBox2D(ForceInit), InvalidResults);
    TestTrue(TEXT("未初始化无效 FBox2D 返回空"), InvalidResults.IsEmpty());
    Grid.QueryAABB(FBox2D(FVector2D(NaNVal, 0.0), FVector2D(10.0, 10.0)), InvalidResults);
    TestTrue(TEXT("含 NaN 分量的 FBox2D 返回空"), InvalidResults.IsEmpty());
    Grid.QueryAABB(FBox2D(FVector2D(80.0, 80.0), FVector2D(10.0, 10.0)), InvalidResults);
    TestTrue(TEXT("同格内颠倒 FBox2D 返回空"), InvalidResults.IsEmpty());

    TestFalse(TEXT("NaN 半径 QueryNearest 返回 false"), Grid.QueryNearest(FVector2D::ZeroVector, NaNVal, NearestTarget));

    Grid.Clear();
    TestFalse(TEXT("空网格 QueryNearest 返回 false"), Grid.QueryNearest(FVector2D::ZeroVector, 100.0f, NearestTarget));
    Grid.QueryRadius(FVector2D::ZeroVector, 100.0f, InvalidResults);
    TestTrue(TEXT("空网格 QueryRadius 返回空"), InvalidResults.IsEmpty());
    Grid.QueryAABB(FBox2D(FVector2D(-100.0, -100.0), FVector2D(100.0, 100.0)), InvalidResults);
    TestTrue(TEXT("空网格 QueryAABB 返回空"), InvalidResults.IsEmpty());
    Grid.QuerySector(FVector2D::ZeroVector, FVector2D(1.0, 0.0), 100.0f, 45.0f, InvalidResults);
    TestTrue(TEXT("空网格 QuerySector 返回空"), InvalidResults.IsEmpty());
    Grid.QueryKNearest(FVector2D::ZeroVector, 100.0f, 3, InvalidResults);
    TestTrue(TEXT("空网格 QueryKNearest 返回空"), InvalidResults.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialHashGrid2DSectorAndKNearestTest,
    "Game.Spatial.HashGrid2D.SectorAndKNearest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 2D 扇形（锐角/45度边界/钝角/全向/零朝向）与 K 近邻严格升序及 K>16 堆溢出路径
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialHashGrid2DSectorAndKNearestTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSpatialHashGrid2D<int32> Grid(100.0f);
    Grid.Insert(1, FVector2D(0.0, 0.0));
    Grid.Insert(2, FVector2D(100.0, 0.0));
    Grid.Insert(3, FVector2D(100.0, 100.0));
    Grid.Insert(4, FVector2D(0.0, 100.0));
    Grid.Insert(5, FVector2D(-50.0, 86.60254));
    Grid.Insert(6, FVector2D(-100.0, 0.0));

    TArray<int32> AcuteResults;
    Grid.QuerySector(FVector2D::ZeroVector, FVector2D(1.0, 0.0), 150.0f, 45.0f, AcuteResults);
    TestEqual(TEXT("45度锐角扇形精确包含顶点 1、正前方 2 与 45度边界 3"), AcuteResults.Num(), 3);
    TestTrue(TEXT("包含顶点 1"), AcuteResults.Contains(1));
    TestTrue(TEXT("包含 2"), AcuteResults.Contains(2));
    TestTrue(TEXT("包含精确 45度边界 3"), AcuteResults.Contains(3));
    TestFalse(TEXT("不含 90度的 4"), AcuteResults.Contains(4));

    TArray<int32> RightAngleResults;
    Grid.QuerySector(FVector2D::ZeroVector, FVector2D(1.0, 0.0), 150.0f, 90.0f, RightAngleResults);
    TestEqual(TEXT("90度半角命中 1、2、3、4"), RightAngleResults.Num(), 4);
    TestFalse(TEXT("不含 120度的 5"), RightAngleResults.Contains(5));

    TArray<int32> ObtuseResults;
    Grid.QuerySector(FVector2D::ZeroVector, FVector2D(1.0, 0.0), 150.0f, 125.0f, ObtuseResults);
    TestEqual(TEXT("125度钝角扇形命中 1~5（不含背后 180度 6）"), ObtuseResults.Num(), 5);
    TestTrue(TEXT("钝角包含 120度的 5"), ObtuseResults.Contains(5));
    TestFalse(TEXT("钝角不含背后 6"), ObtuseResults.Contains(6));

    TArray<int32> FullResults;
    Grid.QuerySector(FVector2D::ZeroVector, FVector2D(1.0, 0.0), 150.0f, 180.0f, FullResults);
    TestEqual(TEXT("180度半角命中全部 6 点"), FullResults.Num(), 6);

    TArray<int32> ZeroDirAcute;
    Grid.QuerySector(FVector2D::ZeroVector, FVector2D::ZeroVector, 150.0f, 45.0f, ZeroDirAcute);
    TestTrue(TEXT("零朝向锐角扇形安全返回空（含顶点也不误命中）"), ZeroDirAcute.IsEmpty());

    TArray<int32> ZeroDirObtuse;
    Grid.QuerySector(FVector2D::ZeroVector, FVector2D::ZeroVector, 150.0f, 120.0f, ZeroDirObtuse);
    TestTrue(TEXT("零朝向钝角扇形安全返回空"), ZeroDirObtuse.IsEmpty());

    TArray<int32> NonPositiveAngle;
    Grid.QuerySector(FVector2D::ZeroVector, FVector2D(1.0, 0.0), 150.0f, 0.0f, NonPositiveAngle);
    TestTrue(TEXT("0度半角返回空"), NonPositiveAngle.IsEmpty());

    TSpatialHashGrid2D<int32> KGrid(100.0f);
    for (int32 Id = 1; Id <= 30; ++Id) {
        KGrid.Insert(Id, FVector2D(static_cast<double>(Id) * 20.0, 0.0));
    }

    TArray<int32> K5Results;
    KGrid.QueryKNearest(FVector2D::ZeroVector, 1000.0f, 5, K5Results);
    TestEqual(TEXT("K=5 命中 5 个"), K5Results.Num(), 5);
    for (int32 Idx = 0; Idx < K5Results.Num(); ++Idx) {
        TestEqual(TEXT("K=5 严格按距离升序排列"), K5Results[Idx], Idx + 1);
    }

    TArray<int32> K24Results;
    KGrid.QueryKNearest(FVector2D::ZeroVector, 1000.0f, 24, K24Results);
    TestEqual(TEXT("K=24 (>16 内联溢出与二分插入路径) 命中 24 个"), K24Results.Num(), 24);
    for (int32 Idx = 0; Idx < K24Results.Num(); ++Idx) {
        TestEqual(TEXT("K=24 严格按距离升序排列"), K24Results[Idx], Idx + 1);
    }

    TArray<int32> PredK;
    KGrid.QueryKNearest(FVector2D::ZeroVector, 1000.0f, 3, PredK, [](int32 Id) { return (Id % 2) == 0; });
    TestEqual(TEXT("偶谓词 K=3 命中 3 个"), PredK.Num(), 3);
    if (PredK.Num() == 3) {
        TestEqual(TEXT("第 1 近偶数 ID=2"), PredK[0], 2);
        TestEqual(TEXT("第 2 近偶数 ID=4"), PredK[1], 4);
        TestEqual(TEXT("第 3 近偶数 ID=6"), PredK[2], 6);
    }

    TArray<int32> ZeroK;
    KGrid.QueryKNearest(FVector2D::ZeroVector, 1000.0f, 0, ZeroK);
    TestTrue(TEXT("K<=0 返回空"), ZeroK.IsEmpty());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialHashGrid2DLwcAndSparseTest,
    "Game.Spatial.HashGrid2D.LargeWorldAndSparseFastPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 2D 大世界双精度坐标无假阴性剪枝、稀疏大半径快路径无卡顿无重复及流式消费原语
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialHashGrid2DLwcAndSparseTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSpatialHashGrid2D<int32> LwcGrid(500.0f);
    const FVector2D FarPoint(4999999.9, -4999999.9);
    TestTrue(TEXT("大世界坐标插入成功"), LwcGrid.Insert(77, FarPoint).IsSuccess());

    TArray<int32> LwcHits;
    LwcGrid.QueryRadius(FVector2D(4999999.5, -4999999.5), 0.6f, LwcHits);
    TestEqual(TEXT("大世界 50km 格边界微距半径查询零假阴性命中"), LwcHits.Num(), 1);
    if (!LwcHits.IsEmpty()) {
        TestEqual(TEXT("命中元素为 77"), LwcHits[0], 77);
    }

    TSpatialHashGrid2D<int32> SparseGrid(500.0f);
    for (int32 Id = 1; Id <= 10; ++Id) {
        SparseGrid.Insert(Id, FVector2D(static_cast<double>(Id) * 1000.0, 0.0));
    }

    TArray<int32> HugeRadiusHits;
    SparseGrid.QueryRadius(FVector2D::ZeroVector, 1.0e8f, HugeRadiusHits);
    TestEqual(TEXT("稀疏超大半径查询完整命中 10 个且零卡顿"), HugeRadiusHits.Num(), 10);

    TArray<int32> HugeBoxHits;
    SparseGrid.QueryAABB(FBox2D(FVector2D(-1.0e8, -1.0e8), FVector2D(1.0e8, 1.0e8)), HugeBoxHits);
    TestEqual(TEXT("稀疏超大 AABB 完整命中 10 个"), HugeBoxHits.Num(), 10);

    TArray<int32> HugeSectorHits;
    SparseGrid.QuerySector(FVector2D::ZeroVector, FVector2D(1.0, 0.0), 1.0e8f, 30.0f, HugeSectorHits);
    TestEqual(TEXT("稀疏超大扇形完整命中 10 个"), HugeSectorHits.Num(), 10);

    int32 HugeNearest = 0;
    TestTrue(TEXT("稀疏超大半径最近邻命中"), SparseGrid.QueryNearest(FVector2D(990.0, 0.0), 1.0e8f, HugeNearest));
    TestEqual(TEXT("最近为 1"), HugeNearest, 1);

    TArray<int32> HugeKNearest;
    SparseGrid.QueryKNearest(FVector2D(990.0, 0.0), 1.0e8f, 4, HugeKNearest);
    TestEqual(TEXT("稀疏超大半径 K=4 无重复返回 4 个"), HugeKNearest.Num(), 4);
    if (HugeKNearest.Num() == 4) {
        TestEqual(TEXT("稀疏 K 近邻第 1 近为 1"), HugeKNearest[0], 1);
        TestEqual(TEXT("稀疏 K 近邻第 2 近为 2"), HugeKNearest[1], 2);
        TestEqual(TEXT("稀疏 K 近邻第 3 近为 3"), HugeKNearest[2], 3);
        TestEqual(TEXT("稀疏 K 近邻第 4 近为 4"), HugeKNearest[3], 4);
    }

    int32 StreamCount = 0;
    double LastDistSq = -1.0;
    bool bMonotonic = true;
    SparseGrid.QueryKNearestMatching(FVector2D::ZeroVector, 5500.0f, 5, [&StreamCount, &LastDistSq, &bMonotonic](int32, double DistSq) {
        ++StreamCount;
        if (DistSq < LastDistSq) {
            bMonotonic = false;
        }
        LastDistSq = DistSq;
    });
    TestEqual(TEXT("QueryKNearestMatching 流式回调 5 次"), StreamCount, 5);
    TestTrue(TEXT("流式 K 近邻距离平方单调非降"), bMonotonic);

    int32 AabbStreamCount = 0;
    SparseGrid.QueryAABBMatching(FBox2D(FVector2D(500.0, -10.0), FVector2D(3500.0, 10.0)), [&AabbStreamCount](int32) {
        ++AabbStreamCount;
    });
    TestEqual(TEXT("QueryAABBMatching 命中 3 个"), AabbStreamCount, 3);

    int32 SectorStreamCount = 0;
    SparseGrid.QuerySectorMatching(FVector2D::ZeroVector, FVector2D(1.0, 0.0), 2500.0f, 30.0f, [&SectorStreamCount](int32, const FVector2D&) {
        ++SectorStreamCount;
    });
    TestEqual(TEXT("QuerySectorMatching 命中 2 个"), SectorStreamCount, 2);

    TSpatialHashGrid2D<int32> ExtremeGrid(500.0f);
    const FVector2D ExtremePos(1.2e12, 0.0);
    const FVector2D ExtremeNeg(-1.2e12, 0.0);
    TestTrue(TEXT("2D 极大正坐标插入成功"), ExtremeGrid.Insert(701, ExtremePos).IsSuccess());
    TestTrue(TEXT("2D 极大负坐标插入成功"), ExtremeGrid.Insert(702, ExtremeNeg).IsSuccess());

    TArray<int32> ExtremeHits;
    ExtremeGrid.QueryRadius(ExtremePos, 100.0f, ExtremeHits);
    TestEqual(TEXT("2D MAX_CELL_COORD 钳制格半径查询零死循环且零漏检"), ExtremeHits.Num(), 1);
    if (!ExtremeHits.IsEmpty()) {
        TestEqual(TEXT("精准命中极大正坐标元素 701"), ExtremeHits[0], 701);
    }

    ExtremeGrid.QueryRadius(ExtremeNeg, 100.0f, ExtremeHits);
    TestEqual(TEXT("2D MIN_CELL_COORD 钳制格半径查询零漏检"), ExtremeHits.Num(), 1);
    if (!ExtremeHits.IsEmpty()) {
        TestEqual(TEXT("精准命中极大负坐标元素 702"), ExtremeHits[0], 702);
    }

    ExtremeGrid.QueryAABB(FBox2D(ExtremePos - FVector2D(50.0, 50.0), ExtremePos + FVector2D(50.0, 50.0)), ExtremeHits);
    TestEqual(TEXT("2D MAX_CELL_COORD 钳制格 AABB 查询零死循环且精准命中"), ExtremeHits.Num(), 1);

    TSpatialHashGrid2D<int32> CenterSparseKGrid(500.0f);
    CenterSparseKGrid.Insert(1, FVector2D(10.0, 0.0));
    CenterSparseKGrid.Insert(2, FVector2D(40.0, 0.0));
    CenterSparseKGrid.Insert(3, FVector2D(40.0, 0.0));
    TArray<int32> TieKResults;
    CenterSparseKGrid.QueryKNearest(FVector2D::ZeroVector, 1.0e8f, 2, TieKResults);
    TestEqual(TEXT("中心格已有 >=K 元素且存在等距项时 K=2 返回精确 2 个"), TieKResults.Num(), 2);
    if (TieKResults.Num() == 2) {
        TestEqual(TEXT("最近为 1"), TieKResults[0], 1);
        TestTrue(TEXT("次近为等距项 2 或 3"), TieKResults[1] == 2 || TieKResults[1] == 3);
    }

    return true;
}

#endif
