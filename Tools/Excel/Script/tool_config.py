#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
工具配置加载器向后兼容代理 (Tools/Excel/Script/tool_config.py)

真实实现已统一收拢至 Tools/Script/tool_config.py。
本文件动态加载真实实现并重导出全部公共符号，确保旧脚本与外部调用 100% 兼容。
"""

import importlib.util
import os
import sys

_REAL_CONFIG_PATH = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Script", "tool_config.py")
)

_spec = importlib.util.spec_from_file_location("_real_tool_config", _REAL_CONFIG_PATH)
if _spec and _spec.loader:
    _mod = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(_mod)
    for _k, _v in _mod.__dict__.items():
        if not _k.startswith("__"):
            globals()[_k] = _v