/**
 * @file FDirectoryChangeWatcherTest.cpp
 * @brief 目录变更监听器自动化测试（非递归扫描修复、删除/回滚指纹检测、配置动态变更与生命周期取消）
 */

#include "Utils/FDirectoryChangeWatcher.h"
#include "Config/FConfigRegistry.h"

#if WITH_AUTOMATION_TESTS

#include "Async/FGameThreadTaskRunner.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <atomic>

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

/**
 * @brief 推进 Ticker 并刷新在途任务，直至变更序号超过指定基线或达到上限
 * @param Watcher 目标监听器
 * @param ExpectedMinSeq 期望达到的最小序号
 * @return 达到期望序号返回 true
 */
bool PumpUntilWatcherSequence(FDirectoryChangeWatcher& Watcher, uint32 ExpectedMinSeq) {
    constexpr int32 MaxTries = 30;
    for (int32 TryIndex = 0; TryIndex < MaxTries; ++TryIndex) {
        FTSTicker::GetCoreTicker().Tick(0.15f);
        FGameThreadTaskRunner::FlushInFlightTasks();
        if (Watcher.GetChangeObserver().Get() >= ExpectedMinSeq) {
            return true;
        }
    }
    return false;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDirectoryChangeWatcherLifecycleTest,
    "Game.Data.IO.DirectoryChangeWatcher.Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证目录变更监听器启动、停止、空目录安全及轮询间隔动态调整生命周期
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 */
bool FGameDirectoryChangeWatcherLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FString TestDir = GetWatcherTestDirectory(TEXT("Lifecycle"));
    CleanupWatcherTestDirectory(TestDir);
    IFileManager::Get().MakeDirectory(*TestDir, true);

    const FName WatcherName(TEXT("UnitTestWatcher_Lifecycle"));
    const FName EnabledKey(TEXT("UnitTestWatcher_Lifecycle.Enabled"));
    const FName IntervalKey(TEXT("DirectoryWatch.UnitTestWatcher_Lifecycle.IntervalSeconds"));
    const FName DirKey(TEXT("DirectoryWatch.UnitTestWatcher_Lifecycle.Directory"));

    {
        FDirectoryChangeWatcher Watcher(WatcherName, EnabledKey, TestDir);
        TestTrue(TEXT("构造后启停配置键已注册"), FConfigRegistry::Get().IsRegistered(EnabledKey));
        TestTrue(TEXT("构造后目录配置键已注册"), FConfigRegistry::Get().IsRegistered(DirKey));

        Watcher.Start();
        Watcher.Start();

        FConfigRegistry::Get().SetConfig<float>(IntervalKey, 0.25f);
        FConfigRegistry::Get().SetConfig<FString>(DirKey, TEXT(""));
        FTSTicker::GetCoreTicker().Tick(0.3f);
        FGameThreadTaskRunner::FlushInFlightTasks();
        TestEqual(TEXT("空目录配置下不触发异常广播"), Watcher.GetChangeObserver().Get(), 0u);

        Watcher.Stop();
        Watcher.Stop();
    }

    FConfigRegistry::Get().SetConfig<FString>(DirKey, TestDir);
    CleanupWatcherTestDirectory(TestDir);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDirectoryChangeWatcherFingerprintAndNonRecursiveTest,
    "Game.Data.IO.DirectoryChangeWatcher.FingerprintAndNonRecursive",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证非递归扫描 (*.json 扩展名修复)、文件删除与时间戳回滚均可被指纹算法精确检出
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 */
bool FGameDirectoryChangeWatcherFingerprintAndNonRecursiveTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FString TestDir = GetWatcherTestDirectory(TEXT("NonRecursive"));
    const FString SubDir = FPaths::Combine(TestDir, TEXT("Nested"));
    CleanupWatcherTestDirectory(TestDir);
    IFileManager::Get().MakeDirectory(*SubDir, true);

    const FString TopJson = FPaths::Combine(TestDir, TEXT("top.json"));
    const FString TopTxt = FPaths::Combine(TestDir, TEXT("ignored.txt"));
    const FString NestedJson = FPaths::Combine(SubDir, TEXT("nested.json"));

    TestTrue(TEXT("写入顶层 json 成功"), FFileHelper::SaveStringToFile(TEXT("{\"v\":1}"), *TopJson));
    TestTrue(TEXT("写入无关扩展名文件成功"), FFileHelper::SaveStringToFile(TEXT("ignore"), *TopTxt));
    TestTrue(TEXT("写入子目录 json 成功"), FFileHelper::SaveStringToFile(TEXT("{\"v\":1}"), *NestedJson));

    const FDirectoryFingerprint NonRecStar = FDirectoryChangeWatcher::ComputeDirectoryFingerprint(TestDir, TEXT("*.json"), false);
    const FDirectoryFingerprint NonRecPlain = FDirectoryChangeWatcher::ComputeDirectoryFingerprint(TestDir, TEXT("json"), false);
    const FDirectoryFingerprint NonRecDot = FDirectoryChangeWatcher::ComputeDirectoryFingerprint(TestDir, TEXT(".json"), false);
    TestEqual(TEXT("非递归 *.json 仅命中顶层 1 个文件"), NonRecStar.FileCount, 1);
    TestEqual(TEXT("*.json 与 json 扩展名归一化指纹一致"), NonRecStar.ContentHash, NonRecPlain.ContentHash);
    TestEqual(TEXT("*.json 与 .json 扩展名归一化指纹一致"), NonRecStar.ContentHash, NonRecDot.ContentHash);

    const FDirectoryFingerprint RecursiveFp = FDirectoryChangeWatcher::ComputeDirectoryFingerprint(TestDir, TEXT("*.json"), true);
    TestEqual(TEXT("递归 *.json 应命中顶层与子目录共 2 个文件"), RecursiveFp.FileCount, 2);

    const FDirectoryFingerprint AllFilesNonRec = FDirectoryChangeWatcher::ComputeDirectoryFingerprint(TestDir, TEXT(""), false);
    TestEqual(TEXT("空扩展名非递归应命中顶层全部 2 个文件"), AllFilesNonRec.FileCount, 2);

    IFileManager::Get().SetTimeStamp(*TopJson, FDateTime::UtcNow() - FTimespan::FromHours(2));
    const FDirectoryFingerprint RollbackFp = FDirectoryChangeWatcher::ComputeDirectoryFingerprint(TestDir, TEXT("*.json"), false);
    TestTrue(TEXT("时间戳回滚后目录指纹应发生变化"), RollbackFp != NonRecStar);

    IFileManager::Get().Delete(*TopJson);
    const FDirectoryFingerprint AfterDeleteFp = FDirectoryChangeWatcher::ComputeDirectoryFingerprint(TestDir, TEXT("*.json"), false);
    TestEqual(TEXT("删除文件后文件数应归零"), AfterDeleteFp.FileCount, 0);
    TestTrue(TEXT("删除文件后目录指纹应发生变化"), AfterDeleteFp != RollbackFp);

    CleanupWatcherTestDirectory(TestDir);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDirectoryChangeWatcherDetectionTest,
    "Game.Data.IO.DirectoryChangeWatcher.Detection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证实时监听器的文件新增、时间戳回滚、文件删除与非递归配置变更均触发有序广播
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
    FConfigRegistry::Get().SetConfig<bool>(FName(TEXT("DirectoryWatch.UnitTestWatcher_Detection.Recursive")), false);

    Watcher.Start();

    const FString NewFile = FPaths::Combine(TestDir, TEXT("new_file.json"));
    FFileHelper::SaveStringToFile(TEXT("updated_content"), *NewFile);
    TestTrue(TEXT("非递归模式下新增顶层文件应触发第 1 次通知"), PumpUntilWatcherSequence(Watcher, 1u));

    IFileManager::Get().SetTimeStamp(*NewFile, FDateTime::UtcNow() - FTimespan::FromHours(3));
    TestTrue(TEXT("文件时间戳回滚应触发第 2 次通知"), PumpUntilWatcherSequence(Watcher, 2u));

    IFileManager::Get().Delete(*NewFile);
    TestTrue(TEXT("文件删除应触发第 3 次通知"), PumpUntilWatcherSequence(Watcher, 3u));

    Watcher.Stop();
    CleanupWatcherTestDirectory(TestDir);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDirectoryChangeWatcherToggleTest,
    "Game.Data.IO.DirectoryChangeWatcher.EnabledToggle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

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
        FTSTicker::GetCoreTicker().Tick(0.15f);
        FGameThreadTaskRunner::FlushInFlightTasks();
    }

    TestEqual(TEXT("禁用状态下变更序号保持为 0"), Watcher.GetChangeObserver().Get(), 0u);

    Watcher.Stop();
    CleanupWatcherTestDirectory(TestDir);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
