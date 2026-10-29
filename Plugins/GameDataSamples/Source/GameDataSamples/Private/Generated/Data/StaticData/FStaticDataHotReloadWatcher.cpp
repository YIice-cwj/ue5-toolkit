// Automatically generated static data hot reload watcher implementation. Do not edit manually.
#include "Generated/Data/StaticData/FStaticDataHotReloadWatcher.h"

#include "IO/FDirectoryChangeWatcher.h"
#include "Generated/Data/StaticData/UStaticDataSubsystem.h"
#include "Config/GameCompileSwitches.h"
#include "Logging/GameLogCategories.h"

namespace {

/** 活跃的 GameInstance 会话引用计数（守护多 PIE 会话生命周期） */
int32 GWatcherRefCount = 0;

/** 静态数据目录监听器实例（进程级，随 Start/Stop 生命周期） */
TUniquePtr<FDirectoryChangeWatcher> GDirectoryWatcher;

/** 目录变更订阅守卫（RAII，析构即退订） */
TSharedPtr<FObserverSubscription> GChangeSubscription;

/** 静态数据目录监听实例名（派生 DirectoryWatch.StaticData.* 配置键） */
const FName StaticDataWatcherName(TEXT("StaticData"));

/** 静态数据热更启停开关配置键 */
const FName StaticDataHotReloadEnabledKey(TEXT("StaticData.HotReload.Enabled"));

/** 静态数据 JSON 目录（相对项目根） */
const TCHAR* StaticDataJsonDirectory = TEXT("DataTables_Json");

}  // namespace

/**
 * @brief 启动热更新监听（引用计数递增；首个会话启动真实监听器）
 *
 * 实现思路：编译期 constexpr 开关分支（非编辑器构建编译期剔除）；首次创建目录监听器，
 * 订阅其变更通知后向全部在线世界派发静态数据重载；多 GameInstance（多 PIE 窗口）增加计数，
 * 避免任意子会话退出导致全局监听器误停。
 */
void FStaticDataHotReloadWatcher::Start() {
    if constexpr (!Game::Config::bIsEditor) {
        return;
    }

    ++GWatcherRefCount;
    if (GWatcherRefCount == 1) {
        if (!GDirectoryWatcher.IsValid()) {
            GDirectoryWatcher = MakeUnique<FDirectoryChangeWatcher>(
                StaticDataWatcherName, StaticDataHotReloadEnabledKey, StaticDataJsonDirectory);
            GChangeSubscription = GDirectoryWatcher->GetChangeObserver().Subscribe(
                [](const uint32& ChangeId) {
                    GAME_LOG(Log, TEXT("【静态数据热更监听】检测到数据表文件变更 (变更序号: %u)，触发全局重新装载。"), ChangeId);
                    UStaticDataSubsystem::ReloadAllWorldStaticData();
                });
        }
        GDirectoryWatcher->Start();
        GAME_LOG(Log, TEXT("【静态数据热更监听】已启动静态数据热更监听 (会话引用计数: %d)。"), GWatcherRefCount);
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
        GAME_LOG(Log, TEXT("【静态数据热更监听】全部会话已退出，彻底注销静态数据热更监听器。"));
    }
}
