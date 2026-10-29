/**
 * @file FStaticDataRegistration.cpp
 * @brief 静态数据统一注册入口实现
 */
#include "Generated/Registration/FStaticDataRegistration.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#include "Generated/Data/StaticData/FStaticDataHotReloadWatcher.h"
#include "Generated/Data/StaticData/UStaticDataSubsystem.h"
#include "Config/GameCompileSwitches.h"

namespace {

/** 控制台命令是否已完成进程级注册（防多 GameInstance 重复注册） */
bool bGlobalCommandRegistered = false;

/** 已注册的静态数据重载控制台命令句柄 */
IConsoleCommand* GReloadStaticDataCommand = nullptr;

/** 已注册的静态数据完整性报告控制台命令句柄 */
IConsoleCommand* GReportStaticDataIntegrityCommand = nullptr;

/**
 * @brief 静态数据 .bin 导出命令（静态注册）
 *
 * 实现思路：导出仅依赖磁盘 JSON、不依赖任何世界/会话，故在模块装载期静态注册，
 * 使引擎初始化完成前即可被 -ExecCmds 调度（Cook 前置导出走无头命令行），
 * 常规运行不带该命令，零行为影响
 */
FAutoConsoleCommandWithWorldArgsAndOutputDevice GExportStaticDataBinCommand(
    TEXT("Game.ExportStaticDataBin"),
    TEXT("将静态数据 JSON 全量导出为运行期 .bin（Cook 前置，编辑器专用）。"),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        []([[maybe_unused]] const TArray<FString>& CommandArguments, [[maybe_unused]] UWorld* World, FOutputDevice& OutputDevice) {
            if constexpr (Game::Config::bIsEditor) {
                if (!UStaticDataSubsystem::ExportStaticDataBin()) {
                    OutputDevice.Logf(TEXT("Game.ExportStaticDataBin: 导出失败，详见日志与仓库错误码。"));
                }
            } else {
                OutputDevice.Logf(TEXT("Game.ExportStaticDataBin: 仅在编辑器模式下可用。"));
            }
        }));

}  // namespace

/**
 * @brief 统一注册
 *
 * 实现思路：Game.ReloadStaticData 命令进程级仅注册一次（回调向全部在线世界派发重载）；
 * 热更监听按会话启动（编译期 constexpr 开关分支），
 * 其配置键（StaticData.HotReload.Enabled 与 DirectoryWatch.StaticData.*）
 * 由目录监听器构造时幂等注册
 */
void FStaticDataRegistration::RegisterAll(UGameInstance* InGameInstance) {
    if (InGameInstance == nullptr) {
        return;
    }

    if (!bGlobalCommandRegistered) {
        GReloadStaticDataCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("Game.ReloadStaticData"),
            TEXT("从磁盘重新加载全部在线世界的静态数据。失败自动回滚至旧数据，成功则数据版本号自增并广播重载事件。"),
            FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
                []([[maybe_unused]] const TArray<FString>& CommandArguments, [[maybe_unused]] UWorld* World, FOutputDevice& OutputDevice) {
                    OutputDevice.Logf(TEXT("Game.ReloadStaticData: 已向全部在线世界派发静态数据重载。"));
                    UStaticDataSubsystem::ReloadAllWorldStaticData();
                }),
            ECVF_Default);

        GReportStaticDataIntegrityCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("Game.ReportStaticDataIntegrity"),
            TEXT("输出当前静态数据快照的表数/行数/内容摘要（CI 双通道对拍用）。"),
            FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
                []([[maybe_unused]] const TArray<FString>& CommandArguments, UWorld* World, FOutputDevice& OutputDevice) {
                    const UStaticDataSubsystem* Subsystem = UStaticDataSubsystem::Get(World);
                    if (Subsystem == nullptr) {
                        OutputDevice.Logf(TEXT("Game.ReportStaticDataIntegrity: 未找到静态数据子系统。"));
                        return;
                    }
                    const FStaticDataContentDigest Digest = Subsystem->BuildLoadedContentDigest();
                    OutputDevice.Logf(TEXT("表数=%d 行数=%d 内容摘要=%s"), Digest.TableCount, Digest.RowCount, *Digest.Digest);
                }),
            ECVF_Default);
        bGlobalCommandRegistered = true;
    }

    if constexpr (Game::Config::bIsEditor) {
        FStaticDataHotReloadWatcher::Start();
    }
}

/**
 * @brief 统一注销
 *
 * 实现思路：仅停止热更监听（编译期 constexpr 开关分支）；
 * 控制台命令为进程级资源，注销会导致后续会话失效，故保留
 */
void FStaticDataRegistration::UnregisterAll([[maybe_unused]] UGameInstance* InGameInstance) {
    if constexpr (Game::Config::bIsEditor) {
        FStaticDataHotReloadWatcher::Stop();
    }
}
