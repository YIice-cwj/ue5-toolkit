#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
统一文件写入器 (file_writer.py)

- GenerationStaging: 事务化生成暂存区——全部产物先落暂存树（置于插件/工程 Intermediate 或同卷父目录，
  绝不污染 Source/<Module>/），全部成功后原子换入 Public/Generated 与 Private/Generated；
  任一失败磁盘保持原状
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
    """
    目录为空时删除（供 JSON 导出后自底向上清理空分类目录）
    @param path 目标目录
    @return 实际删除了空目录返回 True
    """
    try:
        if os.path.isdir(path) and not os.listdir(path):
            os.rmdir(path)
            return True
    except OSError:
        return False
    return False


def _module_root_of(cpp_path: str) -> Optional[str]:
    """
    从模块内源文件路径推导模块根（最内层 Public/Private 之前的部分）
    @param cpp_path 源文件路径
    @return 模块根路径，无法推导返回 None
    """
    normalized = os.path.normpath(cpp_path)
    best_index = -1
    for marker in (f"{os.sep}Public{os.sep}", f"{os.sep}Private{os.sep}"):
        index = normalized.rfind(marker)
        if index > best_index:
            best_index = index
    if best_index != -1:
        return normalized[:best_index]
    return None


def _include_exists(
    cpp_path: str,
    include_path: str,
    staging_module_root: Optional[str] = None,
) -> bool:
    """
    按模块根与相对位置解析 Row 头 include 是否存在（暂存生成期优先核对暂存树，避免旧磁盘残留误导）
    @param cpp_path 当前脚手架源文件路径
    @param include_path 引用的头文件路径
    @param staging_module_root 可选当前事务暂存模块根路径
    @return 存在返回 True
    """
    module_root = _module_root_of(cpp_path)
    target_dir = os.path.dirname(cpp_path)
    if staging_module_root and module_root:
        rel_dir = os.path.relpath(target_dir, module_root)
        staged_target_dir = os.path.join(staging_module_root, rel_dir)
        staged_candidates = [
            os.path.join(staging_module_root, include_path),
            os.path.join(staging_module_root, "Public", include_path),
            os.path.join(staged_target_dir, os.path.basename(include_path)),
            os.path.normpath(
                os.path.join(staged_target_dir, "..", "..", "DataTables", os.path.basename(target_dir), os.path.basename(include_path))
            ),
        ]
        return any(os.path.exists(candidate) for candidate in staged_candidates)
    if module_root:
        for base in (module_root, os.path.join(module_root, "Public")):
            if os.path.exists(os.path.join(base, include_path)):
                return True
    candidates = [
        os.path.join(target_dir, os.path.basename(include_path)),
        os.path.normpath(os.path.join(target_dir, "..", "..", "DataTables", os.path.basename(target_dir), os.path.basename(include_path))),
    ]
    return any(os.path.exists(candidate) for candidate in candidates)


def is_broken_or_empty_scaffold(
    cpp_path: str,
    valid_map_names: Set[str],
    log_marker: Optional[str] = None,
    staging_module_root: Optional[str] = None,
) -> bool:
    """
    判定接口脚手架是否缺失/损坏（检测是否已被用户填充定制实现）
    @param cpp_path 脚手架文件路径
    @param valid_map_names 当前有效的大驼峰 Map 成员名集合（如 ItemMap / GoodsMap）
    @param log_marker 可选自定义日志宏标记
    @param staging_module_root 可选当前事务暂存模块根路径
    @return 缺失或损坏返回 True，应保留返回 False
    """
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
            referenced_maps = re.findall(r"\bInData\.(\w+Map)\b", content)
            if any(m not in valid_map_names for m in referenced_maps):
                return True
        for inc_match in re.findall(r'#include\s+"([^"]+Row\.h)"', content):
            if _include_exists(cpp_path, inc_match, staging_module_root=staging_module_root):
                continue
            return True
        return False
    except (OSError, UnicodeDecodeError) as read_error:
        print(f"[!] 无法读取脚手架文件 {cpp_path}，按保留（不重写）处理：{read_error}", file=sys.stderr)
        return False


def write_file(
    path: str,
    content: str,
    policy: str,
    valid_map_names: Optional[Set[str]] = None,
    log_marker: Optional[str] = None,
) -> Tuple[str, bool]:
    """
    统一文件写入器（单文件直接落盘）
    @param path 输出文件绝对路径
    @param content 文件内容
    @param policy 写入策略: "overwrite" / "preserve" / "repair"
    @param valid_map_names repair 判定所需的当前有效 map 名集合（可选）
    @param log_marker 可选自定义日志宏标记
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
        if existed and not is_broken_or_empty_scaffold(path, valid_map_names or set(), log_marker=log_marker):
            return "preserved", existed
        _write_text(path, content)
        return "repaired" if existed else "created", existed
    raise ValueError(f"未知写入策略: {policy}")


def _stage_into(
    target_path: str,
    staged_path: str,
    content: str,
    policy: str,
    valid_map_names: Optional[Set[str]],
    log_marker: Optional[str] = None,
    staging_module_root: Optional[str] = None,
) -> str:
    """
    将单文件内容写入暂存路径（preserve/repair 以目标现存文件与当前暂存头为准）
    @param target_path 目标文件真实路径（策略判定基准）
    @param staged_path 暂存写入路径
    @param content 生成内容
    @param policy 写入策略
    @param valid_map_names repair 判定 map 名集合
    @param log_marker 可选自定义日志宏标记
    @param staging_module_root 可选当前事务暂存模块根路径
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
        if live_exists and not is_broken_or_empty_scaffold(
            target_path,
            valid_map_names or set(),
            log_marker=log_marker,
            staging_module_root=staging_module_root,
        ):
            _copy_file(target_path, staged_path)
            return "preserved"
        _write_text(staged_path, content)
        return "repaired" if live_exists else "created"
    raise ValueError(f"未知写入策略: {policy}")


def _is_inside_generated_tree(target_path: str, module_dir: str) -> bool:
    """
    判定目标文件路径是否位于模块 Public/Generated 或 Private/Generated 树内
    @param target_path 归一化目标路径
    @param module_dir 归一化模块根路径
    @return 位于 Generated 子树内返回 True，跨盘或位于 Generated 之外返回 False
    """
    target_abs = os.path.normpath(os.path.abspath(target_path))
    module_abs = os.path.normpath(os.path.abspath(module_dir))
    for sub in (os.path.join(module_abs, "Public", "Generated"), os.path.join(module_abs, "Private", "Generated")):
        try:
            if os.path.commonpath([target_abs, sub]) == sub and target_abs != sub:
                return True
        except ValueError:
            continue
    return False


def _resolve_staging_base_dir(module_dir: str) -> str:
    """
    为事务暂存树选择同卷父目录（绝不落入 Source/<Module>/ 污染 UBT 源码扫描）
    @param module_dir 输出模块根目录
    @return 暂存树父目录绝对路径
    """
    abs_module = os.path.normpath(os.path.abspath(module_dir))
    parent = os.path.dirname(abs_module)
    if os.path.basename(parent) == "Source":
        root_dir = os.path.dirname(parent)
        return os.path.join(root_dir, "Intermediate", "GameDataGenStaging")
    return parent


class GenerationStaging:
    """
    事务化生成暂存区

    用法：逐产物 stage_file(...)，全部成功后 commit() 原子换入；失败调用 abort() 丢弃。
    换入范围为模块 Public/Generated 与 Private/Generated 两棵生成树；模块外产物（统一注册头）
    以同目录临时文件 + os.replace 原子替换。任一换入步骤失败即回滚已换入目录。
    """

    def __init__(self, output_module_dir: str) -> None:
        """
        初始化事务化生成暂存区并注册进程退出清理钩子
        @param output_module_dir 输出模块根目录
        """
        self.module_dir = os.path.normpath(output_module_dir)
        staging_base = _resolve_staging_base_dir(self.module_dir)
        os.makedirs(staging_base, exist_ok=True)
        self.staging_root = tempfile.mkdtemp(prefix="GenStaging-", dir=staging_base)
        self._external_files: List[Tuple[str, str]] = []
        self._staged_count = 0
        atexit.register(self._cleanup)

    @property
    def staged_count(self) -> int:
        """
        获取当前已暂存的产物文件数量
        @return 已暂存产物数量
        """
        return self._staged_count

    def stage_file(
        self,
        path: str,
        content: str,
        policy: str,
        valid_map_names: Optional[Set[str]] = None,
        log_marker: Optional[str] = None,
    ) -> Tuple[str, bool]:
        """
        暂存单个生成产物（模块内外产物均先写入 staging_root，commit 前零实时源码树触碰）
        @param path 目标文件绝对路径
        @param content 文件文本内容
        @param policy 写入策略
        @param valid_map_names repair 策略有效 Map 名集合
        @param log_marker 可选自定义日志宏标记
        @return (action, existed)
        """
        target = os.path.normpath(path)
        is_in_generated = _is_inside_generated_tree(target, self.module_dir)
        if not is_in_generated:
            staged_path = os.path.join(self.staging_root, "_external", f"ext-{len(self._external_files)}-{uuid.uuid4().hex}.tmp")
        else:
            relative = os.path.relpath(target, self.module_dir)
            staged_path = os.path.join(self.staging_root, relative)
        existed = os.path.isfile(target)
        action = _stage_into(
            target,
            staged_path,
            content,
            policy,
            valid_map_names,
            log_marker=log_marker,
            staging_module_root=self.staging_root,
        )
        if not is_in_generated:
            self._external_files.append((staged_path, target))
        self._staged_count += 1
        return action, existed

    def abort(self) -> None:
        """
        丢弃全部暂存产物并恢复磁盘洁净状态
        """
        self._cleanup()

    def _cleanup(self) -> None:
        """
        清理暂存树与模块外临时文件（幂等；进程异常退出由 atexit 兜底）
        """
        atexit.unregister(self._cleanup)
        self._remove_external_staging()
        shutil.rmtree(self.staging_root, ignore_errors=True)

    def commit(self) -> None:
        """
        原子换入全部暂存产物（任一步失败自动回滚已换入目录与外部文件并抛出异常）
        """
        generated_roots = [
            os.path.join(self.module_dir, "Public", "Generated"),
            os.path.join(self.module_dir, "Private", "Generated"),
        ]
        moved: List[Tuple[str, Optional[str]]] = []
        replaced_externals: List[Tuple[str, Optional[bytes]]] = []
        try:
            for idx, root in enumerate(generated_roots):
                staged_root = os.path.join(self.staging_root, os.path.relpath(root, self.module_dir))
                has_staged = os.path.isdir(staged_root)
                has_live = os.path.isdir(root)
                if not has_staged and not has_live:
                    continue
                old_root: Optional[str] = None
                if has_live:
                    old_root = os.path.join(self.staging_root, f"old-{idx}-{uuid.uuid4().hex}")
                    os.rename(root, old_root)
                moved.append((root, old_root))
                if has_staged:
                    os.makedirs(os.path.dirname(root), exist_ok=True)
                    os.rename(staged_root, root)
            for staged_path, target in self._external_files:
                os.makedirs(os.path.dirname(target), exist_ok=True)
                prev_bytes: Optional[bytes] = None
                if os.path.isfile(target):
                    with open(target, "rb") as prev_handle:
                        prev_bytes = prev_handle.read()
                tmp_target = f"{target}.tmp-{uuid.uuid4().hex}"
                try:
                    shutil.copy2(staged_path, tmp_target)
                    os.replace(tmp_target, target)
                except OSError:
                    if os.path.isfile(tmp_target):
                        try:
                            os.remove(tmp_target)
                        except OSError:
                            pass
                    raise
                replaced_externals.append((target, prev_bytes))
        except OSError:
            for target, prev_bytes in reversed(replaced_externals):
                try:
                    if prev_bytes is not None:
                        restore_tmp = f"{target}.restore-{uuid.uuid4().hex}"
                        with open(restore_tmp, "wb") as restore_handle:
                            restore_handle.write(prev_bytes)
                        os.replace(restore_tmp, target)
                    elif os.path.isfile(target):
                        os.remove(target)
                except OSError:
                    pass
            for root, old_root in reversed(moved):
                shutil.rmtree(root, ignore_errors=True)
                if old_root and os.path.isdir(old_root):
                    os.rename(old_root, root)
            raise
        finally:
            self._cleanup()

    def _remove_external_staging(self) -> None:
        """
        清理模块外产物的临时文件（换入成功或回滚后均为空操作）
        """
        for staged_path, _target in self._external_files:
            try:
                if os.path.exists(staged_path):
                    os.remove(staged_path)
            except OSError:
                pass


def _write_text(path: str, content: str) -> None:
    """
    以 UTF-8 / LF 落盘（必要时自动创建父目录）
    @param path 目标文件路径
    @param content 文件内容
    """
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)


def _copy_file(source: str, destination: str) -> None:
    """
    拷贝现存文件到暂存路径（必要时创建父目录）
    @param source 源文件路径
    @param destination 目标文件路径
    """
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    shutil.copy2(source, destination)
