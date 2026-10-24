#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
静态数据热更监听与注册入口生成器 (static_data_registration_generator.py)

- FStaticDataHotReloadWatcher.h/.cpp：编辑器磁盘热更监听器（WITH_EDITOR 限定的薄消费者）
- FStaticDataRegistration.h/.cpp：静态数据层注册入口（配置键 + 控制台命令 + 热更监听启停）
- U{前缀}StaticDataExportCommandlet.h/.cpp：.bin 导出命令行工具（Cook 前置确定性导出入口）

接口导出宏取 cfg.api_macro（随配置变化）；控制台命令名前缀取 cfg.project_prefix。
"""

import os

from typing import Any

from generators import GeneratedFile, GeneratorContext

WATCHER_HEADER = "FStaticDataHotReloadWatcher.h"
WATCHER_SOURCE = "FStaticDataHotReloadWatcher.cpp"
REGISTRATION_HEADER = "FStaticDataRegistration.h"
REGISTRATION_SOURCE = "FStaticDataRegistration.cpp"

WATCHER_HEADER_INCLUDE = "Generated/Data/StaticData/FStaticDataHotReloadWatcher.h"
REGISTRATION_HEADER_INCLUDE = "Generated/Registration/FStaticDataRegistration.h"
DIRECTORY_WATCHER_INCLUDE = "IO/FDirectoryChangeWatcher.h"


def commandlet_class_name(cfg: Any) -> str:
    """导出命令行工具类名（约定名由配置统一推导）"""
    return f"U{cfg.export_commandlet}Commandlet"


def build_watcher_header(cfg: Any) -> str:
    """
    生成 FStaticDataHotReloadWatcher.h（编辑器磁盘热更监听器声明）
    @param cfg 运行配置
    @return 头文件内容
    """
    return "\n".join([
        "// Automatically generated static data hot reload watcher. Do not edit manually.",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        "",
        "/**",
        " * @brief 静态数据热更新监听器（静态工具类，编辑器下工作）",
        " *",
        " * 设计思路：轮询机制已下沉至 GameCore/IO 的 FDirectoryChangeWatcher（监听目录/扩展名/递归/间隔",
        " * 全配置驱动），本类仅负责创建该监听器、订阅变更通知并向全部在线世界上下文派发",
        " * UStaticDataSubsystem::ReloadAllWorldStaticData（PIE 多世界下每个 GameInstance 各持一份数据）；",
        " * 启停开关沿用 StaticData.HotReload.Enabled，关闭期间冻结观察基线，重新开启后一次性同步。",
        " */",
        f"class {cfg.api_macro} FStaticDataHotReloadWatcher {{",
        "public:",
        "    FStaticDataHotReloadWatcher() = delete;",
        "",
        "    /**",
        "     * @brief 启动热更新监听（创建目录监听器并订阅变更通知，重复调用先停旧实例）",
        "     */",
        "    static void Start();",
        "",
        "    /**",
        "     * @brief 停止热更新监听（退订并销毁目录监听器）",
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
    return "\n".join([
        "// Automatically generated static data hot reload watcher implementation. Do not edit manually.",
        f'#include "{WATCHER_HEADER_INCLUDE}"',
        "",
        f'#include "{DIRECTORY_WATCHER_INCLUDE}"',
        f'#include "{cfg.subsystem_header_include}"',
        '#include "Config/GameCompileSwitches.h"',
        "",
        "namespace {",
        "",
        "/** 活跃的 GameInstance 会话引用计数（守护多 PIE 会话生命周期） */",
        "int32 GWatcherRefCount = 0;",
        "",
        "/** 静态数据目录监听器实例（进程级，随 Start/Stop 生命周期） */",
        "TUniquePtr<FDirectoryChangeWatcher> GDirectoryWatcher;",
        "",
        "/** 目录变更订阅守卫（RAII，析构即退订） */",
        "TSharedPtr<FObserverSubscription> GChangeSubscription;",
        "",
        "/** 静态数据目录监听实例名（派生 DirectoryWatch.StaticData.* 配置键） */",
        'const FName StaticDataWatcherName(TEXT("StaticData"));',
        "",
        "/** 静态数据热更启停开关配置键 */",
        'const FName StaticDataHotReloadEnabledKey(TEXT("StaticData.HotReload.Enabled"));',
        "",
        "/** 静态数据 JSON 目录（相对项目根） */",
        f'const TCHAR* StaticDataJsonDirectory = TEXT("{cfg.json_root_dir_name}");',
        "",
        "}  // namespace",
        "",
        "/**",
        " * @brief 启动热更新监听（引用计数递增；首个会话启动真实监听器）",
        " *",
        " * 实现思路：编译期 constexpr 开关分支（非编辑器构建编译期剔除）；首次创建目录监听器，",
        " * 订阅其变更通知后向全部在线世界派发静态数据重载；多 GameInstance（多 PIE 窗口）增加计数，",
        " * 避免任意子会话退出导致全局监听器误停。",
        " */",
        "void FStaticDataHotReloadWatcher::Start() {",
        "    if constexpr (!Game::Config::bIsEditor) {",
        "        return;",
        "    }",
        "",
        "    ++GWatcherRefCount;",
        "    if (GWatcherRefCount == 1) {",
        "        if (!GDirectoryWatcher.IsValid()) {",
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
    ])


def build_registration_header(cfg: Any) -> str:
    """
    生成 FStaticDataRegistration.h（静态数据层统一注册类声明）
    @param cfg 运行配置
    @return 头文件内容
    """
    return "\n".join([
        "// Automatically generated static data registration entry. Do not edit manually.",
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
        " * .bin 导出命令在模块装载期静态注册；其余控制台命令为进程级资源，",
        " * 仅首次 GameInstance 注册（幂等守卫），不随会话注销。",
        " */",
        f"class {cfg.api_macro} FStaticDataRegistration {{",
        "public:",
        "    FStaticDataRegistration() = delete;",
        "",
        "    /**",
        "     * @brief 统一注册（StaticData.* 配置键 + 控制台命令 + 热更监听）",
        "     * @param InGameInstance 当前 GameInstance（用于解析静态数据子系统）",
        "     */",
        "    static void RegisterAll(UGameInstance* InGameInstance);",
        "",
        "    /**",
        "     * @brief 统一注销（停止热更监听；控制台命令为进程级资源不注销）",
        "     * @param InGameInstance 当前 GameInstance（本类未使用）",
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
    return "\n".join([
        "// Automatically generated static data registration entry implementation. Do not edit manually.",
        f'#include "{REGISTRATION_HEADER_INCLUDE}"',
        "",
        '#include "Engine/GameInstance.h"',
        '#include "Engine/World.h"',
        '#include "HAL/IConsoleManager.h"',
        "",
        f'#include "{WATCHER_HEADER_INCLUDE}"',
        f'#include "{cfg.subsystem_header_include}"',
        '#include "Config/GameCompileSwitches.h"',
        "",
        "namespace {",
        "",
        "/** 控制台命令是否已完成进程级注册（防多 GameInstance 重复注册） */",
        "bool bGlobalCommandRegistered = false;",
        "",
        "/** 已注册的静态数据重载控制台命令句柄 */",
        "IConsoleCommand* GReloadStaticDataCommand = nullptr;",
        "",
        "/** 已注册的静态数据完整性报告控制台命令句柄 */",
        "IConsoleCommand* GReportStaticDataIntegrityCommand = nullptr;",
        "",
        "/**",
        " * @brief 静态数据 .bin 导出命令（静态注册）",
        " *",
        " * 实现思路：导出仅依赖磁盘 JSON、不依赖任何世界/会话，故在模块装载期静态注册，",
        " * 使引擎初始化完成前即可被 -ExecCmds 调度（Cook 前置导出走无头命令行），",
        " * 常规运行不带该命令，零行为影响",
        " */",
        "FAutoConsoleCommandWithWorldArgsAndOutputDevice GExportStaticDataBinCommand(",
        f'    TEXT("{cfg.export_bin_command}"),',
        '    TEXT("将静态数据 JSON 全量导出为运行期 .bin（Cook 前置，编辑器专用）。"),',
        "    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(",
        "        []([[maybe_unused]] const TArray<FString>& CommandArguments, [[maybe_unused]] UWorld* World, FOutputDevice& OutputDevice) {",
        "            if constexpr (Game::Config::bIsEditor) {",
        "                if (!UStaticDataSubsystem::ExportStaticDataBin()) {",
        f'                    OutputDevice.Logf(TEXT("{cfg.export_bin_command}: 导出失败，详见日志与仓库错误码。"));',
        "                }",
        "            } else {",
        f'                OutputDevice.Logf(TEXT("{cfg.export_bin_command}: 仅在编辑器模式下可用。"));',
        "            }",
        "        }));",
        "",
        "}  // namespace",
        "",
        "/**",
        " * @brief 统一注册",
        " *",
        f" * 实现思路：{command_name} 命令进程级仅注册一次（回调向全部在线世界派发重载）；",
        " * 热更监听按会话启动（编译期 constexpr 开关分支），",
        " * 其配置键（StaticData.HotReload.Enabled 与 DirectoryWatch.StaticData.*）",
        " * 由目录监听器构造时幂等注册",
        " */",
        "void FStaticDataRegistration::RegisterAll(UGameInstance* InGameInstance) {",
        "    if (InGameInstance == nullptr) {",
        "        return;",
        "    }",
        "",
        "    if (!bGlobalCommandRegistered) {",
        "        GReloadStaticDataCommand = IConsoleManager::Get().RegisterConsoleCommand(",
        f'            TEXT("{command_name}"),',
        '            TEXT("从磁盘重新加载全部在线世界的静态数据。失败自动回滚至旧数据，成功则数据版本号自增并广播重载事件。"),',
        "            FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(",
        "                []([[maybe_unused]] const TArray<FString>& CommandArguments, [[maybe_unused]] UWorld* World, FOutputDevice& OutputDevice) {",
        f'                    OutputDevice.Logf(TEXT("{command_name}: 已向全部在线世界派发静态数据重载。"));',
        "                    UStaticDataSubsystem::ReloadAllWorldStaticData();",
        "                }),",
        "            ECVF_Default);",
        "",
        "        GReportStaticDataIntegrityCommand = IConsoleManager::Get().RegisterConsoleCommand(",
        f'            TEXT("{cfg.report_integrity_command}"),',
        '            TEXT("输出当前静态数据快照的表数/行数/内容摘要（CI 双通道对拍用）。"),',
        "            FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(",
        "                []([[maybe_unused]] const TArray<FString>& CommandArguments, UWorld* World, FOutputDevice& OutputDevice) {",
        "                    const UStaticDataSubsystem* Subsystem = UStaticDataSubsystem::Get(World);",
        "                    if (Subsystem == nullptr) {",
        f'                        OutputDevice.Logf(TEXT("{cfg.report_integrity_command}: 未找到静态数据子系统。"));',
        "                        return;",
        "                    }",
        "                    const FStaticDataContentDigest Digest = Subsystem->BuildLoadedContentDigest();",
        '                    OutputDevice.Logf(TEXT("表数=%d 行数=%d 内容摘要=%s"), Digest.TableCount, Digest.RowCount, *Digest.Digest);',
        "                }),",
        "            ECVF_Default);",
        "        bGlobalCommandRegistered = true;",
        "    }",
        "",
        "    if constexpr (Game::Config::bIsEditor) {",
        "        FStaticDataHotReloadWatcher::Start();",
        "    }",
        "}",
        "",
        "/**",
        " * @brief 统一注销",
        " *",
        " * 实现思路：仅停止热更监听（编译期 constexpr 开关分支）；",
        " * 控制台命令为进程级资源，注销会导致后续会话失效，故保留",
        " */",
        "void FStaticDataRegistration::UnregisterAll([[maybe_unused]] UGameInstance* InGameInstance) {",
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
        "// Automatically generated static data export commandlet. Do not edit manually.",
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
        " * 全程不依赖世界与会话，供打包流水线在 Cook 前确定性调用",
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
        "// Automatically generated static data export commandlet implementation. Do not edit manually.",
        f'#include "Generated/Registration/{class_name}.h"',
        "",
        f'#include "{cfg.subsystem_header_include}"',
        "",
        f"int32 {class_name}::Main([[maybe_unused]] const FString& Params) {{",
        "    return UStaticDataSubsystem::ExportStaticDataBin() ? 0 : 1;",
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
