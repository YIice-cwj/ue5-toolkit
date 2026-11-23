/**
 * @file StaticDataSubsystemColdStartTest.cpp
 * @brief 静态数据子系统冷启动确定性、GetPtr/Get 契约与 .bin 导出验证测试
 */

#include "Generated/Data/StaticData/UStaticDataSubsystem.h"

#if WITH_AUTOMATION_TESTS

#include "Async/FGameThreadTaskRunner.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace {

/**
 * @brief 创建带独立世界的临时 GameInstance 并取得其静态数据子系统
 * @param OutGameInstance 输出临时 GameInstance（调用方负责 Shutdown）
 * @return 子系统指针；创建失败返回 nullptr
 */
UStaticDataSubsystem* CreateTestSubsystem(UGameInstance*& OutGameInstance) {
    if (GEngine == nullptr) {
        return nullptr;
    }
    OutGameInstance = NewObject<UGameInstance>(GEngine);
    OutGameInstance->InitializeStandalone(FName(TEXT("StaticDataColdStartTestWorld")));
    return OutGameInstance->GetSubsystem<UStaticDataSubsystem>();
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FStaticDataSubsystemColdStartTest,
    "Game.Data.Subsystem.ColdStartDeterminism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 冷启动确定性测试：GetPtr/Get 契约、连续两次同步加载摘要一致、旧快照零触碰与版本号严格递增
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FStaticDataSubsystemColdStartTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    UStaticDataSubsystem* Subsystem = CreateTestSubsystem(GameInstance);
    TestNotNull(TEXT("临时 GameInstance 上的静态数据子系统有效"), Subsystem);
    if (!Subsystem) {
        return false;
    }

    TestEqual(TEXT("GetPtr(GameInstance) 返回正确子系统"), UStaticDataSubsystem::GetPtr(GameInstance), Subsystem);
    TestEqual(TEXT("GetPtr(Subsystem) 支持同级子系统解析"), UStaticDataSubsystem::GetPtr(Subsystem), Subsystem);
    TestEqual(TEXT("GetPtr(World) 返回正确子系统"), UStaticDataSubsystem::GetPtr(GameInstance->GetWorld()), Subsystem);
    TestEqual(TEXT("Get(GameInstance) 返回强引用"), &UStaticDataSubsystem::Get(GameInstance), Subsystem);
    TestNull(TEXT("GetPtr(nullptr) 安全返回空"), UStaticDataSubsystem::GetPtr(nullptr));

    TestTrue(TEXT("GameInstance 初始化后冷启动加载必须完成"), Subsystem->IsAllDataLoaded());
    TestTrue(TEXT("GameInstance 初始化后快照必须有效"), Subsystem->GetSnapshot().IsValid());
    const uint32 VersionBefore = Subsystem->GetDataVersion();

    const TGameStatusResult<void> FirstLoadResult = Subsystem->LoadAllStaticData(false);
    TestTrue(TEXT("首次显式同步加载必须成功"), FirstLoadResult.IsSuccess());
    TestEqual(TEXT("首次显式加载后版本号必须递增 1"), Subsystem->GetDataVersion(), VersionBefore + 1);
    TestTrue(TEXT("加载成功后必须标记为已加载"), Subsystem->IsAllDataLoaded());
    TestTrue(TEXT("加载成功后快照必须有效"), Subsystem->GetSnapshot().IsValid());
    if (FirstLoadResult.IsFailure() || !Subsystem->GetSnapshot().IsValid()) {
        GameInstance->Shutdown();
        return false;
    }

    const FStaticDataContentDigest FirstDigest = Subsystem->BuildLoadedContentDigest();
    TestTrue(TEXT("首轮内容摘要必须包含表"), FirstDigest.TableCount > 0);

    const TSharedPtr<const FStaticDataSnapshot> FirstSnapshot = Subsystem->GetSnapshot();
    const uint32 FirstSnapshotVersion = FirstSnapshot->DataVersion;

    const TGameStatusResult<void> SecondLoadResult = Subsystem->LoadAllStaticData(false);
    TestTrue(TEXT("第二次同步加载必须成功"), SecondLoadResult.IsSuccess());
    TestEqual(TEXT("第二次加载后版本号必须再递增 1"), Subsystem->GetDataVersion(), VersionBefore + 2);
    TestEqual(TEXT("已发布的旧快照版本号不得被改写"), FirstSnapshot->DataVersion, FirstSnapshotVersion);

    const FStaticDataContentDigest SecondDigest = Subsystem->BuildLoadedContentDigest();
    TestEqual(TEXT("两次加载的表数必须一致"), SecondDigest.TableCount, FirstDigest.TableCount);
    TestEqual(TEXT("两次加载的行数必须一致"), SecondDigest.RowCount, FirstDigest.RowCount);
    TestEqual(TEXT("两次加载的内容摘要必须一致"), SecondDigest.Digest, FirstDigest.Digest);

    const uint32 VersionBeforeAsync = Subsystem->GetDataVersion();
    TestTrue(TEXT("异步热重载派发成功"), Subsystem->ReloadAllStaticData().IsSuccess());
    FGameThreadTaskRunner::FlushInFlightTasks();
    TestEqual(TEXT("异步热重载完成后版本号递增 1"), Subsystem->GetDataVersion(), VersionBeforeAsync + 1);

    const FString BinDirPath = FPaths::ProjectContentDir() / TEXT("DataTables_Bin");
    const FString BinFilePath = BinDirPath / TEXT("StaticDataCache.bin");
    const bool bBinExistedBefore = FPaths::FileExists(BinFilePath);
    TArray<uint8> PreviousBinBytes;
    if (bBinExistedBefore) {
        (void)FFileHelper::LoadFileToArray(PreviousBinBytes, *BinFilePath);
    }
    const TGameStatusResult<void> ExportBinResult = UStaticDataSubsystem::ExportStaticDataBin();
    TestTrue(TEXT("编辑器下静态数据 .bin 导出应成功"), ExportBinResult.IsSuccess());
    if (bBinExistedBefore) {
        (void)FFileHelper::SaveArrayToFile(PreviousBinBytes, *BinFilePath);
    } else {
        IFileManager::Get().Delete(*BinFilePath, false, true);
        IFileManager::Get().DeleteDirectory(*BinDirPath, false, false);
    }

    GameInstance->Shutdown();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
