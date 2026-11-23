#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
数据表 C++ 代码生成器包 (generators/)

每个生成器模块提供统一契约：
    def generate(ctx) -> list[GeneratedFile]

- GeneratedFile: 生成产物（绝对输出路径 + 内容 + 写入策略）
- GeneratorContext: 生成器运行上下文（由 main.py 扫描后填充）
- escape_cpp_string / doxygen_brief: 通用 C++ 字面量转义与多行 Doxygen 助手
"""

from dataclasses import dataclass
from typing import TYPE_CHECKING, Any, Dict, List, Optional, Set

if TYPE_CHECKING:
    from main import ToolConfig

SCAFFOLD_NOT_OVERWRITTEN_NOTE = "// This file will NOT be overwritten by subsequent script runs."


def escape_cpp_string(text: Any) -> str:
    """
    将任意文本安全转义为 C++ 双引号字符串字面量内容（处理反斜杠、引号与换行符）
    @param text 原始字符串
    @return 转义后的安全字符串
    """
    raw = str(text or "")
    return (
        raw.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\r\n", " ")
        .replace("\r", " ")
        .replace("\n", " ")
        .strip()
    )


def doxygen_brief(text: Any, indent: str = "    ") -> List[str]:
    """
    生成符合 B-CMT 规范的多行 Doxygen 文档块（严禁单行压缩）
    @param text 摘要文本
    @param indent 缩进前缀
    @return 多行注释字符串列表
    """
    safe = str(text or "").replace("*/", "* /").replace("\r\n", " ").replace("\r", " ").replace("\n", " ").strip()
    return [
        f"{indent}/**",
        f"{indent} * @brief {safe}",
        f"{indent} */",
    ]


def scaffold_banner(subject: str, name: str) -> List[str]:
    """
    构建「首次生成，后续不再覆盖」脚手架横幅（首行参数化，次行为统一声明）
    @param subject 脚手架主词（如 "service scaffold" / "query implementation"）
    @param name 脚手架所属对象名（分类目录名或表词根）
    @return 横幅注释两行列表
    """
    return [
        f"// Automatically generated initial {subject} for {name}.",
        SCAFFOLD_NOT_OVERWRITTEN_NOTE,
    ]


@dataclass
class GeneratedFile:
    """
    生成产物：绝对输出路径 + 内容 + 写入策略

    valid_map_names: repair 策略判定所需的当前有效 map 名集合（仅 repair 产物需要，缺省为 None）
    """
    path: str
    content: str
    policy: str
    valid_map_names: Optional[Set[str]] = None


@dataclass
class GeneratorContext:
    """
    生成器运行上下文（由 main.py 扫描后填充）

    cfg: ToolConfig（定义于 main.py）
    category_headers: 分类目录 -> 该分类 Row 头文件清单
    category_tables:  分类目录 -> 该分类表元信息列表
    """
    cfg: "ToolConfig"
    category_headers: Dict[str, List[str]]
    category_tables: Dict[str, List[Dict[str, Any]]]
