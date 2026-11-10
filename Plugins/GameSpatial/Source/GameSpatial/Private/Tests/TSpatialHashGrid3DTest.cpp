/**
 * @file TSpatialHashGrid3DTest.cpp
 * @brief TSpatialHashGrid3D 纯逻辑三维空间哈希网格自动化单元测试
 */

#include "Spatial/TSpatialHashGrid3D.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialHashGrid3DLifecycleTest,
    "Game.Spatial.HashGrid3D.LifecycleAndBuckets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 3D 空间哈希网格元素的插入、去重拦截、同桶微移、跨桶迁移与移除生命周期
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialHashGrid3DLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSpatialHashGrid3D<int32> Grid(500.0f);
    TestEqual(TEXT("初始元素数量为 0"), Grid.Num(), 0);
    TestEqual(TEXT("单元格边长符合预期"), Grid.GetCellSize(), 500.0f);

    const TGameStatusResult<void> InsertResult = Grid.Insert(1, FVector(100.0f, 100.0f, 50.0f));
    TestTrue(TEXT("插入元素 1 成功"), InsertResult.IsSuccess());

    const TGameStatusResult<void> DuplicateInsert = Grid.Insert(1, FVector(200.0f, 200.0f, 100.0f));
    TestTrue(TEXT("重复插入同 ID 元素被拦截为失败态"), DuplicateInsert.IsFailure());
    TestEqual(TEXT("重复插入错误码为 6002"), DuplicateInsert.GetError().ErrorCode, 6002);

    TestTrue(TEXT("Contains 确认包含元素 1"), Grid.Contains(1));
    TestEqual(TEXT("当前元素总数为 1"), Grid.Num(), 1);
    TestEqual(TEXT("活跃桶数量为 1"), Grid.GetBucketCount(), 1);

    const TGameStatusResult<FVector> LocResult = Grid.GetLocation(1);
    TestTrue(TEXT("强类型契约获取坐标成功"), LocResult.IsSuccess());
    TestEqual(TEXT("坐标 X 精确一致"), LocResult.GetValue().X, 100.0);
    TestEqual(TEXT("坐标 Y 精确一致"), LocResult.GetValue().Y, 100.0);
    TestEqual(TEXT("坐标 Z 精确一致"), LocResult.GetValue().Z, 50.0);

    const TGameStatusResult<FVector> NonExistLoc = Grid.GetLocation(999);
    TestTrue(TEXT("查询不存在元素返回失败态"), NonExistLoc.IsFailure());
    TestEqual(TEXT("不存在元素错误码为 6001"), NonExistLoc.GetError().ErrorCode, 6001);

    FVector StoredLoc;
    TestTrue(TEXT("兼容重载成功获取元素 1 坐标"), Grid.GetLocation(1, StoredLoc));
    TestEqual(TEXT("坐标 X 精确一致"), StoredLoc.X, 100.0);
    TestEqual(TEXT("坐标 Y 精确一致"), StoredLoc.Y, 100.0);
    TestEqual(TEXT("坐标 Z 精确一致"), StoredLoc.Z, 50.0);

    TestTrue(TEXT("同单元格内小幅度坐标微移成功"), Grid.Update(1, FVector(120.0f, 140.0f, 60.0f)).IsSuccess());
    TestEqual(TEXT("同单元格微移不产生新桶"), Grid.GetBucketCount(), 1);

    TestTrue(TEXT("跨单元格远距离更新成功"), Grid.Update(1, FVector(2500.0f, 2500.0f, 1500.0f)).IsSuccess());
    TestEqual(TEXT("跨桶迁移后旧空桶自动清理活跃桶数仍为 1"), Grid.GetBucketCount(), 1);

    TestTrue(TEXT("移除元素 1 成功"), Grid.Remove(1).IsSuccess());
    TestFalse(TEXT("移除后 Contains 返回假"), Grid.Contains(1));
    TestEqual(TEXT("移除后元素总数归零"), Grid.Num(), 0);
    TestEqual(TEXT("移除后活跃桶彻底清空"), Grid.GetBucketCount(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialHashGrid3DQueriesTest,
    "Game.Spatial.HashGrid3D.GeometricQueries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 3D 空间哈希网格球形半径查询、三维 AABB 盒查询、圆柱体查询及最近目标查找
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialHashGrid3DQueriesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSpatialHashGrid3D<int32> Grid(200.0f);

    Grid.Insert(101, FVector(0.0f, 0.0f, 0.0f));
    Grid.Insert(102, FVector(50.0f, 0.0f, 0.0f));
    Grid.Insert(103, FVector(0.0f, 80.0f, 20.0f));
    Grid.Insert(104, FVector(0.0f, 0.0f, 300.0f));
    Grid.Insert(105, FVector(2000.0f, 2000.0f, 2000.0f));

    TestEqual(TEXT("插入 5 个 3D 锚点"), Grid.Num(), 5);

    TArray<int32> SphereResults;
    Grid.QueryRadius(FVector(0.0f, 0.0f, 0.0f), 100.0f, SphereResults);
    TestEqual(TEXT("球形半径 100 命中 3 个近处点"), SphereResults.Num(), 3);
    TestTrue(TEXT("球内包含 101"), SphereResults.Contains(101));
    TestTrue(TEXT("球内包含 102"), SphereResults.Contains(102));
    TestTrue(TEXT("球内包含 103"), SphereResults.Contains(103));
    TestFalse(TEXT("Z 轴高处的 104 未被命中"), SphereResults.Contains(104));

    TArray<int32> BoxResults;
    const FBox QueryBox(FVector(-10.0f, -10.0f, -10.0f), FVector(60.0f, 10.0f, 10.0f));
    Grid.QueryAABB(QueryBox, BoxResults);
    TestEqual(TEXT("三维 AABB 仅框中 101 与 102"), BoxResults.Num(), 2);
    TestTrue(TEXT("AABB 命中 101"), BoxResults.Contains(101));
    TestTrue(TEXT("AABB 命中 102"), BoxResults.Contains(102));

    TArray<int32> CylinderResults;
    Grid.QueryCylinder(FVector(0.0f, 0.0f, 100.0f), 100.0f, 250.0f, CylinderResults);
    TestTrue(TEXT("圆柱体垂直范围覆盖 101 (Z=0)"), CylinderResults.Contains(101));
    TestTrue(TEXT("圆柱体垂直范围覆盖 104 (Z=300)"), CylinderResults.Contains(104));
    TestFalse(TEXT("远端 105 不在圆柱体内"), CylinderResults.Contains(105));

    int32 NearestTarget = 0;
    const bool bFound = Grid.QueryNearest(FVector(10.0f, 10.0f, 10.0f), 150.0f, NearestTarget);
    TestTrue(TEXT("三维近邻查询成功"), bFound);
    TestEqual(TEXT("最近目标为 101"), NearestTarget, 101);

    int32 FarTarget = 0;
    const bool bFarFound = Grid.QueryNearest(FVector(8000.0f, 8000.0f, 8000.0f), 100.0f, FarTarget);
    TestFalse(TEXT("超出截断半径返回 false"), bFarFound);

    Grid.Clear();
    TestEqual(TEXT("Clear 后元素总数归零"), Grid.Num(), 0);
    TestEqual(TEXT("Clear 后活跃桶归零"), Grid.GetBucketCount(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialHashGrid3DNegativeAndColocationTest,
    "Game.Spatial.HashGrid3D.NegativeAndColocation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 3D 网格在负空间象限跨原点检索、同桶多元素共存与精确移除后的正确性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialHashGrid3DNegativeAndColocationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSpatialHashGrid3D<int32> Grid(250.0f);

    TestTrue(TEXT("负象限插入 A 成功"), Grid.Insert(1, FVector(-380.0f, -380.0f, -380.0f)).IsSuccess());
    TestTrue(TEXT("负象限同桶插入 B 成功"), Grid.Insert(2, FVector(-420.0f, -420.0f, -420.0f)).IsSuccess());
    TestEqual(TEXT("同桶共存桶数仍为 1"), Grid.GetBucketCount(), 1);
    TestEqual(TEXT("元素数总共 2 个"), Grid.Num(), 2);

    TestTrue(TEXT("正象限插入 C 成功"), Grid.Insert(3, FVector(500.0f, 500.0f, 500.0f)).IsSuccess());
    TestEqual(TEXT("不同桶插入后桶数增加"), Grid.GetBucketCount(), 2);

    TestTrue(TEXT("原点插入 D 成功"), Grid.Insert(4, FVector(0.0f, 0.0f, 0.0f)).IsSuccess());

    TArray<int32> CrossOriginResults;
    Grid.QueryAABB(FBox(FVector(-600.0f, -600.0f, -600.0f), FVector(100.0f, 100.0f, 100.0f)), CrossOriginResults);
    TestEqual(TEXT("跨原点框选命中 3 个目标 (1, 2, 4)"), CrossOriginResults.Num(), 3);
    TestTrue(TEXT("包含 1"), CrossOriginResults.Contains(1));
    TestTrue(TEXT("包含 2"), CrossOriginResults.Contains(2));
    TestTrue(TEXT("包含 4"), CrossOriginResults.Contains(4));
    TestFalse(TEXT("正象限深处的 3 未被命中"), CrossOriginResults.Contains(3));

    int32 NearestNeg = 0;
    TestTrue(TEXT("负象限近邻检索成功"), Grid.QueryNearest(FVector(-382.0f, -382.0f, -382.0f), 50.0f, NearestNeg));
    TestEqual(TEXT("离 (-382,-382,-382) 最近为 1"), NearestNeg, 1);

    TestTrue(TEXT("移除同桶中的 1 成功"), Grid.Remove(1).IsSuccess());
    TestFalse(TEXT("1 已被移除"), Grid.Contains(1));
    TestTrue(TEXT("2 依然有效存在"), Grid.Contains(2));
    TestEqual(TEXT("同桶移除单项后桶不被清空"), Grid.GetBucketCount(), 3);

    TArray<int32> RemainderResults;
    Grid.QueryRadius(FVector(-420.0f, -420.0f, -420.0f), 20.0f, RemainderResults);
    TestEqual(TEXT("查询残留的 2 成功命中"), RemainderResults.Num(), 1);
    TestTrue(TEXT("命中 2"), RemainderResults.Contains(2));

    TArray<int32> DegenerateResults;
    Grid.QueryRadius(FVector(0.0f, 0.0f, 0.0f), -10.0f, DegenerateResults);
    TestTrue(TEXT("非法负半径查询直接安全返回空"), DegenerateResults.IsEmpty());

    Grid.Clear();
    TArray<int32> EmptyResults;
    Grid.QueryRadius(FVector(0.0f, 0.0f, 0.0f), 100.0f, EmptyResults);
    TestTrue(TEXT("空网格查询安全返回空"), EmptyResults.IsEmpty());

    int32 EmptyNearest = 0;
    TestFalse(TEXT("空网格近邻查询安全返回 false"), Grid.QueryNearest(FVector(0.0f, 0.0f, 0.0f), 100.0f, EmptyNearest));

    return true;
}

#endif
