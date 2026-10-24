#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
游戏层注册类生成器 (game_registration_generator.py)

生成 F{prefix}GameRegistration.h/.cpp：注册游戏域配置默认键（键名与键值均由配置提供），
并把业务域清单逐个提交给 Core 侧（Cache.{域}.* 策略键由框架层展开，框架层零业务域名）。

类名取 cfg.project_prefix（如 prefix=`Game` → `FGameGameRegistration`，与聚合入口
`FGameRegistration` 互不重名）；接口导出宏取 cfg.api_macro（随配置变化）；
注册项取自 cfg.game_config_defaults，域清单取自 category_tables 的键集合（随数据表域目录自动展开）；
生成器与生成物内均不含任何业务字眼。
"""

import os

from typing import Any

from generators import GeneratedFile, GeneratorContext

HEADER_SUFFIX = "GameRegistration.h"
SOURCE_SUFFIX = "GameRegistration.cpp"

FCONFIG_REGISTRY_INCLUDE = "Config/FConfigRegistry.h"
CORE_REGISTRATION_INCLUDE = "Registration/FGameCoreRegistration.h"


def domains_of(category_tables: dict) -> list:
    """
    取排序后的业务域集合（域即 DataTables 下的分类目录）
    @param category_tables 分类目录 -> 表元信息列表
    @return 排序后的域目录名列表
    """
    return sorted(category_tables.keys())


def class_name_of(cfg: Any) -> str:
    """
    由项目前缀推导游戏层注册类名
    @param cfg 运行配置
    @return 类名（如 FGameGameRegistration）
    """
    return f"F{cfg.project_prefix}{HEADER_SUFFIX[:-2]}"


def header_filename_of(cfg: Any) -> str:
    """
    由项目前缀推导头文件名（须以 F 开头、以 Registration.h 结尾，供注册聚合器扫描）
    @param cfg 运行配置
    @return 头文件名
    """
    return f"F{cfg.project_prefix}{HEADER_SUFFIX}"


def source_filename_of(cfg: Any) -> str:
    """
    由项目前缀推导实现文件名
    @param cfg 运行配置
    @return 实现文件名
    """
    return f"F{cfg.project_prefix}{SOURCE_SUFFIX}"


def header_include_of(cfg: Any) -> str:
    """
    由项目前缀推导头文件在模块内的 include 相对路径
    @param cfg 运行配置
    @return include 相对路径
    """
    return f"Generated/Registration/{header_filename_of(cfg)}"


def build_header(cfg: Any) -> str:
    """
    生成游戏层注册类头文件
    @param cfg 运行配置
    @return 头文件内容
    """
    class_name = class_name_of(cfg)
    return "\n".join([
        "// Automatically generated game layer registration entry. Do not edit manually.",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        "",
        "class UGameInstance;",
        "",
        "/**",
        " * @brief 游戏层统一注册类（静态工具类）",
        " *",
        " * 设计思路：各模块各自实现 F{模块}Registration（RegisterAll / UnregisterAll",
        " * 统一签名），由生成器按约定目录自动扫描，聚合成统一注册入口。",
        " * 注册项（键名与键值）取自配置，业务域清单取自数据表域集合，本类不含任何业务字眼。",
        " */",
        f"class {cfg.api_macro} {class_name} {{",
        "public:",
        f"    {class_name}() = delete;",
        "",
        "    /**",
        "     * @brief 统一注册（注册游戏域配置默认键 + 提交业务域缓存策略域清单）",
        "     * @param InGameInstance 当前 GameInstance（本类未使用，保持统一签名）",
        "     */",
        "    static void RegisterAll(UGameInstance* InGameInstance);",
        "",
        "    /**",
        "     * @brief 统一注销（配置无生命周期，空实现保持统一入口对称）",
        "     * @param InGameInstance 当前 GameInstance（本类未使用，保持统一签名）",
        "     */",
        "    static void UnregisterAll(UGameInstance* InGameInstance);",
        "};",
        "",
    ])


def build_source(cfg: Any, domain_list: list) -> str:
    """
    生成游戏层注册类实现文件
    @param cfg 运行配置
    @param domain_list 业务域目录名列表（已排序）
    @return 实现文件内容

    注册项逐条取自配置（保持配置书写顺序）；业务域清单取自域集合逐个提交给 Core，
    使框架层零业务域名、且新增数据表域目录时注册自动跟随展开。
    """
    class_name = class_name_of(cfg)
    defaults = dict(getattr(cfg, "game_config_defaults", {}))
    lines = [
        "// Automatically generated game layer registration entry implementation. Do not edit manually.",
        f'#include "{header_include_of(cfg)}"',
        "",
    ]
    if defaults:
        lines.append(f'#include "{FCONFIG_REGISTRY_INCLUDE}"')
    lines.extend([
        f'#include "{CORE_REGISTRATION_INCLUDE}"',
        "",
        "/**",
        " * @brief 统一注册",
        " */",
        f"void {class_name}::RegisterAll([[maybe_unused]] UGameInstance* InGameInstance) {{",
    ])
    if defaults:
        lines.append("    FConfigRegistry& Registry = FConfigRegistry::Get();")
        for key, value in defaults.items():
            lines.append(f'    Registry.RegisterConfig<FName>(FName(TEXT("{key}")), FName(TEXT("{value}")));')
    if domain_list:
        if defaults:
            lines.append("")
        lines.append("    static const FName Domains[] = {")
        for domain in domain_list:
            lines.append(f'        FName(TEXT("{domain}")),')
        lines.append("    };")
        lines.append("    for (const FName& Domain : Domains) {")
        lines.append("        FGameCoreRegistration::RegisterCacheDomain(Domain);")
        lines.append("    }")
    lines.extend([
        "}",
        "",
        "/**",
        " * @brief 统一注销（配置无生命周期，空实现保持统一入口对称）",
        " */",
        f"void {class_name}::UnregisterAll([[maybe_unused]] UGameInstance* InGameInstance) {{",
        "}",
        "",
    ])
    return "\n".join(lines)


def generate(ctx: GeneratorContext) -> list:
    """
    生成游戏层注册类 2 个产物（overwrite 全量重写）
    @param ctx 生成器运行上下文
    @return GeneratedFile 列表
    """
    cfg = ctx.cfg
    domain_list = domains_of(ctx.category_tables)
    return [
        GeneratedFile(path=os.path.join(cfg.registration_public_dir, header_filename_of(cfg)),
                      content=build_header(cfg), policy="overwrite"),
        GeneratedFile(path=os.path.join(cfg.registration_private_dir, source_filename_of(cfg)),
                      content=build_source(cfg, domain_list), policy="overwrite"),
    ]
