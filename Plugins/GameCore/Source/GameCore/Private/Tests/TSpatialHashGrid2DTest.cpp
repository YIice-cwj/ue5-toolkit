/**
 * @file TSpatialHashGrid2DTest.cpp
 * @brief TSpatialHashGrid2D 纯逻辑空间哈希网格单元测试
 */

#include "Spatial/TSpatialHashGrid2D.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialHashGridLifecycleAndBucketsTest,
    "Game.Core.Spatial.HashGrid2D.LifecycleAndBuckets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证空间哈希网格元素的插入、重复拦截、同桶原地更新、跨桶迁移与移除生命周期
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialHashGridLifecycleAndBucketsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSpatialHashGrid2D<int32> Grid(500.0f);
    TestEqual(TEXT("初始元素数量为 0"), Grid.Num(), 0);
    TestEqual(TEXT("网格单元边长匹配"), Grid.GetCellSize(), 500.0f);

    TestTrue(TEXT("插入元素 1 成功"), Grid.Insert(1, FVector2D(100.0f, 100.0f)));
    TestFalse(TEXT("重复插入同名元素被拦截"), Grid.Insert(1, FVector2D(200.0f, 200.0f)));
    TestTrue(TEXT("Contains 包含元素 1"), Grid.Contains(1));
    TestEqual(TEXT("元素总数为 1"), Grid.Num(), 1);
    TestEqual(TEXT("活跃桶数量为 1"), Grid.GetBucketCount(), 1);

    FVector2D StoredLoc;
    TestTrue(TEXT("成功获取元素坐标"), Grid.GetLocation(1, StoredLoc));
    TestEqual(TEXT("坐标 X 精确匹配"), StoredLoc.X, 100.0);
    TestEqual(TEXT("坐标 Y 精确匹配"), StoredLoc.Y, 100.0);

    TestTrue(TEXT("同单元格内坐标微移更新成功"), Grid.Update(1, FVector2D(150.0f, 150.0f)));
    TestEqual(TEXT("同单元格更新不增加新桶"), Grid.GetBucketCount(), 1);

    TestTrue(TEXT("跨单元格远距离坐标更新成功"), Grid.Update(1, FVector2D(2500.0f, 2500.0f)));
    TestEqual(TEXT("跨桶迁移后旧空桶自动释放活跃桶数仍为 1"), Grid.GetBucketCount(), 1);

    TestTrue(TEXT("移除元素 1 成功"), Grid.Remove(1));
    TestFalse(TEXT("移除后 Contains 为假"), Grid.Contains(1));
    TestEqual(TEXT("移除后元素总数为 0"), Grid.Num(), 0);
    TestEqual(TEXT("移除后活跃桶彻底归零"), Grid.GetBucketCount(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialHashGridGeometricQueriesTest,
    "Game.Core.Spatial.HashGrid2D.GeometricQueries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证空间哈希网格半径圆形查询、矩形包围盒查询与最近目标查找
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialHashGridGeometricQueriesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSpatialHashGrid2D<int32> Grid(200.0f);

    Grid.Insert(101, FVector2D(0.0f, 0.0f));
    Grid.Insert(102, FVector2D(50.0f, 0.0f));
    Grid.Insert(103, FVector2D(0.0f, 80.0f));
    Grid.Insert(104, FVector2D(300.0f, 300.0f));
    Grid.Insert(105, FVector2D(1000.0f, 1000.0f));

    TestEqual(TEXT("插入 5 个测试点"), Grid.Num(), 5);

    TArray<int32> CircleResults;
    Grid.QueryRadius(FVector2D(0.0f, 0.0f), 100.0f, CircleResults);
    TestEqual(TEXT("半径 100 范围内命中 3 个近处目标"), CircleResults.Num(), 3);
    TestTrue(TEXT("命中点包含 101"), CircleResults.Contains(101));
    TestTrue(TEXT("命中点包含 102"), CircleResults.Contains(102));
    TestTrue(TEXT("命中点包含 103"), CircleResults.Contains(103));
    TestFalse(TEXT("远处的 104 未被命中"), CircleResults.Contains(104));

    TArray<int32> BoxResults;
    const FBox2D QueryBox(FVector2D(-10.0f, -10.0f), FVector2D(60.0f, 10.0f));
    Grid.QueryAABB(QueryBox, BoxResults);
    TestEqual(TEXT("AABB 范围仅框中 101 与 102"), BoxResults.Num(), 2);
    TestTrue(TEXT("AABB 命中 101"), BoxResults.Contains(101));
    TestTrue(TEXT("AABB 命中 102"), BoxResults.Contains(102));

    int32 NearestTarget = 0;
    const bool bFound = Grid.QueryNearest(FVector2D(10.0f, 10.0f), 200.0f, NearestTarget);
    TestTrue(TEXT("最近目标查询成功"), bFound);
    TestEqual(TEXT("离 (10, 10) 最近的目标为 101"), NearestTarget, 101);

    int32 FarTarget = 0;
    const bool bFarFound = Grid.QueryNearest(FVector2D(5000.0f, 5000.0f), 100.0f, FarTarget);
    TestFalse(TEXT("检索半径覆盖不到时返回 false"), bFarFound);

    Grid.Clear();
    TestEqual(TEXT("Clear 后元素总数归零"), Grid.Num(), 0);
    TestEqual(TEXT("Clear 后桶数量归零"), Grid.GetBucketCount(), 0);

    return true;
}

#endif
