/**
 * @file FDirectoryChangeWatcherTest.cpp
 * @brief 目录变更监听器自动化测试（生命周期、配置驱动、时间戳推进与变更广播），Shipping 构建自动剔除
 */

#include "Utils/FDirectoryChangeWatcher.h"
#include "Config/FConfigRegistry.h"

#if WITH_AUTOMATION_TESTS

#include "Async/TaskGraphInterfaces.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace {

/**
 * @brief 构造测试专用的临时监听目录路径
 * @param SubDirName 子目录名称
 * @return 绝对目录路径
 */
FString GetWatcherTestDirectory(const FString& SubDirName) {
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnitTest_Watcher"), SubDirName));
}

/**
 * @brief 清理测试临时目录及其内部文件
 * @param DirectoryPath 目录绝对路径
 */
void CleanupWatcherTestDirectory(const FString& DirectoryPath) {
    IFileManager::Get().DeleteDirectory(*DirectoryPath, false, true);
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameDirectoryChangeWatcherLifecycleTest, "Game.Data.IO.DirectoryChangeWatcher.Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证目录变更监听器启动、停止与配置项幂等注册生命周期
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 */
bool FGameDirectoryChangeWatcherLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FString TestDir = GetWatcherTestDirectory(TEXT("Lifecycle"));
    CleanupWatcherTestDirectory(TestDir);
    IFileManager::Get().MakeDirectory(*TestDir, true);

    const FName WatcherName(TEXT("UnitTestWatcher_Lifecycle"));
    const FName EnabledKey(TEXT("UnitTestWatcher_Lifecycle.Enabled"));

    {
        FDirectoryChangeWatcher Watcher(WatcherName, EnabledKey, TestDir);
        TestTrue(TEXT("构造后启停配置键已注册"), FConfigRegistry::Get().IsRegistered(EnabledKey));
        TestTrue(TEXT("构造后目录配置键已注册"), FConfigRegistry::Get().IsRegistered(FName(TEXT("DirectoryWatch.UnitTestWatcher_Lifecycle.Directory"))));

        Watcher.Start();
        Watcher.Start();
        Watcher.Stop();
        Watcher.Stop();
    }

    CleanupWatcherTestDirectory(TestDir);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameDirectoryChangeWatcherDetectionTest, "Game.Data.IO.DirectoryChangeWatcher.Detection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证写入文件后时间戳推进并触发观察者自增广播
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 */
bool FGameDirectoryChangeWatcherDetectionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FString TestDir = GetWatcherTestDirectory(TEXT("Detection"));
    CleanupWatcherTestDirectory(TestDir);
    IFileManager::Get().MakeDirectory(*TestDir, true);

    const FString BaselineFile = FPaths::Combine(TestDir, TEXT("baseline.json"));
    FFileHelper::SaveStringToFile(TEXT("initial_data"), *BaselineFile);
    IFileManager::Get().SetTimeStamp(*BaselineFile, FDateTime::UtcNow() - FTimespan::FromHours(1));

    const FName WatcherName(TEXT("UnitTestWatcher_Detection"));
    const FName EnabledKey(TEXT("UnitTestWatcher_Detection.Enabled"));

    FDirectoryChangeWatcher Watcher(WatcherName, EnabledKey, TestDir);
    FConfigRegistry::Get().SetConfig<float>(FName(TEXT("DirectoryWatch.UnitTestWatcher_Detection.IntervalSeconds")), 0.1f);

    FEvent* ChangeEvent = FPlatformProcess::GetSynchEventFromPool(false);
    std::atomic<uint32> LastReceivedSeq{0};

    const TSharedRef<FObserverSubscription> Subscription = Watcher.GetChangeObserver().Subscribe(
        [ChangeEvent, &LastReceivedSeq](const uint32& NewSeq) {
            LastReceivedSeq.store(NewSeq);
            ChangeEvent->Trigger();
        }
    );

    Watcher.Start();

    const FString NewFile = FPaths::Combine(TestDir, TEXT("new_file.json"));
    FFileHelper::SaveStringToFile(TEXT("updated_content"), *NewFile);
    IFileManager::Get().SetTimeStamp(*NewFile, FDateTime::UtcNow() + FTimespan::FromSeconds(10));

    constexpr int32 MaxTries = 40;
    bool bDetected = false;
    for (int32 TryIndex = 0; TryIndex < MaxTries; ++TryIndex) {
        FTSTicker::GetCoreTicker().Tick(0.15f);
        FPlatformProcess::Yield();
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        if (ChangeEvent->Wait(50)) {
            bDetected = true;
            break;
        }
    }

    TestTrue(TEXT("检测到文件变更并收到广播通知"), bDetected);
    TestTrue(TEXT("变更序号大于零"), LastReceivedSeq.load() > 0);

    Watcher.Stop();
    FPlatformProcess::ReturnSynchEventToPool(ChangeEvent);
    CleanupWatcherTestDirectory(TestDir);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameDirectoryChangeWatcherToggleTest, "Game.Data.IO.DirectoryChangeWatcher.EnabledToggle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证启停开关关闭时跳过扫描且不产生变更通知
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 */
bool FGameDirectoryChangeWatcherToggleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FString TestDir = GetWatcherTestDirectory(TEXT("Toggle"));
    CleanupWatcherTestDirectory(TestDir);
    IFileManager::Get().MakeDirectory(*TestDir, true);

    const FName WatcherName(TEXT("UnitTestWatcher_Toggle"));
    const FName EnabledKey(TEXT("UnitTestWatcher_Toggle.Enabled"));

    FDirectoryChangeWatcher Watcher(WatcherName, EnabledKey, TestDir);
    FConfigRegistry::Get().SetConfig<bool>(EnabledKey, false);

    Watcher.Start();

    const FString NewFile = FPaths::Combine(TestDir, TEXT("disabled_test.json"));
    FFileHelper::SaveStringToFile(TEXT("should_not_trigger"), *NewFile);

    for (int32 Index = 0; Index < 5; ++Index) {
        FTSTicker::GetCoreTicker().Tick(0.1f);
    }

    TestEqual(TEXT("禁用状态下变更序号保持为 0"), Watcher.GetChangeObserver().Get(), 0u);

    Watcher.Stop();
    CleanupWatcherTestDirectory(TestDir);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
