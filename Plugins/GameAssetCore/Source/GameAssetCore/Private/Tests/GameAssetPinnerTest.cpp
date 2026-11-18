/**
 * @file GameAssetPinnerTest.cpp
 * @brief FGameAssetPinner 与 FScopedPinnedAsset 内存保活及作用域守卫自动化测试
 */

#include "Asset/FGameAssetPinner.h"
#include "Asset/FScopedPinnedAsset.h"
#include "Library/UGameAssetBlueprintLibrary.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetPinnerLifecycleTest,
    "Game.Asset.Pinner.LifecycleAndCounting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameAssetPinner 的引用计数增减、非法路径守卫、归零注销与全局清空机制
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetPinnerLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameAssetPinner::ClearAllPinnedAssets();

    const FSoftObjectPath EmptyPath;
    const TGameStatusResult<void> EmptyPinResult = FGameAssetPinner::PinAsset(EmptyPath);
    TestTrue(TEXT("锁定空路径返回失败态"), EmptyPinResult.IsFailure());
    TestEqual(TEXT("空路径错误码为 1001"), EmptyPinResult.GetError().ErrorCode, 1001);
    TestFalse(TEXT("空路径 IsAssetPinned 为假"), FGameAssetPinner::IsAssetPinned(EmptyPath));
    TestEqual(TEXT("空路径引用计数为 0"), FGameAssetPinner::GetPinRefCount(EmptyPath), 0);
    TestNull(TEXT("空路径获取已锁定指针返回 nullptr"), FGameAssetPinner::GetPinnedAsset(EmptyPath));

    const TGameStatusResult<void> EmptyUnpinResult = FGameAssetPinner::UnpinAsset(EmptyPath);
    TestTrue(TEXT("解除空路径返回失败态"), EmptyUnpinResult.IsFailure());

    UGameInstance* ResidentTarget = NewObject<UGameInstance>();
    const FSoftObjectPath ResidentPath(ResidentTarget);

    TestFalse(TEXT("尚未锁定的路径 IsAssetPinned 为假"), FGameAssetPinner::IsAssetPinned(ResidentPath));
    const TGameStatusResult<void> UnpinnedResult = FGameAssetPinner::UnpinAsset(ResidentPath);
    TestTrue(TEXT("解除未锁定的路径返回失败态"), UnpinnedResult.IsFailure());
    TestEqual(TEXT("未锁定路径错误码为 1003"), UnpinnedResult.GetError().ErrorCode, 1003);

    const TGameStatusResult<void> PinResult1 = FGameAssetPinner::PinAsset(ResidentPath);
    TestTrue(TEXT("初次锁定有效驻留路径成功"), PinResult1.IsSuccess());
    TestTrue(TEXT("锁定后 IsAssetPinned 为真"), FGameAssetPinner::IsAssetPinned(ResidentPath));
    TestEqual(TEXT("初次锁定后引用计数为 1"), FGameAssetPinner::GetPinRefCount(ResidentPath), 1);
    TestEqual(TEXT("锁定后通过 Pinner 获取实例指针匹配"), FGameAssetPinner::GetPinnedAsset(ResidentPath), static_cast<UObject*>(ResidentTarget));
    TestEqual(TEXT("锁定后通过 Pinner 强类型获取匹配"), FGameAssetPinner::GetPinnedAsset<UGameInstance>(ResidentPath), ResidentTarget);
    TestEqual(TEXT("全局锁定总数为 1"), FGameAssetPinner::GetTotalPinnedCount(), 1);

    TArray<FSoftObjectPath> PinnedPaths;
    FGameAssetPinner::GetAllPinnedPaths(PinnedPaths);
    TestEqual(TEXT("导出的已锁定路径数量为 1"), PinnedPaths.Num(), 1);
    if (PinnedPaths.Num() == 1) {
        TestEqual(TEXT("导出的路径与目标匹配"), PinnedPaths[0], ResidentPath);
    }
    TestEqual(TEXT("蓝图导出路径数量为 1"), UGameAssetBlueprintLibrary::GetAllPinnedAssetPaths().Num(), 1);

    const TSoftObjectPtr<UObject> BpSoftPtr(ResidentTarget);
    TestTrue(TEXT("蓝图 IsAssetPinned 为真"), UGameAssetBlueprintLibrary::IsAssetPinned(BpSoftPtr));
    TestTrue(TEXT("蓝图 IsAssetLoaded 为真"), UGameAssetBlueprintLibrary::IsAssetLoaded(BpSoftPtr));
    TestEqual(TEXT("蓝图 GetLoadedAsset 匹配"), UGameAssetBlueprintLibrary::GetLoadedAsset(BpSoftPtr), static_cast<UObject*>(ResidentTarget));
    TestEqual(TEXT("蓝图 GetPinnedAsset 匹配"), UGameAssetBlueprintLibrary::GetPinnedAsset(BpSoftPtr), static_cast<UObject*>(ResidentTarget));
    TestEqual(TEXT("蓝图 GetAssetPinRefCount 为 1"), UGameAssetBlueprintLibrary::GetAssetPinRefCount(BpSoftPtr), 1);
    TestEqual(TEXT("蓝图 GetTotalPinnedAssetCount 为 1"), UGameAssetBlueprintLibrary::GetTotalPinnedAssetCount(), 1);

    TestTrue(TEXT("蓝图 PinAsset 增加引用计数成功"), UGameAssetBlueprintLibrary::PinAsset(BpSoftPtr));
    TestEqual(TEXT("蓝图锁定后引用计数为 2"), FGameAssetPinner::GetPinRefCount(ResidentPath), 2);
    TestTrue(TEXT("蓝图 UnpinAsset 扣减引用计数成功"), UGameAssetBlueprintLibrary::UnpinAsset(BpSoftPtr));
    TestEqual(TEXT("蓝图扣减后引用计数为 1"), FGameAssetPinner::GetPinRefCount(ResidentPath), 1);

    UGameInstance* ResidentTarget2 = NewObject<UGameInstance>();
    const TArray<TSoftObjectPtr<UObject>> BatchPinList = {
        BpSoftPtr,
        TSoftObjectPtr<UObject>(ResidentTarget2)
    };
    const int32 PinnedBatchCount = UGameAssetBlueprintLibrary::PinAssets(BatchPinList);
    TestEqual(TEXT("蓝图批量 PinAssets 成功锁定 2 项"), PinnedBatchCount, 2);
    TestEqual(TEXT("批量锁定后 ResidentTarget 引用计数为 2"), FGameAssetPinner::GetPinRefCount(ResidentPath), 2);
    TestEqual(TEXT("全局锁定总数为 2"), FGameAssetPinner::GetTotalPinnedCount(), 2);

    const int32 UnpinnedBatchCount = UGameAssetBlueprintLibrary::UnpinAssets(BatchPinList);
    TestEqual(TEXT("蓝图批量 UnpinAssets 成功扣减 2 项"), UnpinnedBatchCount, 2);
    TestEqual(TEXT("批量扣减后 ResidentTarget 引用计数恢复为 1"), FGameAssetPinner::GetPinRefCount(ResidentPath), 1);
    TestEqual(TEXT("全局锁定总数恢复为 1"), FGameAssetPinner::GetTotalPinnedCount(), 1);
    ResidentTarget2->MarkAsGarbage();

    const TGameStatusResult<void> PinResult2 = FGameAssetPinner::PinAsset(ResidentPath);
    TestTrue(TEXT("重复锁定同一路径递增引用计数成功"), PinResult2.IsSuccess());
    TestEqual(TEXT("重复锁定后引用计数为 2"), FGameAssetPinner::GetPinRefCount(ResidentPath), 2);

    const TGameStatusResult<void> UnpinResult1 = FGameAssetPinner::UnpinAsset(ResidentPath);
    TestTrue(TEXT("扣减第一次引用后返回成功"), UnpinResult1.IsSuccess());
    TestTrue(TEXT("一次 Unpin 后仍然被锁定"), FGameAssetPinner::IsAssetPinned(ResidentPath));
    TestEqual(TEXT("扣减一次后引用计数为 1"), FGameAssetPinner::GetPinRefCount(ResidentPath), 1);

    const TGameStatusResult<void> UnpinResult2 = FGameAssetPinner::UnpinAsset(ResidentPath);
    TestTrue(TEXT("扣减第二次引用后完全解除锁定返回成功"), UnpinResult2.IsSuccess());
    TestFalse(TEXT("引用归零后 IsAssetPinned 为假"), FGameAssetPinner::IsAssetPinned(ResidentPath));
    TestEqual(TEXT("归零后引用计数为 0"), FGameAssetPinner::GetPinRefCount(ResidentPath), 0);
    TestNull(TEXT("完全解锁后获取已锁定指针返回 nullptr"), FGameAssetPinner::GetPinnedAsset(ResidentPath));

    TArray<FSoftObjectPath> PinnedPathsAfterUnpin;
    FGameAssetPinner::GetAllPinnedPaths(PinnedPathsAfterUnpin);
    TestEqual(TEXT("完全解锁后导出的锁定路径数量为 0"), PinnedPathsAfterUnpin.Num(), 0);
    TestEqual(TEXT("完全解锁后蓝图导出路径数量为 0"), UGameAssetBlueprintLibrary::GetAllPinnedAssetPaths().Num(), 0);

    FGameAssetPinner::ClearAllPinnedAssets();
    ResidentTarget->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FScopedPinnedAssetRaiiTest,
    "Game.Asset.Pinner.ScopedGuardAndMove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FScopedPinnedAsset 的 RAII 析构释放、移动构造/赋值与强类型解引用访问
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FScopedPinnedAssetRaiiTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameAssetPinner::ClearAllPinnedAssets();

    UGameInstance* ResidentTarget = NewObject<UGameInstance>();
    const FSoftObjectPath ResidentPath(ResidentTarget);

    {
        FScopedPinnedAsset DefaultPin;
        TestFalse(TEXT("默认构造守卫不持有锁定"), DefaultPin.IsPinned());
        TestTrue(TEXT("默认构造守卫路径为空"), DefaultPin.GetPath().IsNull());
        TestFalse(TEXT("默认构造守卫布尔转换为假"), static_cast<bool>(DefaultPin));
        TestNull(TEXT("默认构造守卫 Get 返回 nullptr"), DefaultPin.Get());
        DefaultPin.Release();

        FScopedPinnedAsset ScopedPin(ResidentPath);
        TestTrue(TEXT("FScopedPinnedAsset 构造时接管锁定"), ScopedPin.IsPinned());
        TestTrue(TEXT("FScopedPinnedAsset 布尔转换为真"), static_cast<bool>(ScopedPin));
        TestEqual(TEXT("GetPath 返回正确路径"), ScopedPin.GetPath(), ResidentPath);
        TestEqual(TEXT("Get 返回原生 UObject 指针"), ScopedPin.Get(), static_cast<UObject*>(ResidentTarget));
        TestEqual(TEXT("Get<T> 强类型返回正确实例"), ScopedPin.Get<UGameInstance>(), ResidentTarget);
        TestTrue(TEXT("RAII 守卫内路径处于锁定状态"), FGameAssetPinner::IsAssetPinned(ResidentPath));

        {
            const TSoftObjectPtr<UGameInstance> SoftTargetPtr(ResidentTarget);
            FScopedPinnedAsset SoftConstructedPin(SoftTargetPtr);
            TestTrue(TEXT("由 TSoftObjectPtr 直接构造守卫有效"), SoftConstructedPin.IsPinned());
            TestEqual(TEXT("由 TSoftObjectPtr 构造守卫解析对象一致"), SoftConstructedPin.Get<UGameInstance>(), ResidentTarget);
            TestEqual(TEXT("嵌套作用域引用计数递增至 2"), FGameAssetPinner::GetPinRefCount(ResidentPath), 2);
        }
        TestEqual(TEXT("内层守卫析构后引用计数降为 1"), FGameAssetPinner::GetPinRefCount(ResidentPath), 1);

        FScopedPinnedAsset MovedPin(MoveTemp(ScopedPin));
        TestFalse(TEXT("移动构造后源守卫处于非锁定状态"), ScopedPin.IsPinned());
        TestTrue(TEXT("移动构造后目标守卫持有锁定"), MovedPin.IsPinned());
        TestTrue(TEXT("移动后资产仍然处于保活状态"), FGameAssetPinner::IsAssetPinned(ResidentPath));

        FScopedPinnedAsset AssignedPin;
        AssignedPin = MoveTemp(MovedPin);
        TestFalse(TEXT("移动赋值后源守卫处于非锁定状态"), MovedPin.IsPinned());
        TestTrue(TEXT("移动赋值后目标守卫持有锁定"), AssignedPin.IsPinned());
        TestEqual(TEXT("移动赋值后目标守卫路径匹配"), AssignedPin.GetPath(), ResidentPath);

        AssignedPin.Release();
        TestFalse(TEXT("主动 Release 后守卫变为非锁定"), AssignedPin.IsPinned());
        TestFalse(TEXT("主动 Release 解除底层资产锁定引用"), FGameAssetPinner::IsAssetPinned(ResidentPath));
    }
    TestFalse(TEXT("离开作用域后资产处于非保活状态"), FGameAssetPinner::IsAssetPinned(ResidentPath));

    ResidentTarget->MarkAsGarbage();
    return true;
}

#endif
