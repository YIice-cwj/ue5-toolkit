/**
 * @file TSpatialHashGrid3DTest.cpp
 * @brief TSpatialHashGrid3D 纯逻辑三维空间哈希网格全路径与边界单元测试
 */

#include "Grid/TSpatialHashGrid3D.h"

#if WITH_AUTOMATION_TESTS

#include <limits>
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialHashGrid3DLifecycleTest,
    "Game.Spatial.HashGrid3D.LifecycleAndBuckets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 3D 网格构造回退、错误码契约、O(1) 桶内交换索引一致性与极值位钳制
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialHashGrid3DLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const float NaNVal = std::numeric_limits<float>::quiet_NaN();
    const float InfVal = std::numeric_limits<float>::infinity();

    TestEqual(TEXT("非法 0 边长回退默认值"), TSpatialHashGrid3D<int32>(0.0f).GetCellSize(), TSpatialHashGrid3D<int32>::DEFAULT_CELL_SIZE);
    TestEqual(TEXT("非法 NaN 边长回退默认值"), TSpatialHashGrid3D<int32>(NaNVal).GetCellSize(), TSpatialHashGrid3D<int32>::DEFAULT_CELL_SIZE);

    TSpatialHashGrid3D<int32> Grid(500.0f);
    Grid.Reserve(64);
    TestEqual(TEXT("初始元素数量为 0"), Grid.Num(), 0);

    const TGameStatusResult<void> NaNInsert = Grid.Insert(1, FVector(0.0, NaNVal, 0.0));
    TestTrue(TEXT("NaN 三维坐标被拦截"), NaNInsert.IsFailure());
    TestEqual(TEXT("NaN 坐标错误码为 6003"), NaNInsert.GetError().ErrorCode, Game::SpatialErrorCodes::InvalidCoordinate);

    const TGameStatusResult<void> InfInsert = Grid.Insert(1, FVector(0.0, 0.0, InfVal));
    TestTrue(TEXT("Inf 三维坐标被拦截"), InfInsert.IsFailure());
    TestEqual(TEXT("Inf 坐标错误码为 6003"), InfInsert.GetError().ErrorCode, Game::SpatialErrorCodes::InvalidCoordinate);

    TestTrue(TEXT("插入元素 1 成功"), Grid.Insert(1, FVector(100.0, 100.0, 50.0)).IsSuccess());

    const TGameStatusResult<void> DuplicateInsert = Grid.Insert(1, FVector(200.0, 200.0, 100.0));
    TestTrue(TEXT("重复插入同 ID 被拦截"), DuplicateInsert.IsFailure());
    TestEqual(TEXT("重复插入错误码为 6002"), DuplicateInsert.GetError().ErrorCode, Game::SpatialErrorCodes::ElementAlreadyExists);

    const FVector* FoundLoc = Grid.FindLocation(1);
    TestNotNull(TEXT("FindLocation 返回有效坐标指针"), FoundLoc);
    if (FoundLoc != nullptr) {
        TestEqual(TEXT("坐标 X 匹配"), FoundLoc->X, 100.0);
        TestEqual(TEXT("坐标 Y 匹配"), FoundLoc->Y, 100.0);
        TestEqual(TEXT("坐标 Z 匹配"), FoundLoc->Z, 50.0);
    }
    TestNull(TEXT("不存在元素 FindLocation 返回 nullptr"), Grid.FindLocation(999));

    TestEqual(TEXT("更新不存在元素返回 6001"), Grid.Update(999, FVector::ZeroVector).GetError().ErrorCode, Game::SpatialErrorCodes::ElementNotFound);
    TestEqual(TEXT("更新 NaN 坐标返回 6003"), Grid.Update(1, FVector(NaNVal, 0.0, 0.0)).GetError().ErrorCode, Game::SpatialErrorCodes::InvalidCoordinate);

    TestTrue(TEXT("同坐标无变更更新成功"), Grid.Update(1, FVector(100.0, 100.0, 50.0)).IsSuccess());
    TestTrue(TEXT("同格微移更新成功"), Grid.Update(1, FVector(120.0, 140.0, 60.0)).IsSuccess());
    TestEqual(TEXT("同格微移不产生新桶"), Grid.GetBucketCount(), 1);

    TestTrue(TEXT("跨格迁移更新成功"), Grid.Update(1, FVector(2500.0, 2500.0, 1500.0)).IsSuccess());
    TestEqual(TEXT("跨桶迁移后旧空桶自动回收"), Grid.GetBucketCount(), 1);

    TestTrue(TEXT("同桶插入 2"), Grid.Insert(2, FVector(2510.0, 2510.0, 1510.0)).IsSuccess());
    TestTrue(TEXT("同桶插入 3"), Grid.Insert(3, FVector(2520.0, 2520.0, 1520.0)).IsSuccess());
    TestTrue(TEXT("同桶插入 4"), Grid.Insert(4, FVector(2530.0, 2530.0, 1530.0)).IsSuccess());

    TestTrue(TEXT("移除首元素 1 触发尾部 4 换位"), Grid.Remove(1).IsSuccess());
    TestTrue(TEXT("换位元素 4 同格更新成功"), Grid.Update(4, FVector(2535.0, 2535.0, 1535.0)).IsSuccess());
    TArray<int32> Hits;
    Grid.QueryRadius(FVector(2535.0, 2535.0, 1535.0), 1.0f, Hits);
    TestEqual(TEXT("换位元素 4 桶坐标严格一致"), Hits.Num(), 1);
    if (!Hits.IsEmpty()) {
        TestEqual(TEXT("命中元素 4"), Hits[0], 4);
    }

    TestTrue(TEXT("换位元素 4 跨格迁移成功"), Grid.Update(4, FVector(-1000.0, -1000.0, -1000.0)).IsSuccess());

    TArray<int32> AllElements;
    Grid.GetAllElements(AllElements);
    TestEqual(TEXT("GetAllElements 导出 3 个元素"), AllElements.Num(), 3);

    int32 SumIds = 0;
    Grid.ForEachElement([&SumIds](int32 Id, const FVector&) { SumIds += Id; });
    TestEqual(TEXT("ForEachElement 遍历 ID 之和正确"), SumIds, 2 + 3 + 4);

    TestEqual(TEXT("移除不存在元素返回 6001"), Grid.Remove(999).GetError().ErrorCode, Game::SpatialErrorCodes::ElementNotFound);

    TestTrue(TEXT("移除 2"), Grid.Remove(2).IsSuccess());
    TestTrue(TEXT("移除 3"), Grid.Remove(3).IsSuccess());
    TestTrue(TEXT("移除 4"), Grid.Remove(4).IsSuccess());
    TestEqual(TEXT("全部移除后总数归零"), Grid.Num(), 0);
    TestEqual(TEXT("全部移除后活跃桶归零"), Grid.GetBucketCount(), 0);

    TestEqual(
        TEXT("3D 坐标上限钳制正确"),
        TSpatialHashContainer3D<int32>::ClampCoord(TSpatialHashContainer3D<int32>::MAX_CELL_COORD + 100),
        TSpatialHashContainer3D<int32>::MAX_CELL_COORD
    );
    TestEqual(
        TEXT("3D 坐标下限钳制正确"),
        TSpatialHashContainer3D<int32>::ClampCoord(TSpatialHashContainer3D<int32>::MIN_CELL_COORD - 100),
        TSpatialHashContainer3D<int32>::MIN_CELL_COORD
    );

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialHashGrid3DQueriesTest,
    "Game.Spatial.HashGrid3D.GeometricQueries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 3D 球形、AABB、圆柱体与最近邻查询及边界非法参数拦截
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialHashGrid3DQueriesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSpatialHashGrid3D<int32> Grid(200.0f);
    Grid.Insert(101, FVector(0.0, 0.0, 0.0));
    Grid.Insert(102, FVector(50.0, 0.0, 0.0));
    Grid.Insert(103, FVector(0.0, 80.0, 20.0));
    Grid.Insert(104, FVector(0.0, 0.0, 300.0));
    Grid.Insert(105, FVector(-380.0, -380.0, -380.0));

    TArray<int32> SphereResults;
    Grid.QueryRadius(FVector::ZeroVector, 100.0f, SphereResults);
    TestEqual(TEXT("球形半径 100 命中 3 点"), SphereResults.Num(), 3);
    TestTrue(TEXT("含 101"), SphereResults.Contains(101));
    TestTrue(TEXT("含 102"), SphereResults.Contains(102));
    TestTrue(TEXT("含 103"), SphereResults.Contains(103));
    TestFalse(TEXT("不含高处 104"), SphereResults.Contains(104));

    TArray<int32> BoxResults;
    Grid.QueryAABB(FBox(FVector(-400.0, -400.0, -400.0), FVector(60.0, 10.0, 10.0)), BoxResults);
    TestEqual(TEXT("跨原点三维 AABB 命中 101、102、105"), BoxResults.Num(), 3);

    TArray<int32> CylinderResults;
    Grid.QueryCylinder(FVector(0.0, 0.0, 100.0), 100.0f, 200.0f, CylinderResults);
    TestTrue(TEXT("圆柱体精确包含 Z=0 的 101"), CylinderResults.Contains(101));
    TestTrue(TEXT("圆柱体精确包含 Z=300 的 104"), CylinderResults.Contains(104));
    TestFalse(TEXT("不含负象限 105"), CylinderResults.Contains(105));

    TArray<int32> PredCylinder;
    Grid.QueryCylinder(FVector(0.0, 0.0, 100.0), 100.0f, 200.0f, PredCylinder, [](int32, const FVector& Loc) {
        return Loc.Z > 200.0;
    });
    TestEqual(TEXT("圆柱体谓词仅命中 104"), PredCylinder.Num(), 1);
    if (!PredCylinder.IsEmpty()) {
        TestEqual(TEXT("命中项为 104"), PredCylinder[0], 104);
    }

    int32 Nearest = 0;
    TestTrue(TEXT("空中心格向周边搜索三维最近邻成功"), Grid.QueryNearest(FVector(-250.0, -250.0, -250.0), 300.0f, Nearest));
    TestEqual(TEXT("距 (-250,-250,-250) 最近为 105"), Nearest, 105);

    TestTrue(
        TEXT("谓词排除 101 后命中次近 102"),
        Grid.QueryNearest(FVector(10.0, 10.0, 10.0), 150.0f, Nearest, [](int32 Id) { return Id != 101; })
    );
    TestEqual(TEXT("次近目标为 102"), Nearest, 102);

    const float NaNVal = std::numeric_limits<float>::quiet_NaN();
    TArray<int32> Invalid;
    Grid.QueryRadius(FVector::ZeroVector, NaNVal, Invalid);
    TestTrue(TEXT("NaN 半径返回空"), Invalid.IsEmpty());
    Grid.QueryCylinder(FVector::ZeroVector, 100.0f, 0.0f, Invalid);
    TestTrue(TEXT("0 半高圆柱返回空"), Invalid.IsEmpty());
    Grid.QueryCylinder(FVector::ZeroVector, -5.0f, 100.0f, Invalid);
    TestTrue(TEXT("负半径圆柱返回空"), Invalid.IsEmpty());
    Grid.QueryAABB(FBox(ForceInit), Invalid);
    TestTrue(TEXT("无效 FBox 返回空"), Invalid.IsEmpty());
    Grid.QueryAABB(FBox(FVector(NaNVal, 0.0, 0.0), FVector(10.0, 10.0, 10.0)), Invalid);
    TestTrue(TEXT("含 NaN 分量的 FBox 返回空"), Invalid.IsEmpty());
    Grid.QueryAABB(FBox(FVector(80.0, 80.0, 80.0), FVector(10.0, 10.0, 10.0)), Invalid);
    TestTrue(TEXT("同格颠倒 FBox 返回空"), Invalid.IsEmpty());
    TestFalse(TEXT("NaN 半径 QueryNearest 返回 false"), Grid.QueryNearest(FVector::ZeroVector, NaNVal, Nearest));

    Grid.Clear();
    Grid.QueryRadius(FVector::ZeroVector, 100.0f, Invalid);
    TestTrue(TEXT("空网格 QueryRadius 返回空"), Invalid.IsEmpty());
    Grid.QueryCylinder(FVector::ZeroVector, 100.0f, 100.0f, Invalid);
    TestTrue(TEXT("空网格 QueryCylinder 返回空"), Invalid.IsEmpty());
    Grid.QueryCone(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 100.0f, 45.0f, Invalid);
    TestTrue(TEXT("空网格 QueryCone 返回空"), Invalid.IsEmpty());
    Grid.QueryKNearest(FVector::ZeroVector, 100.0f, 3, Invalid);
    TestTrue(TEXT("空网格 QueryKNearest 返回空"), Invalid.IsEmpty());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialHashGrid3DConeAndKNearestTest,
    "Game.Spatial.HashGrid3D.ConeAndKNearest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 3D 锥体（锐角/精确45度边界/钝角/全向/斜向轴/零朝向）与 K 近邻严格升序及 K>16 堆溢出路径
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialHashGrid3DConeAndKNearestTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSpatialHashGrid3D<int32> Grid(100.0f);
    Grid.Insert(1, FVector(0.0, 0.0, 0.0));
    Grid.Insert(2, FVector(100.0, 0.0, 0.0));
    Grid.Insert(3, FVector(100.0, 100.0, 0.0));
    Grid.Insert(4, FVector(0.0, 100.0, 0.0));
    Grid.Insert(5, FVector(-50.0, 0.0, 86.60254));
    Grid.Insert(6, FVector(-100.0, 0.0, 0.0));

    TArray<int32> AcuteCone;
    Grid.QueryCone(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 150.0f, 45.0f, AcuteCone);
    TestEqual(TEXT("45度锐角锥体命中顶点 1、正前方 2 与 45度边界 3"), AcuteCone.Num(), 3);
    TestTrue(TEXT("含 1"), AcuteCone.Contains(1));
    TestTrue(TEXT("含 2"), AcuteCone.Contains(2));
    TestTrue(TEXT("含 3"), AcuteCone.Contains(3));

    TArray<int32> RightCone;
    Grid.QueryCone(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 150.0f, 90.0f, RightCone);
    TestEqual(TEXT("90度半球锥体精确命中 1、2、3、4"), RightCone.Num(), 4);
    TestTrue(TEXT("90度含 4"), RightCone.Contains(4));
    TestFalse(TEXT("90度不含 120度 5"), RightCone.Contains(5));

    TArray<int32> ObtuseCone;
    Grid.QueryCone(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 150.0f, 125.0f, ObtuseCone);
    TestEqual(TEXT("125度钝角锥体命中 1~5，排除背后 6"), ObtuseCone.Num(), 5);
    TestTrue(TEXT("含 120度 5"), ObtuseCone.Contains(5));
    TestFalse(TEXT("不含背后 6"), ObtuseCone.Contains(6));

    TArray<int32> DiagonalCone;
    Grid.QueryCone(FVector::ZeroVector, FVector(1.0, 1.0, 0.0), 150.0f, 10.0f, DiagonalCone);
    TestEqual(TEXT("斜向 (1,1,0) 窄锥命中顶点 1 与 3"), DiagonalCone.Num(), 2);
    TestTrue(TEXT("含 3"), DiagonalCone.Contains(3));

    TArray<int32> FullCone;
    Grid.QueryCone(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 150.0f, 180.0f, FullCone);
    TestEqual(TEXT("180度半角命中全部 6 点"), FullCone.Num(), 6);

    TArray<int32> ZeroDirAcute;
    Grid.QueryCone(FVector::ZeroVector, FVector::ZeroVector, 150.0f, 45.0f, ZeroDirAcute);
    TestTrue(TEXT("零朝向锐角锥返回空"), ZeroDirAcute.IsEmpty());

    TArray<int32> ZeroDirObtuse;
    Grid.QueryCone(FVector::ZeroVector, FVector::ZeroVector, 150.0f, 120.0f, ZeroDirObtuse);
    TestTrue(TEXT("零朝向钝角锥返回空"), ZeroDirObtuse.IsEmpty());

    TSpatialHashGrid3D<int32> KGrid(100.0f);
    for (int32 Id = 1; Id <= 30; ++Id) {
        KGrid.Insert(Id, FVector(static_cast<double>(Id) * 20.0, 0.0, 0.0));
    }

    TArray<int32> K24;
    KGrid.QueryKNearest(FVector::ZeroVector, 1000.0f, 24, K24);
    TestEqual(TEXT("3D K=24 溢出内联容量路径命中 24 个"), K24.Num(), 24);
    for (int32 Idx = 0; Idx < K24.Num(); ++Idx) {
        TestEqual(TEXT("3D K=24 严格按距离升序排列"), K24[Idx], Idx + 1);
    }

    TArray<int32> ZeroK;
    KGrid.QueryKNearest(FVector::ZeroVector, 1000.0f, 0, ZeroK);
    TestTrue(TEXT("K<=0 返回空"), ZeroK.IsEmpty());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialHashGrid3DLwcFlatAndSparseTest,
    "Game.Spatial.HashGrid3D.LargeWorldFlatMapAndSparseFastPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 3D 大世界坐标零假阴性剪枝、平面地图多格分布查询正确性、稀疏大半径快路径与流式原语
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialHashGrid3DLwcFlatAndSparseTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSpatialHashGrid3D<int32> LwcGrid(500.0f);
    const FVector FarPoint(4999999.9, -4999999.9, 1000000.0);
    TestTrue(TEXT("大世界三维坐标插入成功"), LwcGrid.Insert(88, FarPoint).IsSuccess());

    TArray<int32> LwcHits;
    LwcGrid.QueryRadius(FVector(4999999.5, -4999999.5, 1000000.0), 0.6f, LwcHits);
    TestEqual(TEXT("3D 50km 格边界微距半径查询零假阴性命中"), LwcHits.Num(), 1);
    if (!LwcHits.IsEmpty()) {
        TestEqual(TEXT("命中元素为 88"), LwcHits[0], 88);
    }

    TSpatialHashGrid3D<int32> FlatGrid(500.0f);
    FlatGrid.Reserve(400);
    for (int32 X = 0; X < 20; ++X) {
        for (int32 Y = 0; Y < 20; ++Y) {
            FlatGrid.Insert(X * 20 + Y, FVector(static_cast<double>(X) * 500.0 + 100.0, static_cast<double>(Y) * 500.0 + 100.0, 0.0));
        }
    }
    TestEqual(TEXT("平面地图 400 个不同单元格生成 400 个独立活跃桶"), FlatGrid.GetBucketCount(), 400);

    TArray<int32> FlatHits;
    FlatGrid.QueryRadius(FVector(5100.0, 5100.0, 0.0), 550.0f, FlatHits);
    TestEqual(TEXT("平面地图半径 550 精确命中中心与上下左右 5 个相邻格元素"), FlatHits.Num(), 5);

    TSpatialHashGrid3D<int32> SparseGrid(500.0f);
    for (int32 Id = 1; Id <= 10; ++Id) {
        SparseGrid.Insert(Id, FVector(static_cast<double>(Id) * 1000.0, 0.0, 0.0));
    }

    TArray<int32> HugeHits;
    SparseGrid.QueryRadius(FVector::ZeroVector, 1.0e8f, HugeHits);
    TestEqual(TEXT("3D 稀疏超大半径查询命中全部 10 个且零溢出卡顿"), HugeHits.Num(), 10);

    SparseGrid.QueryAABB(FBox(FVector(-1.0e8), FVector(1.0e8)), HugeHits);
    TestEqual(TEXT("3D 稀疏超大 AABB 命中全部 10 个"), HugeHits.Num(), 10);

    SparseGrid.QueryCylinder(FVector::ZeroVector, 1.0e8f, 1.0e8f, HugeHits);
    TestEqual(TEXT("3D 稀疏超大圆柱命中全部 10 个"), HugeHits.Num(), 10);

    SparseGrid.QueryCone(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 1.0e8f, 30.0f, HugeHits);
    TestEqual(TEXT("3D 稀疏超大锥体命中全部 10 个"), HugeHits.Num(), 10);

    int32 HugeNearest = 0;
    TestTrue(TEXT("3D 稀疏超大半径最近邻命中"), SparseGrid.QueryNearest(FVector(990.0, 0.0, 0.0), 1.0e8f, HugeNearest));
    TestEqual(TEXT("最近为 1"), HugeNearest, 1);

    SparseGrid.QueryKNearest(FVector(990.0, 0.0, 0.0), 1.0e8f, 4, HugeHits);
    TestEqual(TEXT("3D 稀疏超大半径 K=4 返回 4 个无重复"), HugeHits.Num(), 4);
    if (HugeHits.Num() == 4) {
        TestEqual(TEXT("第 1 近为 1"), HugeHits[0], 1);
        TestEqual(TEXT("第 4 近为 4"), HugeHits[3], 4);
    }

    int32 StreamCount = 0;
    SparseGrid.QueryRadiusMatching(FVector::ZeroVector, 3500.0f, [&StreamCount](int32, const FVector&) { ++StreamCount; });
    TestEqual(TEXT("3D QueryRadiusMatching 命中 3 个"), StreamCount, 3);

    int32 ConeStreamCount = 0;
    SparseGrid.QueryConeMatching(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 2500.0f, 30.0f, [&ConeStreamCount](int32) { ++ConeStreamCount; });
    TestEqual(TEXT("3D QueryConeMatching 命中 2 个"), ConeStreamCount, 2);

    int32 CylStreamCount = 0;
    SparseGrid.QueryCylinderMatching(FVector::ZeroVector, 2500.0f, 100.0f, [&CylStreamCount](int32) { ++CylStreamCount; });
    TestEqual(TEXT("3D QueryCylinderMatching 命中 2 个"), CylStreamCount, 2);

    int32 KStreamCount = 0;
    SparseGrid.QueryKNearestMatching(FVector::ZeroVector, 5500.0f, 3, [&KStreamCount](int32, double) { ++KStreamCount; });
    TestEqual(TEXT("3D QueryKNearestMatching 命中 3 个"), KStreamCount, 3);

    TSpatialHashGrid3D<int32> ClampedGrid(500.0f);
    const FVector BeyondMax(6.0e8, 0.0, 0.0);
    const FVector BeyondMin(-6.0e8, 0.0, 0.0);
    TestTrue(TEXT("3D 超越 21 位正上限坐标插入成功"), ClampedGrid.Insert(801, BeyondMax).IsSuccess());
    TestTrue(TEXT("3D 超越 21 位负下限坐标插入成功"), ClampedGrid.Insert(802, BeyondMin).IsSuccess());

    TArray<int32> ClampedHits;
    ClampedGrid.QueryRadius(BeyondMax, 100.0f, ClampedHits);
    TestEqual(TEXT("3D MAX_CELL_COORD 钳制格球形查询零假阴性漏检"), ClampedHits.Num(), 1);
    if (!ClampedHits.IsEmpty()) {
        TestEqual(TEXT("精准命中超越正上限元素 801"), ClampedHits[0], 801);
    }

    ClampedGrid.QueryRadius(BeyondMin, 100.0f, ClampedHits);
    TestEqual(TEXT("3D MIN_CELL_COORD 钳制格球形查询零假阴性漏检"), ClampedHits.Num(), 1);
    if (!ClampedHits.IsEmpty()) {
        TestEqual(TEXT("精准命中超越负下限元素 802"), ClampedHits[0], 802);
    }

    ClampedGrid.QueryCylinder(BeyondMax, 100.0f, 100.0f, ClampedHits);
    TestEqual(TEXT("3D MAX_CELL_COORD 钳制格圆柱体查询零假阴性漏检"), ClampedHits.Num(), 1);

    TSpatialHashGrid3D<int32> CenterSparseKGrid(500.0f);
    CenterSparseKGrid.Insert(1, FVector(10.0, 0.0, 0.0));
    CenterSparseKGrid.Insert(2, FVector(40.0, 0.0, 0.0));
    CenterSparseKGrid.Insert(3, FVector(40.0, 0.0, 0.0));
    TArray<int32> TieKResults;
    CenterSparseKGrid.QueryKNearest(FVector::ZeroVector, 1.0e8f, 2, TieKResults);
    TestEqual(TEXT("3D 中心格已有 >=K 元素且存在等距项时 K=2 精准返回 2 个"), TieKResults.Num(), 2);
    if (TieKResults.Num() == 2) {
        TestEqual(TEXT("3D 最近为 1"), TieKResults[0], 1);
        TestTrue(TEXT("3D 次近为等距项 2 或 3"), TieKResults[1] == 2 || TieKResults[1] == 3);
    }

    return true;
}

#endif
