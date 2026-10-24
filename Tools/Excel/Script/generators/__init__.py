#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
数据表 C++ 代码生成器包 (generators/)

每个生成器模块提供统一契约：
    def generate(ctx) -> list[GeneratedFile]

- GeneratedFile: 生成产物（绝对输出路径 + 内容 + 写入策略）
- GeneratorContext: 生成器运行上下文（由 main.py 扫描后填充）
"""

from dataclasses import dataclass
from typing import TYPE_CHECKING, Any, Dict, List, Optional, Set

if TYPE_CHECKING:
    from main import ToolConfig

SCAFFOLD_NOT_OVERWRITTEN_NOTE = "// This file will NOT be overwritten by subsequent script runs."


def scaffold_banner(subject: str, name: str) -> list:
    """构建「首次生成，后续不再覆盖」脚手架横幅（首行参数化，次行为统一声明）
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
    """生成产物：绝对输出路径 + 内容 + 写入策略

    valid_map_names: repair 策略判定所需的当前有效 map 名集合
                     （仅 repair 产物需要，缺省为 None）
    """
    path: str
    content: str
    policy: str
    valid_map_names: Optional[Set[str]] = None


@dataclass
class GeneratorContext:
    """生成器运行上下文（由 main.py 扫描后填充）

    cfg: ToolConfig（定义于 main.py）
    category_headers: 分类目录 -> 该分类 Row 头文件清单
    category_tables:  分类目录 -> 该分类表元信息列表
    """
    cfg: "ToolConfig"
    category_headers: Dict[str, List[str]]
    category_tables: Dict[str, List[Dict[str, Any]]]
