#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
统一注册入口生成器 (registration_generator.py)

依据 generator_config.json 的 "registration" 段（source_roots + output_header），
自动扫描各源码根下符合约定的注册类头文件，生成统一注册头文件：

- 约定：文件名 F*Registration.h，类名 F{模块}Registration，
        含 static void RegisterAll(UGameInstance*) / UnregisterAll(UGameInstance*)
- 排序：按类名正序生成 RegisterAll，反序生成 UnregisterAll
- 输出：overwrite（全自动生成，请勿手动编辑）

无需逐条登记注册项：新增模块只需放置注册类文件，重跑生成器即自动并入。
"""

import os
import re

from typing import Optional

from generators import GeneratedFile

SECTION_KEY = "registration"
HEADER_RE = re.compile(r"class\s+\w+_API\s+(F\w+Registration)\b")
SKIP_DIRS = {"Intermediate", "Binaries", "DerivedDataCache", "Saved"}
PUBLIC_MARKER = "/Public/"


def generate(config_raw: dict) -> list:
    """
    生成统一注册入口头文件
    @param config_raw 完整配置字典（须含 "registration" 段）
    @return 生成产物列表（配置缺失或未发现注册类时返回空列表）
    """
    section = config_raw.get(SECTION_KEY)
    if not section:
        return []
    output_header = section.get("output_header")
    source_roots = section.get("source_roots", [])
    if not output_header or not source_roots:
        return []
    registrars = _discover(source_roots)
    if not registrars:
        return []
    return [GeneratedFile(path=output_header, content=_build_content(config_raw, registrars, output_header), policy="overwrite")]


def _discover(source_roots: list) -> list:
    """
    扫描源码根，发现符合约定的注册类
    @param source_roots 扫描根路径列表
    @return [(类名, include 相对路径), ...] 按类名排序
    """
    found = {}
    for root in source_roots:
        if not os.path.isdir(root):
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
            for filename in filenames:
                if not (filename.startswith("F") and filename.endswith("Registration.h")):
                    continue
                full_path = os.path.join(dirpath, filename)
                try:
                    with open(full_path, "r", encoding="utf-8") as f:
                        content = f.read()
                except OSError:
                    continue
                match = HEADER_RE.search(content)
                if not match:
                    continue
                if not _has_uniform_signature(content):
                    continue
                include_path = _include_path(full_path)
                if include_path:
                    found[match.group(1)] = include_path
    return sorted(found.items())


def _has_uniform_signature(content: str) -> bool:
    """
    校验注册类是否实现统一签名（容忍 [[maybe_unused]] 等属性）
    @param content 头文件内容
    @return 同时含 RegisterAll(UGameInstance*) 与 UnregisterAll(UGameInstance*) 返回 True
    """
    register_ok = re.search(r"RegisterAll\([^)]*UGameInstance\*", content) is not None
    unregister_ok = re.search(r"UnregisterAll\([^)]*UGameInstance\*", content) is not None
    return register_ok and unregister_ok


def _include_path(full_path: str) -> Optional[str]:
    """
    由绝对路径推导模块内 include 相对路径（取 Public/ 之后）
    @param full_path 头文件绝对路径
    @return 相对 include 路径；不含 Public/ 标记时返回 None
    """
    idx = full_path.find(PUBLIC_MARKER)
    if idx == -1:
        return None
    rel = full_path[idx + len(PUBLIC_MARKER):]
    return rel


def _build_content(config_raw: dict, registrars: list, output_header: str) -> str:
    """按注册类清单拼装统一注册头文件内容"""
    header_filename = os.path.basename(output_header)
    prefix = config_raw.get("project_prefix", "Game")
    class_name = config_raw.get("registration_class_name") or f"F{prefix}Registration"
    gi_class = config_raw.get("game_instance_class_name") or f"U{prefix}GameInstance"

    lines = [
        "/**",
        f" * @file {header_filename}",
        " * @brief 统一注册入口（由 Tools/Excel/Script 管线自动生成，请勿手动编辑）",
        " *",
        " * 自动扫描各模块 Public/ 下 F{模块}Registration 注册类并聚合；",
        " * RegisterAll 按类名正序执行，UnregisterAll 按反序注销。",
        " */",
        "",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        "",
        "class UGameInstance;",
        "",
    ]
    for _, include_path in registrars:
        lines.append(f'#include "{include_path}"')
    lines.append("")
    lines.append("/**")
    lines.append(" * @brief 统一注册入口（静态工具类）")
    lines.append(" *")
    lines.append(f" * 由 {gi_class}::Init / Shutdown 各调用一次。")
    lines.append(" */")
    lines.append(f"class {class_name} {{")
    lines.append("public:")
    lines.append(f"    {class_name}() = delete;")
    lines.append("")
    lines.append("    /**")
    lines.append("     * @brief 统一注册（配置键 / 控制台命令 / 运行时装配）")
    lines.append("     * @param InGameInstance 当前 GameInstance")
    lines.append("     */")
    lines.append("    static void RegisterAll(UGameInstance* InGameInstance) {")
    for reg_class, _ in registrars:
        lines.append(f"        {reg_class}::RegisterAll(InGameInstance);")
    lines.append("    }")
    lines.append("")
    lines.append("    /**")
    lines.append("     * @brief 统一注销（与注册反序）")
    lines.append("     * @param InGameInstance 当前 GameInstance")
    lines.append("     */")
    lines.append("    static void UnregisterAll(UGameInstance* InGameInstance) {")
    for reg_class, _ in reversed(registrars):
        lines.append(f"        {reg_class}::UnregisterAll(InGameInstance);")
    lines.append("    }")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)
