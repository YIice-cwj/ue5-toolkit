#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
静态数据热更监听与注册入口生成器 (static_data_registration_generator.py)

- FStaticDataHotReloadWatcher.h/.cpp：编辑器磁盘热更监听器（WITH_EDITOR 限定的薄消费者）
- FStaticDataRegistration.h/.cpp：静态数据层注册入口（自动控制台命令 + 热更监听启停）
- U{前缀}StaticDataExportCommandlet.h/.cpp：.bin 导出命令行工具（Cook 前置确定性导出入口）
"""

import os
from typing import Any

from generators import GeneratedFile, GeneratorContext, doxygen_brief

WATCHER_HEADER = "FStaticDataHotReloadWatcher.h"
WATCHER_SOURCE = "FStaticDataHotReloadWatcher.cpp"
REGISTRATION_HEADER = "FStaticDataRegistration.h"
REGISTRATION_SOURCE = "FStaticDataRegistration.cpp"

REGISTRATION_HEADER_INCLUDE = "Generated/Registration/FStaticDataRegistration.h"
DIRECTORY_WATCHER_INCLUDE = "Utils/FDirectoryChangeWatcher.h"


def commandlet_class_name(cfg: Any) -> str:
    """
    导出命令行工具类名（约定名由配置统一推导）
    @param cfg 运行配置
    @return Commandlet 类名
    """
    return f"U{cfg.export_commandlet}Commandlet"


def build_watcher_header(cfg: Any) -> str:
    """
    生成 FStaticDataHotReloadWatcher.h（编辑器磁盘热更监听器声明）
    @param cfg 运行配置
    @return 头文件内容
    """
    return "\n".join([
        "/**",
        f" * @file {WATCHER_HEADER}",
        " * @brief 静态数据热重载文件监听器",
        " */",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        "",
        "/**",
        " * @brief 静态数据热更新监听器（静态工具类，编辑器下工作）",
        " *",
        " * 设计思路：轮询机制已下沉至 GameDataCore/Utils 的 FDirectoryChangeWatcher（监听目录/扩展名/递归/间隔",
        " * 全配置驱动），本类仅负责创建该监听器、订阅变更通知并向全部在线世界上下文派发",
        " * UStaticDataSubsystem::ReloadAllWorldStaticData（PIE 多世界下每个 GameInstance 各持一份数据）；",
        " * 启停开关沿用 StaticData.HotReload.Enabled，关闭期间冻结观察基线，重新开启后一次性同步。",
        " */",
        f"class {cfg.api_macro} FStaticDataHotReloadWatcher {{",
        "public:",
        "    FStaticDataHotReloadWatcher() = delete;",
        "",
        "    /**",
        "     * @brief 启动热更新监听（递增会话引用计数，仅首个活跃会话创建并启动目录监听器）",
        "     */",
        "    static void Start();",
        "",
        "    /**",
        "     * @brief 停止热更新监听（递减会话引用计数，仅当全部活跃会话退出时退订并销毁监听器）",
        "     */",
        "    static void Stop();",
        "};",
        "",
    ])


def build_watcher_source(cfg: Any) -> str:
    """
    生成 FStaticDataHotReloadWatcher.cpp（集中编译开关 if constexpr 分支控制）
    @param cfg 运行配置
    @return 实现文件内容
    """
    watcher_include = f"{cfg.static_data_include_prefix}/{WATCHER_HEADER}"
    lines = [
        "/**",
        f" * @file {WATCHER_SOURCE}",
        " * @brief 静态数据热重载文件监听器实现",
        " */",
        f'#include "{watcher_include}"',
        "",
        '#include "Config/GameCompileSwitches.h"',
        f'#include "{DIRECTORY_WATCHER_INCLUDE}"',
        f'#include "{cfg.subsystem_header_include}"',
        "",
        "namespace {",
        "",
        *doxygen_brief("活跃的 GameInstance 会话引用计数（守护多 PIE 会话生命周期）", indent=""),
        "int32 GWatcherRefCount = 0;",
        "",
        *doxygen_brief("静态数据目录监听器实例（进程级，随 Start/Stop 生命周期）", indent=""),
        "TUniquePtr<FDirectoryChangeWatcher> GDirectoryWatcher;",
        "",
        *doxygen_brief("目录变更订阅守卫（RAII，析构即退订）", indent=""),
        "TSharedPtr<FObserverSubscription> GChangeSubscription;",
        "",
        *doxygen_brief("静态数据 JSON 目录（相对项目根）", indent=""),
        f'const TCHAR* StaticDataJsonDirectory = TEXT("{cfg.json_root_dir_name}");',
        "",
        "}  // namespace",
        "",
        "/**",
        " * @brief 启动热更新监听（引用计数递增；首个会话启动真实监听器）",
        " */",
        "void FStaticDataHotReloadWatcher::Start() {",
        "    if constexpr (!Game::Config::bIsEditor) {",
        "        return;",
        "    }",
        "",
        "    ++GWatcherRefCount;",
        "    if (GWatcherRefCount == 1) {",
        "        if (!GDirectoryWatcher.IsValid()) {",
        '            static const FName StaticDataWatcherName(TEXT("StaticData"));',
        '            static const FName StaticDataHotReloadEnabledKey(TEXT("StaticData.HotReload.Enabled"));',
        "            GDirectoryWatcher = MakeUnique<FDirectoryChangeWatcher>(",
        "                StaticDataWatcherName, StaticDataHotReloadEnabledKey, StaticDataJsonDirectory);",
        "            GChangeSubscription = GDirectoryWatcher->GetChangeObserver().Subscribe(",
        "                [](const uint32&) {",
        "                    UStaticDataSubsystem::ReloadAllWorldStaticData();",
        "                });",
        "        }",
        "        GDirectoryWatcher->Start();",
        "    }",
        "}",
        "",
        "/**",
        " * @brief 停止热更新监听（引用计数递减；仅当全部会话退出时彻底销毁监听器）",
        " */",
        "void FStaticDataHotReloadWatcher::Stop() {",
        "    if constexpr (!Game::Config::bIsEditor) {",
        "        return;",
        "    }",
        "",
        "    if (GWatcherRefCount > 0) {",
        "        --GWatcherRefCount;",
        "    }",
        "    if (GWatcherRefCount == 0) {",
        "        if (GDirectoryWatcher.IsValid()) {",
        "            GDirectoryWatcher->Stop();",
        "        }",
        "        GChangeSubscription.Reset();",
        "        GDirectoryWatcher.Reset();",
        "    }",
        "}",
        "",
    ]
    return "\n".join(lines)


def build_registration_header(cfg: Any) -> str:
    """
    生成 FStaticDataRegistration.h（静态数据层统一注册类声明）
    @param cfg 运行配置
    @return 头文件内容
    """
    return "\n".join([
        "/**",
        f" * @file {REGISTRATION_HEADER}",
        " * @brief 静态数据统一注册入口",
        " */",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        "",
        "class UGameInstance;",
        "",
        "/**",
        " * @brief 静态数据层统一注册类（静态工具类）",
        " *",
        " * 设计思路：各模块各自实现 F{模块}Registration（RegisterAll / UnregisterAll",
        " * 统一签名），由生成器按约定目录自动扫描，聚合成统一注册入口。",
        " * 静态数据控制台命令通过 FAutoConsoleCommandWithWorldArgsAndOutputDevice 在模块装载期",
        " * 自动完成 RAII 注册与卸载；RegisterAll / UnregisterAll 负责编辑器热更监听会话生命周期。",
        " */",
        f"class {cfg.api_macro} FStaticDataRegistration {{",
        "public:",
        "    FStaticDataRegistration() = delete;",
        "",
        "    /**",
        "     * @brief 统一注册（在编辑器构建下启动热更监听会话）",
        "     * @param InGameInstance 当前 GameInstance",
        "     */",
        "    static void RegisterAll(UGameInstance* InGameInstance);",
        "",
        "    /**",
        "     * @brief 统一注销（在编辑器构建下递减热更监听会话引用计数）",
        "     * @param InGameInstance 当前 GameInstance",
        "     */",
        "    static void UnregisterAll(UGameInstance* InGameInstance);",
        "};",
        "",
    ])


def build_registration_source(cfg: Any) -> str:
    """
    生成 FStaticDataRegistration.cpp（静态数据层统一注册类实现）
    @param cfg 运行配置
    @return 实现文件内容
    """
    command_name = f"{cfg.project_prefix}.ReloadStaticData"
    watcher_include = f"{cfg.static_data_include_prefix}/{WATCHER_HEADER}"
    return "\n".join([
        "/**",
        f" * @file {REGISTRATION_SOURCE}",
        " * @brief 静态数据统一注册入口实现",
        " */",
        f'#include "{REGISTRATION_HEADER_INCLUDE}"',
        "",
        '#include "Config/GameCompileSwitches.h"',
        '#include "Engine/GameInstance.h"',
        '#include "Engine/World.h"',
        '#include "HAL/IConsoleManager.h"',
        f'#include "{watcher_include}"',
        f'#include "{cfg.subsystem_header_include}"',
        "",
        "namespace {",
        "",
        "/**",
        " * @brief 静态数据 .bin 导出控制台命令（模块装载期 RAII 自动注册）",
        " */",
        "FAutoConsoleCommandWithWorldArgsAndOutputDevice GExportStaticDataBinCommand(",
        f'    TEXT("{cfg.export_bin_command}"),',
        '    TEXT("将静态数据 JSON 全量导出为运行期 .bin（Cook 前置，编辑器专用）。"),',
        "    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(",
        "        []([[maybe_unused]] const TArray<FString>& CommandArguments, [[maybe_unused]] UWorld* World, FOutputDevice& OutputDevice) {",
        "            const TGameStatusResult<void> Result = UStaticDataSubsystem::ExportStaticDataBin();",
        "            if (Result.IsFailure()) {",
        f'                OutputDevice.Logf(TEXT("{cfg.export_bin_command}: 导出失败 [%d] %s"), Result.GetError().ErrorCode, *Result.GetError().Message);',
        "            }",
        "        }));",
        "",
        "/**",
        " * @brief 全世界静态数据热重载控制台命令（模块装载期 RAII 自动注册）",
        " */",
        "FAutoConsoleCommandWithWorldArgsAndOutputDevice GReloadStaticDataCommand(",
        f'    TEXT("{command_name}"),',
        '    TEXT("从磁盘重新加载全部在线世界的静态数据。失败自动回滚至旧数据，成功则数据版本号自增并广播重载事件。"),',
        "    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(",
        "        []([[maybe_unused]] const TArray<FString>& CommandArguments, [[maybe_unused]] UWorld* World, FOutputDevice& OutputDevice) {",
        f'            OutputDevice.Logf(TEXT("{command_name}: 已向全部在线世界派发静态数据重载。"));',
        "            UStaticDataSubsystem::ReloadAllWorldStaticData();",
        "        }));",
        "",
        "/**",
        " * @brief 静态数据快照完整性摘要报告控制台命令（模块装载期 RAII 自动注册）",
        " */",
        "FAutoConsoleCommandWithWorldArgsAndOutputDevice GReportStaticDataIntegrityCommand(",
        f'    TEXT("{cfg.report_integrity_command}"),',
        '    TEXT("输出当前静态数据快照的表数/行数/内容摘要（CI 双通道对拍用）。"),',
        "    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(",
        "        []([[maybe_unused]] const TArray<FString>& CommandArguments, UWorld* World, FOutputDevice& OutputDevice) {",
        "            const UStaticDataSubsystem* Subsystem = UStaticDataSubsystem::GetPtr(World);",
        "            if (Subsystem == nullptr) {",
        f'                OutputDevice.Logf(TEXT("{cfg.report_integrity_command}: 未找到静态数据子系统。"));',
        "                return;",
        "            }",
        "            const FStaticDataContentDigest Digest = Subsystem->BuildLoadedContentDigest();",
        '            OutputDevice.Logf(TEXT("表数=%d 行数=%d 内容摘要=%s"), Digest.TableCount, Digest.RowCount, *Digest.Digest);',
        "        }));",
        "",
        "}  // namespace",
        "",
        "/**",
        " * @brief 统一注册（在编辑器构建下启动热更监听会话）",
        " * @param InGameInstance 当前 GameInstance",
        " */",
        "void FStaticDataRegistration::RegisterAll(UGameInstance* InGameInstance) {",
        "    if (InGameInstance == nullptr) {",
        "        return;",
        "    }",
        "",
        "    if constexpr (Game::Config::bIsEditor) {",
        "        FStaticDataHotReloadWatcher::Start();",
        "    }",
        "}",
        "",
        "/**",
        " * @brief 统一注销（在编辑器构建下递减热更监听会话引用计数）",
        " * @param InGameInstance 当前 GameInstance",
        " */",
        "void FStaticDataRegistration::UnregisterAll(UGameInstance* InGameInstance) {",
        "    if (InGameInstance == nullptr) {",
        "        return;",
        "    }",
        "",
        "    if constexpr (Game::Config::bIsEditor) {",
        "        FStaticDataHotReloadWatcher::Stop();",
        "    }",
        "}",
        "",
    ])


def build_commandlet_header(cfg: Any) -> str:
    """
    生成 .bin 导出命令行工具头文件（Cook 前置的确定性导出入口）
    @param cfg 运行配置
    @return 头文件内容
    """
    class_name = commandlet_class_name(cfg)
    return "\n".join([
        "/**",
        f" * @file {class_name}.h",
        " * @brief 静态数据 .bin 导出命令行工具声明（由 Tools/Excel 管线自动生成）",
        " */",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        '#include "Commandlets/Commandlet.h"',
        "",
        f'#include "{class_name}.generated.h"',
        "",
        "/**",
        " * @brief 静态数据 .bin 导出命令行工具（Cook 前置）",
        " *",
        " * 设计思路：以 -run= 无头执行，把 JSON 全量导出为运行期 .bin 后按结果返回退出码",
        f" * （0 成功 / 1 失败），命令名 {cfg.export_commandlet}；",
        " * 全程不依赖世界与会话，供打包流水线在 Cook 前确定性调用。",
        " */",
        "UCLASS()",
        f"class {cfg.api_macro} {class_name} : public UCommandlet {{",
        "    GENERATED_BODY()",
        "",
        "public:",
        "    /**",
        "     * @brief 命令入口（导出成功返回 0，失败返回 1）",
        "     * @param Params 命令行参数（本工具无需额外参数）",
        "     * @return 进程退出码",
        "     */",
        "    virtual int32 Main(const FString& Params) override;",
        "};",
        "",
    ])


def build_commandlet_source(cfg: Any) -> str:
    """
    生成 .bin 导出命令行工具实现（委托子系统导出，退出码即结果）
    @param cfg 运行配置
    @return 实现文件内容
    """
    class_name = commandlet_class_name(cfg)
    return "\n".join([
        "/**",
        f" * @file {class_name}.cpp",
        " * @brief 静态数据 .bin 导出命令行工具实现（由 Tools/Excel 管线自动生成）",
        " */",
        f'#include "Generated/Registration/{class_name}.h"',
        "",
        f'#include "{cfg.subsystem_header_include}"',
        "",
        "/**",
        " * @brief 命令入口（执行静态数据 .bin 全量导出，成功返回 0，失败返回 1）",
        " * @param Params 命令行参数（本工具无需额外参数）",
        " * @return 进程退出码",
        " */",
        f"int32 {class_name}::Main([[maybe_unused]] const FString& Params) {{",
        "    return UStaticDataSubsystem::ExportStaticDataBin().IsSuccess() ? 0 : 1;",
        "}",
        "",
    ])


def generate(ctx: GeneratorContext) -> list:
    """
    生成热更监听、注册入口与 .bin 导出命令行工具全部 6 个产物（域无关；overwrite 全量重写）
    @param ctx 生成器运行上下文
    @return GeneratedFile 列表
    """
    cfg = ctx.cfg
    class_name = commandlet_class_name(cfg)
    return [
        GeneratedFile(path=os.path.join(cfg.static_data_public_dir, WATCHER_HEADER),
                      content=build_watcher_header(cfg), policy="overwrite"),
        GeneratedFile(path=os.path.join(cfg.static_data_private_dir, WATCHER_SOURCE),
                      content=build_watcher_source(cfg), policy="overwrite"),
        GeneratedFile(path=os.path.join(cfg.registration_public_dir, REGISTRATION_HEADER),
                      content=build_registration_header(cfg), policy="overwrite"),
        GeneratedFile(path=os.path.join(cfg.registration_private_dir, REGISTRATION_SOURCE),
                      content=build_registration_source(cfg), policy="overwrite"),
        GeneratedFile(path=os.path.join(cfg.registration_public_dir, f"{class_name}.h"),
                      content=build_commandlet_header(cfg), policy="overwrite"),
        GeneratedFile(path=os.path.join(cfg.registration_private_dir, f"{class_name}.cpp"),
                      content=build_commandlet_source(cfg), policy="overwrite"),
    ]
