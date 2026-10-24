#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
统一文件写入器 (file_writer.py)

- GenerationStaging: 事务化生成暂存区——全部产物先落暂存树，全部成功后原子换入
  Public/Generated 与 Private/Generated；任一失败（含生成期异常）磁盘保持原状
- write_file(path, content, policy): 单文件按写入策略落盘（overwrite / preserve / repair）
- is_broken_or_empty_scaffold: 接口脚手架损坏判定
"""

import atexit
import os
import re
import shutil
import sys
import tempfile
import uuid
from typing import List, Optional, Set, Tuple


def remove_dir_if_empty(path: str) -> bool:
    """目录为空时删除（供 JSON 导出后自底向上清理空分类目录）
    @param path 目标目录
    @return 实际删除了空目录返回 true
    """
    try:
        if os.path.isdir(path) and not os.listdir(path):
            os.rmdir(path)
            return True
    except OSError:
        return False
    return False


def _module_root_of(cpp_path: str) -> Optional[str]:
    """从模块内源文件路径推导模块根（Public/Private 之前的部分）"""
    normalized = os.path.normpath(cpp_path)
    for marker in (f"{os.sep}Public{os.sep}", f"{os.sep}Private{os.sep}"):
        index = normalized.find(marker)
        if index != -1:
            return normalized[:index]
    return None


def _include_exists(cpp_path: str, include_path: str) -> bool:
    """按模块根与相对位置解析 Row 头 include 是否存在（避免误判脚手架损坏并覆盖用户代码）"""
    module_root = _module_root_of(cpp_path)
    if module_root:
        if os.path.exists(os.path.join(module_root, include_path)):
            return True
    target_dir = os.path.dirname(cpp_path)
    candidates = [
        os.path.join(target_dir, os.path.basename(include_path)),
        os.path.normpath(os.path.join(target_dir, "..", "..", "DataTables", os.path.basename(target_dir), os.path.basename(include_path))),
    ]
    return any(os.path.exists(candidate) for candidate in candidates)


def is_broken_or_empty_scaffold(cpp_path: str, valid_map_names: Set[str], log_marker: Optional[str] = None) -> bool:
    """判定接口脚手架是否缺失/损坏（通用语义：检测是否已被用户填充定制实现）"""
    if not os.path.exists(cpp_path):
        return True
    try:
        with open(cpp_path, "r", encoding="utf-8") as f:
            content = f.read()
        if "// Automatically generated initial interface" not in content:
            return False
        if any(marker in content for marker in ("_LOG(", "UE_LOG(", "LogGame")):
            return False
        if log_marker and log_marker in content:
            return False
        if valid_map_names:
            referenced_maps = re.findall(r"\b(\w+_map)\b", content)
            if any(m not in valid_map_names for m in referenced_maps):
                return True
        for inc_match in re.findall(r'#include\s+"([^"]+Row\.h)"', content):
            if _include_exists(cpp_path, inc_match):
                continue
            return True
        return False
    except (OSError, UnicodeDecodeError) as read_error:
        print(f"[!] 无法读取脚手架文件 {cpp_path}，按保留（不重写）处理：{read_error}", file=sys.stderr)
        return False


def write_file(path: str, content: str, policy: str, valid_map_names: Optional[Set[str]] = None) -> Tuple[str, bool]:
    """
    统一文件写入器（单文件直接落盘）
    @param path 输出文件绝对路径
    @param content 文件内容
    @param policy 写入策略: "overwrite" / "preserve" / "repair"
    @param valid_map_names repair 判定所需的当前有效 map 名集合（可选，缺省视为空集）
    @return (action, existed)
    """
    existed = os.path.isfile(path)
    if policy == "overwrite":
        _write_text(path, content)
        return "overwritten", existed
    if policy == "preserve":
        if existed:
            return "preserved", existed
        _write_text(path, content)
        return "created", existed
    if policy == "repair":
        if existed and not is_broken_or_empty_scaffold(path, valid_map_names or set()):
            return "preserved", existed
        _write_text(path, content)
        return "repaired" if existed else "created", existed
    raise ValueError(f"未知写入策略: {policy}")


def _stage_into(target_path: str, staged_path: str, content: str, policy: str,
                valid_map_names: Optional[Set[str]]) -> str:
    """
    将单文件内容写入暂存路径（preserve/repair 以目标现存文件为准）
    @param target_path 目标文件真实路径（策略判定基准）
    @param staged_path 暂存写入路径
    @param content 生成内容
    @param policy 写入策略
    @param valid_map_names repair 判定 map 名集合
    @return action ∈ overwritten / created / preserved / repaired
    """
    live_exists = os.path.isfile(target_path)
    if policy == "overwrite":
        _write_text(staged_path, content)
        return "overwritten"
    if policy == "preserve":
        if live_exists:
            _copy_file(target_path, staged_path)
            return "preserved"
        _write_text(staged_path, content)
        return "created"
    if policy == "repair":
        if live_exists and not is_broken_or_empty_scaffold(target_path, valid_map_names or set()):
            _copy_file(target_path, staged_path)
            return "preserved"
        _write_text(staged_path, content)
        return "repaired" if live_exists else "created"
    raise ValueError(f"未知写入策略: {policy}")


class GenerationStaging:
    """
    事务化生成暂存区

    用法：逐产物 stage_file(...)，全部成功后 commit() 原子换入；失败调用 abort() 丢弃。
    换入范围为模块 Public/Generated 与 Private/Generated 两棵生成树；模块外产物（统一注册头）
    以同目录临时文件 + os.replace 原子替换。任一换入步骤失败即回滚已换入目录。
    """

    def __init__(self, output_module_dir: str):
        self.module_dir = os.path.normpath(output_module_dir)
        intermediate_dir = os.path.join(self.module_dir, "Intermediate")
        os.makedirs(intermediate_dir, exist_ok=True)
        self.staging_root = tempfile.mkdtemp(prefix="GenStaging-", dir=intermediate_dir)
        self._external_files: List[Tuple[str, str]] = []
        self._staged_count = 0
        atexit.register(self._cleanup)

    @property
    def staged_count(self) -> int:
        """已暂存产物数量"""
        return self._staged_count

    def stage_file(self, path: str, content: str, policy: str,
                   valid_map_names: Optional[Set[str]] = None) -> Tuple[str, bool]:
        """
        暂存单个生成产物
        @return (action, existed)
        """
        target = os.path.normpath(path)
        relative = os.path.relpath(target, self.module_dir)
        is_external = relative.startswith("..")
        if is_external:
            staged_path = f"{target}.tmp-{uuid.uuid4().hex}"
        else:
            staged_path = os.path.join(self.staging_root, relative)
        existed = os.path.isfile(target)
        action = _stage_into(target, staged_path, content, policy, valid_map_names)
        if is_external:
            self._external_files.append((staged_path, target))
        self._staged_count += 1
        return action, existed

    def abort(self) -> None:
        """丢弃全部暂存产物（磁盘保持原状）"""
        self._cleanup()

    def _cleanup(self) -> None:
        """清理暂存树与模块外临时文件（幂等；进程异常退出由 atexit 兜底）"""
        self._remove_external_staging()
        shutil.rmtree(self.staging_root, ignore_errors=True)

    def commit(self) -> None:
        """原子换入全部暂存产物（失败自动回滚并抛出异常）"""
        generated_roots = [
            os.path.join(self.module_dir, "Public", "Generated"),
            os.path.join(self.module_dir, "Private", "Generated"),
        ]
        moved: List[Tuple[str, Optional[str]]] = []
        try:
            for root in generated_roots:
                staged_root = os.path.join(self.staging_root, os.path.relpath(root, self.module_dir))
                if not os.path.isdir(staged_root):
                    continue
                old_root: Optional[str] = None
                if os.path.isdir(root):
                    old_root = f"{root}.old-{uuid.uuid4().hex}"
                    os.rename(root, old_root)
                os.makedirs(os.path.dirname(root), exist_ok=True)
                os.rename(staged_root, root)
                moved.append((root, old_root))
            for staged_path, target in self._external_files:
                os.makedirs(os.path.dirname(target), exist_ok=True)
                os.replace(staged_path, target)
        except OSError:
            for root, old_root in reversed(moved):
                shutil.rmtree(root, ignore_errors=True)
                if old_root and os.path.isdir(old_root):
                    os.rename(old_root, root)
            raise
        finally:
            self._cleanup()
        for _root, old_root in moved:
            if old_root:
                shutil.rmtree(old_root, ignore_errors=True)

    def _remove_external_staging(self) -> None:
        """清理模块外产物的同目录临时文件（换入成功或回滚后均为空操作）"""
        for staged_path, _target in self._external_files:
            try:
                if os.path.exists(staged_path):
                    os.remove(staged_path)
            except OSError:
                pass


def _write_text(path: str, content: str) -> None:
    """以 UTF-8 / LF 落盘（必要时自动创建父目录）"""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)


def _copy_file(source: str, destination: str) -> None:
    """拷贝现存文件到暂存路径（必要时创建父目录）"""
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    shutil.copy2(source, destination)
