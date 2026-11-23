/**
 * @file FStaticDataHotReloadWatcher.cpp
 * @brief 静态数据热重载文件监听器实现
 */
#include "Generated/Data/StaticData/FStaticDataHotReloadWatcher.h"

#include "Config/GameCompileSwitches.h"
#include "Utils/FDirectoryChangeWatcher.h"
#include "Generated/Data/StaticData/UStaticDataSubsystem.h"

namespace {

/**
 * @brief 活跃的 GameInstance 会话引用计数（守护多 PIE 会话生命周期）
 */
int32 GWatcherRefCount = 0;

/**
 * @brief 静态数据目录监听器实例（进程级，随 Start/Stop 生命周期）
 */
TUniquePtr<FDirectoryChangeWatcher> GDirectoryWatcher;

/**
 * @brief 目录变更订阅守卫（RAII，析构即退订）
 */
TSharedPtr<FObserverSubscription> GChangeSubscription;

/**
 * @brief 静态数据 JSON 目录（相对项目根）
 */
const TCHAR* StaticDataJsonDirectory = TEXT("DataTables_Json");

}  // namespace

/**
 * @brief 启动热更新监听（引用计数递增；首个会话启动真实监听器）
 */
void FStaticDataHotReloadWatcher::Start() {
    if constexpr (!Game::Config::bIsEditor) {
        return;
    }

    ++GWatcherRefCount;
    if (GWatcherRefCount == 1) {
        if (!GDirectoryWatcher.IsValid()) {
            static const FName StaticDataWatcherName(TEXT("StaticData"));
            static const FName StaticDataHotReloadEnabledKey(TEXT("StaticData.HotReload.Enabled"));
            GDirectoryWatcher = MakeUnique<FDirectoryChangeWatcher>(
                StaticDataWatcherName, StaticDataHotReloadEnabledKey, StaticDataJsonDirectory);
            GChangeSubscription = GDirectoryWatcher->GetChangeObserver().Subscribe(
                [](const uint32&) {
                    UStaticDataSubsystem::ReloadAllWorldStaticData();
                });
        }
        GDirectoryWatcher->Start();
    }
}

/**
 * @brief 停止热更新监听（引用计数递减；仅当全部会话退出时彻底销毁监听器）
 */
void FStaticDataHotReloadWatcher::Stop() {
    if constexpr (!Game::Config::bIsEditor) {
        return;
    }

    if (GWatcherRefCount > 0) {
        --GWatcherRefCount;
    }
    if (GWatcherRefCount == 0) {
        if (GDirectoryWatcher.IsValid()) {
            GDirectoryWatcher->Stop();
        }
        GChangeSubscription.Reset();
        GDirectoryWatcher.Reset();
    }
}
