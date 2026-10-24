/**
 * @file StaticDataSubsystemColdStartTest.cpp
 * @brief 静态数据子系统冷启动确定性测试（域无关：只校验加载状态、快照与版本不变量），Shipping 构建自动剔除
 */

#include "Generated/Data/StaticData/UStaticDataSubsystem.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"

namespace {

/**
 * @brief 创建带独立世界的临时 GameInstance 并取得其静态数据子系统
 * @param OutGameInstance 输出临时 GameInstance（调用方负责 Shutdown）
 * @return 子系统指针；创建失败返回 nullptr
 *
 * 实现思路：GameInstanceSubsystem 的 ClassWithin 为 UGameInstance，故以 GEngine 为 Outer
 * 构造 GameInstance 后调用 InitializeStandalone 建立独立世界上下文，不触碰编辑器现有世界。
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStaticDataSubsystemColdStartTest, "Game.Data.Subsystem.ColdStartDeterminism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 冷启动确定性测试：连续两次同步加载的内容摘要必须一致，旧快照零触碰且版本号严格递增
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

    TestTrue(TEXT("GameInstance 初始化后冷启动加载必须完成"), Subsystem->IsAllDataLoaded());
    TestTrue(TEXT("GameInstance 初始化后快照必须有效"), Subsystem->GetSnapshot().IsValid());
    const uint32 VersionBefore = Subsystem->GetDataVersion();

    const bool bFirstLoaded = Subsystem->LoadAllStaticData(false);
    TestTrue(TEXT("首次显式同步加载必须成功"), bFirstLoaded);
    TestEqual(TEXT("首次显式加载后版本号必须递增 1"), Subsystem->GetDataVersion(), VersionBefore + 1);
    TestTrue(TEXT("加载成功后必须标记为已加载"), Subsystem->IsAllDataLoaded());
    TestTrue(TEXT("加载成功后快照必须有效"), Subsystem->GetSnapshot().IsValid());
    if (!bFirstLoaded || !Subsystem->GetSnapshot().IsValid()) {
        GameInstance->Shutdown();
        return false;
    }

    const FStaticDataContentDigest FirstDigest = Subsystem->BuildLoadedContentDigest();
    TestTrue(TEXT("首轮内容摘要必须包含表"), FirstDigest.TableCount > 0);

    const TSharedPtr<const FStaticDataSnapshot> FirstSnapshot = Subsystem->GetSnapshot();
    const uint32 FirstSnapshotVersion = FirstSnapshot->DataVersion;

    const bool bSecondLoaded = Subsystem->LoadAllStaticData(false);
    TestTrue(TEXT("第二次同步加载必须成功"), bSecondLoaded);
    TestEqual(TEXT("第二次加载后版本号必须再递增 1"), Subsystem->GetDataVersion(), VersionBefore + 2);
    TestEqual(TEXT("已发布的旧快照版本号不得被改写"), FirstSnapshot->DataVersion, FirstSnapshotVersion);

    const FStaticDataContentDigest SecondDigest = Subsystem->BuildLoadedContentDigest();
    TestEqual(TEXT("两次加载的表数必须一致"), SecondDigest.TableCount, FirstDigest.TableCount);
    TestEqual(TEXT("两次加载的行数必须一致"), SecondDigest.RowCount, FirstDigest.RowCount);
    TestEqual(TEXT("两次加载的内容摘要必须一致"), SecondDigest.Digest, FirstDigest.Digest);

    GameInstance->Shutdown();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
