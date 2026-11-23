#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
全局静态数据子系统生成器 (subsystem_generator.py)

生成 UStaticDataSubsystem 头文件与实现文件（每次覆盖），
依据 DataTables 下实际分类目录集合自动展开加载编排与快照发布逻辑。
生成物遵循：
- 15 层类结构（公开接口优先排布，1:1 实现顺序对齐）
- 统一 GetPtr / Get 双入口（兼容 UGameInstance / UGameInstanceSubsystem / WorldContext）
- 统一 TGameStatusResult<void> 错误透传（Game::ModuleId::Data + Game::DataErrorCodes）
- TCancelableScopeGuard 门闩兜底与零业务性行内注释
"""

import os
from typing import Any, List, Sequence

from generators import GeneratedFile, GeneratorContext, doxygen_brief


def _emit_log(cfg: Any, verbosity: str, message: str, *args: str, indent: str = "    ") -> str:
    """
    根据 cfg.log_macro 与 log_category_name 生成规范的日志代码
    @param cfg 运行配置
    @param verbosity 日志级别（Log / Warning / Error 等）
    @param message TEXT(...) 格式化消息
    @param args 可变格式化参数
    @param indent 行首缩进
    @return 日志调用语句
    """
    args_str = (", " + ", ".join(args)) if args else ""
    if getattr(cfg, "log_macro", "GAME_LOG") == "UE_LOG":
        cat_name = getattr(cfg, "log_category_name", "LogGame")
        return f"{indent}UE_LOG({cat_name}, {verbosity}, {message}{args_str});"
    macro_name = getattr(cfg, "log_macro", "GAME_LOG")
    return f"{indent}{macro_name}({verbosity}, {message}{args_str});"


def _subsystem_header_preamble(cfg: Any) -> List[str]:
    """
    构建头文件前导段（include、委托声明、类文档与 UCLASS 声明）
    @param cfg 运行配置
    @return 代码行列表
    """
    header_name = os.path.basename(cfg.subsystem_header_include) if hasattr(cfg, "subsystem_header_include") else "UStaticDataSubsystem.h"
    return [
        "/**",
        f" * @file {header_name}",
        " * @brief 全局静态数据子系统",
        " */",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        '#include "Subsystems/GameInstanceSubsystem.h"',
        "",
        '#include "Async/FCancellationTokenSource.h"',
        '#include "Listener/FValueObserver.h"',
        '#include "StaticData/FStaticDataRepository.h"',
        '#include "Types/TGameResult.h"',
        f'#include "{cfg.static_data_include_prefix}/FStaticDataSnapshot.h"',
        f'#include "{cfg.static_data_include_prefix}/FStaticDataSnapshotStore.h"',
        f'#include "{cfg.static_data_include_prefix}/StaticDataTypes.h"',
        "",
        '#include "UStaticDataSubsystem.generated.h"',
        "",
        "/**",
        " * @brief 静态数据重载完成事件委托（参数：触发重载的子系统指针、递增后的数据版本号）",
        " */",
        "DECLARE_MULTICAST_DELEGATE_TwoParams(FStaticDataReloadedDelegate, UStaticDataSubsystem*, uint32)",
        "",
        "/**",
        " * @brief 全局静态数据子系统（随数据表管线自动生成）",
        " *",
        " * 继承自 UGameInstanceSubsystem，作为全局静态配置数据仓库唯一持有者：",
        " * 以「快照所有权 + 原子发布」承载各域数据，首次加载与热重载共用同一条提交流程",
        " * （门闩防重入 → 主线程反序列化暂存 → 后台预热复合缓存 → 回主线程原子发布）。",
        " * 业务查询请先取 GetSnapshot()，再交各域 F{域}StaticDataInterface / F{表}Query。",
        " */",
        "UCLASS(BlueprintType)",
        f"class {cfg.api_macro} UStaticDataSubsystem : public UGameInstanceSubsystem {{",
        "    GENERATED_BODY()",
        "",
        "public:",
    ]


def _subsystem_header_public_api(cfg: Any) -> List[str]:
    """
    构建头文件 public 业务接口声明段（生命周期/加载/查询/版本/快照/观察者/摘要）
    @param cfg 运行配置
    @return 代码行列表
    """
    return [
        "    /**",
        "     * @brief 子系统初始化生命周期回调：执行冷启动同步加载及可选启动自检",
        "     * @param Collection 子系统依赖集合",
        "     */",
        "    void Initialize(FSubsystemCollectionBase& Collection) override;",
        "",
        "    /**",
        "     * @brief 子系统反初始化生命周期回调：取消在途异步预热并释放重载门闩",
        "     */",
        "    void Deinitialize() override;",
        "",
        "    /**",
        "     * @brief 加载所有域的静态数据容器（走统一提交流程）",
        "     * @param bAsync 是否异步执行（冷启动默认为 false 确保就绪，热重载为 true）",
        "     * @return 暂存数据加载并进入提交流程返回 Success，失败返回含 Game::DataErrorCodes 的 Failure",
        "     *",
        "     * 实现思路：冷启动优先读 .bin 通道，magic/版本/CRC 任一不符或表缺失即整体回落 JSON 通道；",
        f"     * 命令行 -{cfg.static_data_channel_cmd_arg}=json 可强制 JSON 通道（Cold-Start 基准与双通道对拍用）。",
        "     */",
        "    [[nodiscard]] TGameStatusResult<void> LoadAllStaticData(bool bAsync = false);",
        "",
        "    /**",
        "     * @brief 查询静态数据是否已全部加载成功",
        "     * @return 若全部分表数据均加载完成返回 true，否则返回 false",
        "     */",
        f'    UFUNCTION(BlueprintPure, Category = "{cfg.blueprint_category}")',
        "    [[nodiscard]] bool IsAllDataLoaded() const;",
        "",
        "    /**",
        "     * @brief 重新加载全部静态数据（恒走后台异步预热，在线数据零触碰；成功后版本号自增并广播）",
        "     * @return 暂存数据加载并进入后台预热返回 Success，失败返回含 Game::DataErrorCodes 的 Failure",
        "     *",
        "     * 实现思路：热更的数据源即磁盘 JSON，故直接走 JSON 通道，不经 .bin（避免读到落后于 JSON 的旧档）。",
        "     */",
        "    [[nodiscard]] TGameStatusResult<void> ReloadAllStaticData();",
        "",
        "    /**",
        "     * @brief 获取当前数据版本号",
        "     * @return 当前版本号（自 0 起，每次成功提交自增）",
        "     */",
        "    [[nodiscard]] uint32 GetDataVersion() const noexcept;",
        "",
        "    /**",
        "     * @brief 获取当前在线快照（读者持有期间内容绝对稳定，不随后续提交改变）",
        "     * @return 快照共享指针，尚未完成首次加载返回空",
        "     */",
        "    [[nodiscard]] TSharedPtr<const FStaticDataSnapshot> GetSnapshot() const;",
        "",
        "    /**",
        "     * @brief 获取数据版本观察者（订阅静态数据更新，提交后收到新版本号）",
        "     * @return 观察者引用",
        "     */",
        "    [[nodiscard]] FValueObserver<uint32>& GetDataVersionObserver();",
        "",
        "    /**",
        "     * @brief 汇总当前在线快照的数据内容摘要（Cold-Start 基准与双通道数据对拍用）",
        "     * @return 表数、行数与顺序无关摘要；尚未完成首次加载返回零值结构",
        "     *",
        "     * 实现思路：取在线快照各域数据经「登记读写器 + 仓库」重建类型擦除镜像，",
        "     * 再由仓库按结构名/行键排序计算摘要——与装载通道无关，仅反映数据内容。",
        "     */",
        "    [[nodiscard]] FStaticDataContentDigest BuildLoadedContentDigest() const;",
        "",
    ]


def _subsystem_header_static_api(cfg: Any) -> List[str]:
    """
    构建头文件静态工具方法、编辑器导出接口与重载事件声明段
    @param cfg 运行配置
    @return 代码行列表
    """
    json_dir = getattr(cfg, "json_root_dir_name", "DataTables_Json")
    return [
        "    /**",
        "     * @brief 静态工具方法：安全获取当前静态数据子系统指针（支持 UGameInstance / UGameInstanceSubsystem / 世界上下文）",
        "     * @param InWorldContextObject 世界上下文对象",
        "     * @return 静态数据子系统指针，不存在返回 nullptr",
        "     */",
        f'    UFUNCTION(BlueprintPure, Category = "{cfg.blueprint_category}", meta = (WorldContext = "InWorldContextObject", DisplayName = "Get Static Data Subsystem"))',
        "    [[nodiscard]] static UStaticDataSubsystem* GetPtr(const UObject* InWorldContextObject);",
        "",
        "    /**",
        "     * @brief 静态工具方法：获取当前静态数据子系统强引用（调用方保证上下文有效且子系统已就绪）",
        "     * @param InWorldContextObject 世界上下文对象",
        "     * @return 静态数据子系统引用",
        "     */",
        "    [[nodiscard]] static UStaticDataSubsystem& Get(const UObject* InWorldContextObject);",
        "",
        "    /**",
        "     * @brief 静态工具方法：向全部在线世界上下文派发静态数据重载",
        "     *",
        "     * 实现思路：磁盘热更监听与控制台命令共用入口；每个 GameInstance 各持一份静态数据，",
        "     * 按引擎世界上下文集合遍历派发，保证所有世界同批换新（版本号各自自增）。",
        "     */",
        "    static void ReloadAllWorldStaticData();",
        "",
        "    /**",
        f"     * @brief 将 {json_dir} 全量导出为运行期 .bin（Cook 前置；控制台命令 {cfg.export_bin_command} 调用）",
        "     * @return 成功返回 Success，失败返回含 Game::DataErrorCodes 的 Failure",
        "     *",
        "     * 实现思路：逐域从 JSON 装载本地容器（与运行期同源）→ 登记生成式读写器 → 逐域写入仓库 → 整体导出至内容目录。",
        "     */",
        "    [[nodiscard]] static TGameStatusResult<void> ExportStaticDataBin();",
        "",
        *doxygen_brief("静态数据重载完成事件（业务层可绑定此事件作废已缓存数据）"),
        "    FStaticDataReloadedDelegate OnStaticDataReloaded;",
        "",
    ]


def _subsystem_header_private_section(cfg: Any) -> List[str]:
    """
    构建头文件 private 段（提交流程/提交回调声明与核心成员）
    @param cfg 运行配置
    @return 代码行列表
    """
    return [
        "private:",
        "    /**",
        "     * @brief 启动一次数据提交流程（支持同步冷启动与异步热更）",
        "     * @param bPreferBin 是否优先走 .bin 通道",
        "     * @param bAsync 是否异步执行（冷启动为 false 消除竞态；热更恒为 true）",
        "     * @return 成功返回 Success，失败返回含 Game::DataErrorCodes 的 Failure",
        "     */",
        "    [[nodiscard]] TGameStatusResult<void> BeginReloadFlow(bool bPreferBin, bool bAsync);",
        "",
        "    /**",
        "     * @brief 原子提交新快照（发布 + 释放门闩 + 广播，零主线程深拷贝）",
        "     * @param InNextSnapshot 已完成预热的新快照（调用链保证非空）",
        "     */",
        "    void CommitSnapshot(TSharedPtr<const FStaticDataSnapshot> InNextSnapshot);",
        "",
        *doxygen_brief("是否已成功完成全量静态数据预加载"),
        f'    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "{cfg.blueprint_category}", meta = (AllowPrivateAccess = "true"))',
        "    bool bIsAllDataLoaded = false;",
        "",
        *doxygen_brief("快照 RCU 存储（在线快照为查询唯一真源；重载门闩保证同一时刻仅一个写者）"),
        "    FStaticDataSnapshotStore SnapshotStore;",
        "",
        *doxygen_brief("数据版本号观察者（提交后 Set 广播，业务可订阅监听数据更新）"),
        '    FValueObserver<uint32> DataVersionObserver{FName(TEXT("StaticData.DataVersion"))};',
        "",
        *doxygen_brief("子系统生命周期取消源（Deinitialize 时取消在途异步预热回调）"),
        "    FCancellationTokenSource LifetimeCancelSource;",
        "};",
        "",
    ]


def generate_static_data_subsystem_header(cfg: Any, cat_folders: Sequence[str]) -> str:
    """
    生成全局静态数据子系统头文件 UStaticDataSubsystem.h（每次覆盖）
    @param cfg 运行配置
    @param cat_folders 全部分类目录（已排序）
    @return 头文件代码文本
    """
    lines = _subsystem_header_preamble(cfg)
    lines += _subsystem_header_public_api(cfg)
    lines += _subsystem_header_static_api(cfg)
    lines += _subsystem_header_private_section(cfg)
    return "\n".join(lines)


def _subsystem_cpp_preamble(cfg: Any) -> List[str]:
    """
    构建 cpp 前导段（文件头与引擎/项目 include）
    @param cfg 运行配置
    @return 代码行列表
    """
    cpp_name = os.path.basename(cfg.subsystem_header_include).replace(".h", ".cpp") if hasattr(cfg, "subsystem_header_include") else "UStaticDataSubsystem.cpp"
    lines = [
        "/**",
        f" * @file {cpp_name}",
        " * @brief 全局静态数据子系统实现",
        " */",
        f'#include "{cfg.subsystem_header_include}"',
        "",
        '#include "Async/FGameThreadTaskRunner.h"',
        '#include "Config/GameCompileSwitches.h"',
        '#include "Containers/Ticker.h"',
        '#include "Core/DataErrorCodes.h"',
        '#include "Engine/Engine.h"',
        '#include "Engine/GameInstance.h"',
        '#include "Engine/World.h"',
        '#include "HAL/FileManager.h"',
        '#include "HAL/PlatformMisc.h"',
        '#include "HAL/PlatformTime.h"',
        '#include "Misc/CommandLine.h"',
        '#include "Misc/FileHelper.h"',
        '#include "Misc/Parse.h"',
        '#include "Misc/Paths.h"',
        '#include "StaticData/FStaticDataRepository.h"',
        '#include "Types/TCancelableScopeGuard.h"',
    ]
    if getattr(cfg, "log_header", ""):
        lines.append(f'#include "{cfg.log_header}"')
    lines.extend([
        f'#include "{cfg.static_data_include_prefix}/StaticDataSerialization.h"',
        "",
    ])
    return lines


def _subsystem_cpp_interface_includes(cfg: Any, cat_folders: Sequence[str]) -> List[str]:
    """
    构建 cpp 各域 StaticDataInterface 头 include 列表
    @param cfg 运行配置
    @param cat_folders 分类目录序列
    @return include 行列表
    """
    return [f'#include "{cfg.static_data_include_prefix}/{cat}/{cat}StaticDataInterface.h"' for cat in cat_folders]


def _cpp_ns_bin_path(cfg: Any) -> List[str]:
    """
    构建 cpp 匿名命名空间中的 .bin 运行时路径获取函数
    @param cfg 运行配置
    @return 代码行列表
    """
    return [
        "/**",
        " * @brief 生成式二进制档的运行时路径（内容目录相对路径，随生成配置固化）",
        " * @return .bin 绝对路径（编辑器直读磁盘，发布包内经 UFS 从 Pak 读取）",
        " */",
        "FString GetStaticDataBinPath() {",
        f'    return FPaths::ProjectContentDir() / TEXT("{cfg.static_data_bin_content_path}");',
        "}",
        "",
    ]


def _cpp_ns_channel_switch(cfg: Any) -> List[str]:
    """
    构建 cpp 匿名命名空间中的冷启动装载通道开关解析函数
    @param cfg 运行配置
    @return 代码行列表
    """
    return [
        "/**",
        f" * @brief 解析冷启动装载通道的强制开关（命令行 -{cfg.static_data_channel_cmd_arg}=json 强制 JSON 通道）",
        " * @return 是否优先走 .bin 通道（未指定开关即默认优先）",
        " */",
        "bool ResolvePreferBinChannel() {",
        "    FString ChannelName;",
        f'    if (FParse::Value(FCommandLine::Get(), TEXT("{cfg.static_data_channel_cmd_arg}="), ChannelName)) {{',
        '        return !ChannelName.Equals(TEXT("json"), ESearchCase::IgnoreCase);',
        "    }",
        "    return true;",
        "}",
        "",
    ]


def _cpp_ns_load_json(cfg: Any, cat_folders: Sequence[str], staging_params: str) -> List[str]:
    """
    构建 cpp 匿名命名空间中的 JSON 通道整体装载辅助函数
    @param cfg 运行配置
    @param cat_folders 分类目录序列
    @param staging_params 暂存容器形参声明
    @return 代码行列表
    """
    param_docs = [f" * @param Out{cat}Data {cat} 域暂存容器" for cat in cat_folders]
    lines = [
        "/**",
        " * @brief 自 JSON 通道整体装载各域暂存数据（开发期热更与 .bin 回落通道）",
        *param_docs,
        " * @return 全部域装载成功返回 Success，否则返回首个失败结果",
        " */",
        f"TGameStatusResult<void> LoadStagingFromJson({staging_params}) {{",
        "    const double LoadStartSeconds = FPlatformTime::Seconds();",
    ]
    for cat in cat_folders:
        lines.extend([
            f"    if (const TGameStatusResult<void> Res_{cat} = Out{cat}Data.LoadAllFromJson(); Res_{cat}.IsFailure()) {{",
            f"        return Res_{cat};",
            "    }",
        ])
    lines += [
        "",
        _emit_log(cfg, "Log", 'TEXT("【静态数据子系统】本批次数据来源：JSON 通道。")'),
        _emit_log(cfg, "Log", 'TEXT("【静态数据装载耗时】JSON 通道反序列化 %.3f ms。")', "(FPlatformTime::Seconds() - LoadStartSeconds) * 1000.0"),
        "    return TGameStatusResult<void>::Success();",
        "}",
        "",
    ]
    return lines


def _cpp_ns_load_bin(cfg: Any, cat_folders: Sequence[str], staging_params: str) -> List[str]:
    """
    构建 cpp 匿名命名空间中的 .bin 通道整体装载辅助函数
    @param cfg 运行配置
    @param cat_folders 分类目录序列
    @param staging_params 暂存容器形参声明
    @return 代码行列表
    """
    param_docs = [f" * @param Out{cat}Data {cat} 域暂存容器" for cat in cat_folders]
    lines = [
        "/**",
        " * @brief 尝试自 .bin 通道整体装载各域暂存数据（任一步失败即返回精确错误，交由调用方回落 JSON）",
        " * @param InBinPath .bin 文件路径",
        *param_docs,
        " * @return 全部表提取成功返回 Success，否则返回失败结果",
        " */",
        f"TGameStatusResult<void> TryLoadStagingFromBin(const FString& InBinPath, {staging_params}) {{",
        "    const double LoadStartSeconds = FPlatformTime::Seconds();",
        "    FStaticDataRepository Repository;",
        f"    {cfg.row_serializer_register_func}(Repository);",
        "",
        "    const TGameStatusResult<int32> ImportResult = Repository.ImportFromFile(InBinPath);",
        "    if (ImportResult.IsFailure()) {",
        _emit_log(cfg, "Warning", 'TEXT("【静态数据子系统】.bin 通道装载失败（错误码 %d: %s），已回落 JSON 通道。")',
                  "ImportResult.GetError().ErrorCode, *ImportResult.GetError().Message", indent="        "),
        "        return TGameStatusResult<void>::Failure(ImportResult.GetError());",
        "    }",
        "",
    ]
    for cat in cat_folders:
        lines.extend([
            f"    if (const TGameStatusResult<void> ExtractRes_{cat} = Out{cat}Data.LoadAllFromRepository(Repository); ExtractRes_{cat}.IsFailure()) {{",
            _emit_log(cfg, "Warning", 'TEXT("【静态数据子系统】.bin 通道表提取失败（错误码 %d: %s），已回落 JSON 通道。")',
                      f"ExtractRes_{cat}.GetError().ErrorCode, *ExtractRes_{cat}.GetError().Message", indent="        "),
            f"        return ExtractRes_{cat};",
            "    }",
        ])
    lines += [
        "",
        _emit_log(cfg, "Log", 'TEXT("【静态数据子系统】本批次数据来源：.bin 通道（%s）。")', "*InBinPath"),
        _emit_log(cfg, "Log", 'TEXT("【静态数据装载耗时】.bin 通道反序列化 %.3f ms。")', "(FPlatformTime::Seconds() - LoadStartSeconds) * 1000.0"),
        "    return TGameStatusResult<void>::Success();",
        "}",
        "",
    ]
    return lines


def _cpp_ns_launch_verify(cfg: Any) -> List[str]:
    """
    构建 cpp 匿名命名空间中的打包产物启动自检辅助函数
    @param cfg 运行配置
    @return 代码行列表
    """
    return [
        "/**",
        " * @brief 打包产物启动自检（CI 冷启动双通道对拍用）",
        " * @param Subsystem 已完成冷启动装载的静态数据子系统",
        " */",
        "void VerifyOnLaunchIfRequested(const UStaticDataSubsystem& Subsystem) {",
        f'    if (!FParse::Param(FCommandLine::Get(), TEXT("{cfg.verify_launch_switch}"))) {{',
        "        return;",
        "    }",
        "",
        "    const FStaticDataContentDigest Digest = Subsystem.BuildLoadedContentDigest();",
        "    const bool bLoaded = Subsystem.IsAllDataLoaded() && Digest.TableCount > 0;",
        _emit_log(cfg, "Log", 'TEXT("表数=%d 行数=%d 内容摘要=%s")',
                  "Digest.TableCount, Digest.RowCount, *Digest.Digest"),
        "    FFileHelper::SaveStringToFile(",
        '        FString::Printf(TEXT("loaded=%d tables=%d rows=%d digest=%s"),',
        "            bLoaded ? 1 : 0, Digest.TableCount, Digest.RowCount, *Digest.Digest),",
        '        *(FPaths::ProjectSavedDir() / TEXT("StaticDataVerify.txt")));',
        "    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bLoaded](float) {",
        "        FPlatformMisc::RequestExitWithStatus(false, bLoaded ? 0 : 1);",
        "        return false;",
        "    }));",
        "}",
        "",
    ]


def _subsystem_cpp_anonymous_namespace(cfg: Any, cat_folders: Sequence[str], staging_params: str) -> List[str]:
    """
    构建 cpp 匿名命名空间整段（.bin 路径 / 通道开关 / JSON 与 .bin 装载助手 / 启动自检）
    @param cfg 运行配置
    @param cat_folders 分类目录序列
    @param staging_params 暂存容器形参声明
    @return 代码行列表
    """
    lines = ["", "namespace {", ""]
    lines += _cpp_ns_bin_path(cfg)
    lines += _cpp_ns_channel_switch(cfg)
    lines += _cpp_ns_load_json(cfg, cat_folders, staging_params)
    lines += _cpp_ns_load_bin(cfg, cat_folders, staging_params)
    lines += _cpp_ns_launch_verify(cfg)
    lines += ["}  // namespace", ""]
    return lines


def _cpp_public_section_header() -> List[str]:
    """
    构建 cpp Public 成员函数实现段分隔头
    @return 代码行列表
    """
    return [
        "/* -------------------------------------------------------------------------",
        " * Public 成员函数实现（按头文件声明顺序 1:1 对齐）",
        " * ------------------------------------------------------------------------- */",
        "",
    ]


def _cpp_framework_overrides() -> List[str]:
    """
    构建 cpp Public 生命周期重写实现（Initialize / Deinitialize）
    @return 代码行列表
    """
    return [
        "/**",
        " * @brief 子系统初始化生命周期回调：执行冷启动同步加载及可选启动自检",
        " * @param Collection 子系统依赖集合",
        " */",
        "void UStaticDataSubsystem::Initialize(FSubsystemCollectionBase& Collection) {",
        "    Super::Initialize(Collection);",
        "    LifetimeCancelSource = FCancellationTokenSource();",
        "    (void)LoadAllStaticData(false);",
        "    VerifyOnLaunchIfRequested(*this);",
        "}",
        "",
        "/**",
        " * @brief 子系统反初始化生命周期回调：取消在途异步预热并释放重载门闩",
        " */",
        "void UStaticDataSubsystem::Deinitialize() {",
        "    LifetimeCancelSource.Cancel();",
        "    SnapshotStore.EndReload();",
        "    Super::Deinitialize();",
        "}",
        "",
    ]


def _cpp_public_load_queries() -> List[str]:
    """
    构建 cpp Public 成员函数实现（加载/查询/版本/快照/观察者）
    @return 代码行列表
    """
    return [
        "/**",
        " * @brief 加载所有域的静态数据容器（走统一提交流程）",
        " * @param bAsync 是否异步执行（冷启动默认为 false 确保就绪，热重载为 true）",
        " * @return 暂存数据加载并进入提交流程返回 Success，失败返回 Failure",
        " */",
        "TGameStatusResult<void> UStaticDataSubsystem::LoadAllStaticData(bool bAsync) {",
        "    return BeginReloadFlow(ResolvePreferBinChannel(), bAsync);",
        "}",
        "",
        "/**",
        " * @brief 查询静态数据是否已全部加载成功",
        " * @return 若全部分表数据均加载完成返回 true，否则返回 false",
        " */",
        "bool UStaticDataSubsystem::IsAllDataLoaded() const {",
        "    return bIsAllDataLoaded;",
        "}",
        "",
        "/**",
        " * @brief 重新加载全部静态数据（恒走后台异步预热，在线数据零触碰；成功后版本号自增并广播）",
        " * @return 暂存数据加载并进入后台预热返回 Success，失败返回 Failure",
        " */",
        "TGameStatusResult<void> UStaticDataSubsystem::ReloadAllStaticData() {",
        "    return BeginReloadFlow(false, true);",
        "}",
        "",
        "/**",
        " * @brief 获取当前数据版本号",
        " * @return 当前版本号（自 0 起，每次成功提交自增）",
        " */",
        "uint32 UStaticDataSubsystem::GetDataVersion() const noexcept {",
        "    return SnapshotStore.GetVersion();",
        "}",
        "",
        "/**",
        " * @brief 获取当前在线快照（读者持有期间内容绝对稳定，不随后续提交改变）",
        " * @return 快照共享指针，尚未完成首次加载返回空",
        " */",
        "TSharedPtr<const FStaticDataSnapshot> UStaticDataSubsystem::GetSnapshot() const {",
        "    return SnapshotStore.Acquire();",
        "}",
        "",
        "/**",
        " * @brief 获取数据版本观察者",
        " * @return 观察者引用（订阅后提交时收到新版本号）",
        " */",
        "FValueObserver<uint32>& UStaticDataSubsystem::GetDataVersionObserver() {",
        "    return DataVersionObserver;",
        "}",
        "",
    ]


def _cpp_build_digest(cfg: Any, cat_folders: Sequence[str]) -> List[str]:
    """
    构建 cpp Public 成员函数实现：在线快照内容摘要构建
    @param cfg 运行配置
    @param cat_folders 分类目录序列
    @return 代码行列表
    """
    lines = [
        "/**",
        " * @brief 汇总当前在线快照的数据内容摘要（Cold-Start 基准与双通道数据对拍用）",
        " * @return 表数、行数与顺序无关摘要；尚未完成首次加载返回零值结构",
        " *",
        " * 实现思路：取在线快照各域数据经「登记读写器 + 仓库」重建类型擦除镜像，",
        " * 再由仓库按结构名/行键排序计算摘要——与装载通道无关，仅反映数据内容。",
        " */",
        "FStaticDataContentDigest UStaticDataSubsystem::BuildLoadedContentDigest() const {",
        "    const TSharedPtr<const FStaticDataSnapshot> Snapshot = GetSnapshot();",
        "    if (!Snapshot.IsValid()) {",
        "        return FStaticDataContentDigest();",
        "    }",
        "",
        "    FStaticDataRepository Repository;",
        f"    {cfg.row_serializer_register_func}(Repository);",
        "",
    ]
    for cat in cat_folders:
        lines.extend([
            f"    if (const TGameStatusResult<void> AddRes_{cat} = Snapshot->{cat}Data.AddAllToRepository(Repository); AddRes_{cat}.IsFailure()) {{",
            _emit_log(cfg, "Warning", 'TEXT("【静态数据校验】内容摘要构建失败：仓库写入失败（错误码 %d: %s）。")',
                      f"AddRes_{cat}.GetError().ErrorCode, *AddRes_{cat}.GetError().Message", indent="        "),
            "        return FStaticDataContentDigest();",
            "    }",
        ])
    lines += [
        "",
        "    return Repository.ComputeContentDigest();",
        "}",
        "",
    ]
    return lines


def _subsystem_cpp_public_impls(cfg: Any, cat_folders: Sequence[str]) -> List[str]:
    """
    构建 cpp Public 成员函数实现段（生命周期/首载/查询/版本/快照/摘要）
    @param cfg 运行配置
    @param cat_folders 分类目录序列
    @return 代码行列表
    """
    lines = _cpp_public_section_header()
    lines += _cpp_framework_overrides()
    lines += _cpp_public_load_queries()
    lines += _cpp_build_digest(cfg, cat_folders)
    return lines


def _subsystem_cpp_static_impls() -> List[str]:
    """
    构建 cpp Public 静态函数实现段（子系统获取与全世重载派发）
    @return 代码行列表
    """
    return [
        "/* -------------------------------------------------------------------------",
        " * Public 静态函数实现",
        " * ------------------------------------------------------------------------- */",
        "",
        "/**",
        " * @brief 静态工具方法：安全获取当前静态数据子系统指针",
        " * @param InWorldContextObject 世界上下文对象",
        " * @return 静态数据子系统指针，不存在返回 nullptr",
        " */",
        "UStaticDataSubsystem* UStaticDataSubsystem::GetPtr(const UObject* InWorldContextObject) {",
        "    if (InWorldContextObject == nullptr) {",
        "        return nullptr;",
        "    }",
        "",
        "    if (const UGameInstance* AsGameInstance = Cast<UGameInstance>(InWorldContextObject)) {",
        "        return AsGameInstance->GetSubsystem<UStaticDataSubsystem>();",
        "    }",
        "",
        "    if (const UGameInstanceSubsystem* AsSubsystem = Cast<UGameInstanceSubsystem>(InWorldContextObject)) {",
        "        const UGameInstance* OwningInstance = AsSubsystem->GetGameInstance();",
        "        return OwningInstance ? OwningInstance->GetSubsystem<UStaticDataSubsystem>() : nullptr;",
        "    }",
        "",
        "    const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(InWorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;",
        "    if (World == nullptr) {",
        "        return nullptr;",
        "    }",
        "",
        "    const UGameInstance* GameInstance = World->GetGameInstance();",
        "    return GameInstance ? GameInstance->GetSubsystem<UStaticDataSubsystem>() : nullptr;",
        "}",
        "",
        "/**",
        " * @brief 静态工具方法：获取当前静态数据子系统强引用",
        " * @param InWorldContextObject 世界上下文对象",
        " * @return 静态数据子系统引用",
        " */",
        "UStaticDataSubsystem& UStaticDataSubsystem::Get(const UObject* InWorldContextObject) {",
        "    UStaticDataSubsystem* Subsystem = GetPtr(InWorldContextObject);",
        '    checkf(Subsystem != nullptr, TEXT("UStaticDataSubsystem::Get: 无法从给定上下文获取静态数据子系统实例。"));',
        "    return *Subsystem;",
        "}",
        "",
        "/**",
        " * @brief 静态工具方法：向全部在线世界上下文派发静态数据重载",
        " */",
        "void UStaticDataSubsystem::ReloadAllWorldStaticData() {",
        "    if (GEngine == nullptr) {",
        "        return;",
        "    }",
        "",
        "    for (const FWorldContext& WorldContext : GEngine->GetWorldContexts()) {",
        "        UGameInstance* GameInstance = WorldContext.OwningGameInstance.Get();",
        "        if (GameInstance == nullptr) {",
        "            continue;",
        "        }",
        "        if (UStaticDataSubsystem* Subsystem = GameInstance->GetSubsystem<UStaticDataSubsystem>()) {",
        "            (void)Subsystem->ReloadAllStaticData();",
        "        }",
        "    }",
        "}",
        "",
    ]


def _subsystem_cpp_editor_export(cfg: Any, cat_folders: Sequence[str], export_args: str) -> List[str]:
    """
    构建 cpp .bin 全量导出实现段（编译期 constexpr 开关分支控制）
    @param cfg 运行配置
    @param cat_folders 分类目录序列
    @param export_args 导出容器实参列表
    @return 代码行列表
    """
    json_dir = getattr(cfg, "json_root_dir_name", "DataTables_Json")
    lines = [
        "/**",
        f" * @brief 将 {json_dir} 全量导出为运行期 .bin（Cook 前置；控制台命令 {cfg.export_bin_command} 调用）",
        " * @return 成功返回 Success，失败返回含 Game::DataErrorCodes 的 Failure",
        " */",
        "TGameStatusResult<void> UStaticDataSubsystem::ExportStaticDataBin() {",
        "    if constexpr (!Game::Config::bIsEditor) {",
        "        return TGameStatusResult<void>::Failure(",
        "            Game::ModuleId::Data,",
        "            Game::DataErrorCodes::ExportDisabledInNonEditor,",
        '            TEXT("静态数据 .bin 导出仅在编辑器构建中可用")',
        "        );",
        "    }",
        "",
    ]
    for cat in cat_folders:
        lines.append(f"    F{cat}StaticData Export{cat}Data;")
    lines += [
        "",
        f"    const TGameStatusResult<void> JsonLoadResult = LoadStagingFromJson({export_args});",
        "    if (JsonLoadResult.IsFailure()) {",
        _emit_log(cfg, "Error", 'TEXT("【静态数据二进制导出】失败：JSON 通道装载失败（错误码 %d: %s）。")',
                  "JsonLoadResult.GetError().ErrorCode, *JsonLoadResult.GetError().Message", indent="        "),
        "        return JsonLoadResult;",
        "    }",
        "",
        "    FStaticDataRepository Repository;",
        f"    {cfg.row_serializer_register_func}(Repository);",
        "",
    ]
    for cat in cat_folders:
        lines.extend([
            f"    if (const TGameStatusResult<void> AddRes_{cat} = Export{cat}Data.AddAllToRepository(Repository); AddRes_{cat}.IsFailure()) {{",
            _emit_log(cfg, "Error", 'TEXT("【静态数据二进制导出】失败：仓库写入失败（错误码 %d: %s）。")',
                      f"AddRes_{cat}.GetError().ErrorCode, *AddRes_{cat}.GetError().Message", indent="        "),
            f"        return AddRes_{cat};",
            "    }",
        ])
    lines += [
        "",
        "    const FString BinPath = GetStaticDataBinPath();",
        "    IFileManager::Get().MakeDirectory(*FPaths::GetPath(BinPath), true);",
        "    const TGameStatusResult<void> ExportResult = Repository.ExportToFile(BinPath);",
        "    if (ExportResult.IsFailure()) {",
        _emit_log(cfg, "Error", 'TEXT("【静态数据二进制导出】失败：写盘失败（错误码 %d: %s）: %s")',
                  "ExportResult.GetError().ErrorCode, *ExportResult.GetError().Message", "*BinPath", indent="        "),
        "        return ExportResult;",
        "    }",
        "",
        _emit_log(cfg, "Log", 'TEXT("【静态数据二进制导出】成功：已导出 %d 张表至 %s。")', "Repository.TableCount()", "*BinPath"),
        "    return TGameStatusResult<void>::Success();",
        "}",
    ]
    return lines


def _subsystem_cpp_private_impls(cfg: Any, cat_folders: Sequence[str]) -> List[str]:
    """
    构建 cpp Private 成员函数实现段（提交流程 + 原子提交）
    @param cfg 运行配置
    @param cat_folders 分类目录序列
    @return 代码行列表
    """
    snapshot_staging_args = ", ".join(f"NewSnapshot->{cat}Data" for cat in cat_folders)
    lines = [
        "",
        "/* -------------------------------------------------------------------------",
        " * Private 成员函数实现",
        " * ------------------------------------------------------------------------- */",
        "",
        "/**",
        " * @brief 启动一次数据提交流程（支持同步冷启动与异步热更）",
        " * @param bPreferBin 是否优先走 .bin 通道",
        " * @param bAsync 是否异步执行（冷启动为 false 消除竞态；热更恒为 true）",
        " * @return 成功返回 Success，失败返回含 Game::DataErrorCodes 的 Failure",
        " */",
        "TGameStatusResult<void> UStaticDataSubsystem::BeginReloadFlow(bool bPreferBin, bool bAsync) {",
        "    const TGameStatusResult<void> GateResult = SnapshotStore.TryBeginReload();",
        "    if (GateResult.IsFailure()) {",
        _emit_log(cfg, "Warning", 'TEXT("【静态数据子系统】已有重载进行中，本次请求已忽略。")', indent="        "),
        "        return GateResult;",
        "    }",
        "",
        "    TSharedRef<FStaticDataSnapshotStore::FReloadLatchTicket, ESPMode::ThreadSafe> ReloadTicket = SnapshotStore.MakeReloadLatchTicket();",
        "    auto ReloadGuard = MakeCancelableScopeGuard([ReloadTicket]() {",
        "        ReloadTicket->Release();",
        "    });",
        "",
        "    const uint32 NextVersion = SnapshotStore.GetVersion() + 1;",
        "    const FString BinPath = GetStaticDataBinPath();",
        "    TSharedPtr<FStaticDataSnapshot> NewSnapshot = MakeShared<FStaticDataSnapshot>();",
        "    NewSnapshot->DataVersion = NextVersion;",
        "",
        f"    bool bStagingLoaded = bPreferBin && TryLoadStagingFromBin(BinPath, {snapshot_staging_args}).IsSuccess();",
        "    if (!bStagingLoaded) {",
        f"        const TGameStatusResult<void> JsonResult = LoadStagingFromJson({snapshot_staging_args});",
        "        if (JsonResult.IsFailure()) {",
        _emit_log(cfg, "Warning", 'TEXT("【静态数据子系统】静态数据加载失败（错误码 %d: %s），在线数据保持原状。")',
                  "JsonResult.GetError().ErrorCode, *JsonResult.GetError().Message", indent="            "),
        "            return TGameStatusResult<void>::Failure(",
        "                Game::ModuleId::Data,",
        "                Game::DataErrorCodes::StagingLoadFailed,",
        "                JsonResult.GetError().Message",
        "            );",
        "        }",
        "    }",
        "",
        "    if (!bAsync) {",
    ]
    for cat in cat_folders:
        lines.append(f"        F{cat}StaticDataInterface::WarmUpCompositeCache(NewSnapshot->Cache, NewSnapshot->{cat}Data);")
    lines += [
        "        ReloadGuard.Dismiss();",
        "        CommitSnapshot(MoveTemp(NewSnapshot));",
        "        ReloadTicket->Release();",
        "        return TGameStatusResult<void>::Success();",
        "    }",
        "",
        "    TWeakObjectPtr<UStaticDataSubsystem> WeakThis(this);",
        "    const FCancellationToken LifetimeToken = LifetimeCancelSource.GetToken();",
        "    ReloadGuard.Dismiss();",
        "    FGameThreadTaskRunner::Run(WeakThis, LifetimeToken, [WeakThis, NewSnapshot, LifetimeToken, ReloadTicket]() {",
        "        if (LifetimeToken.IsCancellationRequested()) {",
        "            ReloadTicket->Release();",
        "            return;",
        "        }",
    ]
    for cat in cat_folders:
        lines.append(f"        F{cat}StaticDataInterface::WarmUpCompositeCache(NewSnapshot->Cache, NewSnapshot->{cat}Data);")
    lines += [
        "",
        "        FGameThreadTaskRunner::RunOnGameThread(WeakThis, [WeakThis, NewSnapshot, LifetimeToken, ReloadTicket]() {",
        "            if (LifetimeToken.IsCancellationRequested()) {",
        "                ReloadTicket->Release();",
        "                return;",
        "            }",
        "            WeakThis->CommitSnapshot(NewSnapshot);",
        "            ReloadTicket->Release();",
        "        });",
        "    });",
        "    return TGameStatusResult<void>::Success();",
        "}",
        "",
        "/**",
        " * @brief 原子提交新快照（发布 + 释放门闩 + 广播，零主线程深拷贝）",
        " * @param InNextSnapshot 已完成预热的新快照（调用链保证非空）",
        " */",
        "void UStaticDataSubsystem::CommitSnapshot(TSharedPtr<const FStaticDataSnapshot> InNextSnapshot) {",
        "    const uint32 NewVersion = InNextSnapshot->DataVersion;",
        "    bIsAllDataLoaded = true;",
        "    SnapshotStore.Publish(MoveTemp(InNextSnapshot));",
        "    SnapshotStore.EndReload();",
        _emit_log(cfg, "Log", 'TEXT("【静态数据子系统】静态数据提交成功，当前数据版本号: %u。")', "NewVersion"),
        "    DataVersionObserver.Set(NewVersion);",
        "    OnStaticDataReloaded.Broadcast(this, NewVersion);",
        "}",
        "",
    ]
    return lines


def generate_static_data_subsystem_cpp(cfg: Any, cat_folders: Sequence[str]) -> str:
    """
    生成全局静态数据子系统实现文件 UStaticDataSubsystem.cpp（每次覆盖）
    实现顺序严格与 UStaticDataSubsystem.h 声明顺序 1:1 对齐
    @param cfg 运行配置
    @param cat_folders 全部分类目录（已排序）
    @return 实现文件代码文本
    """
    staging_params = ", ".join(f"F{cat}StaticData& Out{cat}Data" for cat in cat_folders)
    export_args = ", ".join(f"Export{cat}Data" for cat in cat_folders)

    lines = _subsystem_cpp_preamble(cfg)
    lines += _subsystem_cpp_interface_includes(cfg, cat_folders)
    lines += _subsystem_cpp_anonymous_namespace(cfg, cat_folders, staging_params)
    lines += _subsystem_cpp_public_impls(cfg, cat_folders)
    lines += _subsystem_cpp_static_impls()
    lines += _subsystem_cpp_editor_export(cfg, cat_folders, export_args)
    lines += _subsystem_cpp_private_impls(cfg, cat_folders)
    return "\n".join(lines)


def generate(ctx: GeneratorContext) -> list:
    """
    生成全局静态数据子系统头文件与实现文件
    @param ctx 生成器运行上下文
    @return UStaticDataSubsystem.h（Public）与 UStaticDataSubsystem.cpp（Private），均 overwrite；无分类时返回空列表
    """
    cat_folders = sorted(ctx.category_tables.keys())
    if not cat_folders:
        return []
    return [
        GeneratedFile(
            path=os.path.join(ctx.cfg.static_data_public_dir, "UStaticDataSubsystem.h"),
            content=generate_static_data_subsystem_header(ctx.cfg, cat_folders),
            policy="overwrite",
        ),
        GeneratedFile(
            path=os.path.join(ctx.cfg.static_data_private_dir, "UStaticDataSubsystem.cpp"),
            content=generate_static_data_subsystem_cpp(ctx.cfg, cat_folders),
            policy="overwrite",
        ),
    ]
