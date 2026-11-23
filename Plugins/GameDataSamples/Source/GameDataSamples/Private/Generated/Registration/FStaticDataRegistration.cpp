/**
 * @file FStaticDataRegistration.cpp
 * @brief 静态数据统一注册入口实现
 */
#include "Generated/Registration/FStaticDataRegistration.h"

#include "Config/GameCompileSwitches.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Generated/Data/StaticData/FStaticDataHotReloadWatcher.h"
#include "Generated/Data/StaticData/UStaticDataSubsystem.h"

namespace {

/**
 * @brief 静态数据 .bin 导出控制台命令（模块装载期 RAII 自动注册）
 */
FAutoConsoleCommandWithWorldArgsAndOutputDevice GExportStaticDataBinCommand(
    TEXT("Game.ExportStaticDataBin"),
    TEXT("将静态数据 JSON 全量导出为运行期 .bin（Cook 前置，编辑器专用）。"),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        []([[maybe_unused]] const TArray<FString>& CommandArguments, [[maybe_unused]] UWorld* World, FOutputDevice& OutputDevice) {
            const TGameStatusResult<void> Result = UStaticDataSubsystem::ExportStaticDataBin();
            if (Result.IsFailure()) {
                OutputDevice.Logf(TEXT("Game.ExportStaticDataBin: 导出失败 [%d] %s"), Result.GetError().ErrorCode, *Result.GetError().Message);
            }
        }));

/**
 * @brief 全世界静态数据热重载控制台命令（模块装载期 RAII 自动注册）
 */
FAutoConsoleCommandWithWorldArgsAndOutputDevice GReloadStaticDataCommand(
    TEXT("Game.ReloadStaticData"),
    TEXT("从磁盘重新加载全部在线世界的静态数据。失败自动回滚至旧数据，成功则数据版本号自增并广播重载事件。"),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        []([[maybe_unused]] const TArray<FString>& CommandArguments, [[maybe_unused]] UWorld* World, FOutputDevice& OutputDevice) {
            OutputDevice.Logf(TEXT("Game.ReloadStaticData: 已向全部在线世界派发静态数据重载。"));
            UStaticDataSubsystem::ReloadAllWorldStaticData();
        }));

/**
 * @brief 静态数据快照完整性摘要报告控制台命令（模块装载期 RAII 自动注册）
 */
FAutoConsoleCommandWithWorldArgsAndOutputDevice GReportStaticDataIntegrityCommand(
    TEXT("Game.ReportStaticDataIntegrity"),
    TEXT("输出当前静态数据快照的表数/行数/内容摘要（CI 双通道对拍用）。"),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        []([[maybe_unused]] const TArray<FString>& CommandArguments, UWorld* World, FOutputDevice& OutputDevice) {
            const UStaticDataSubsystem* Subsystem = UStaticDataSubsystem::GetPtr(World);
            if (Subsystem == nullptr) {
                OutputDevice.Logf(TEXT("Game.ReportStaticDataIntegrity: 未找到静态数据子系统。"));
                return;
            }
            const FStaticDataContentDigest Digest = Subsystem->BuildLoadedContentDigest();
            OutputDevice.Logf(TEXT("表数=%d 行数=%d 内容摘要=%s"), Digest.TableCount, Digest.RowCount, *Digest.Digest);
        }));

}  // namespace

/**
 * @brief 统一注册（在编辑器构建下启动热更监听会话）
 * @param InGameInstance 当前 GameInstance
 */
void FStaticDataRegistration::RegisterAll(UGameInstance* InGameInstance) {
    if (InGameInstance == nullptr) {
        return;
    }

    if constexpr (Game::Config::bIsEditor) {
        FStaticDataHotReloadWatcher::Start();
    }
}

/**
 * @brief 统一注销（在编辑器构建下递减热更监听会话引用计数）
 * @param InGameInstance 当前 GameInstance
 */
void FStaticDataRegistration::UnregisterAll(UGameInstance* InGameInstance) {
    if (InGameInstance == nullptr) {
        return;
    }

    if constexpr (Game::Config::bIsEditor) {
        FStaticDataHotReloadWatcher::Stop();
    }
}
