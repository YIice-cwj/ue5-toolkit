#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
数据表往返幂等自检 (roundtrip_check.py)

- 在临时目录对 Excel 副本执行「正向导出 → 反向回写 → 再正向导出」，比较两份 JSON 是否逐字节一致
- 真实 Excel 只读不写；--no-write 仅做两次正向导出比较（校验正向链路自身确定性）
- 供 CI 门使用：任一解析错误、转换错误或导出差异均以非零退出码终止，且退出前自动清理临时目录
"""

import argparse
import difflib
import filecmp
import os
import shutil
import sys
import tempfile
from typing import List, Set, Tuple

TOOLS_SCRIPT_DIR = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Script")
)
if TOOLS_SCRIPT_DIR not in sys.path:
    sys.path.insert(0, TOOLS_SCRIPT_DIR)

import excel_to_json  # noqa: E402
import json_to_excel  # noqa: E402
import tool_config  # noqa: E402
from excel_parser import iter_xlsx_files  # noqa: E402


def copy_xlsx_tree(src_dir: str, dst_dir: str) -> int:
    """
    只复制有效 .xlsx 文件（镜像子目录结构，跳过临时与隐藏文件）
    @param src_dir 源 Excel 目录
    @param dst_dir 目标副本目录
    @return 复制文件数量
    """
    copied = 0
    for src_path in iter_xlsx_files(src_dir):
        rel_dir = os.path.relpath(os.path.dirname(src_path), src_dir)
        out_dir = os.path.join(dst_dir, rel_dir) if rel_dir != "." else dst_dir
        os.makedirs(out_dir, exist_ok=True)
        shutil.copy2(src_path, os.path.join(out_dir, os.path.basename(src_path)))
        copied += 1
    return copied


def relative_file_set(root_dir: str, suffix: str) -> Set[str]:
    """
    收集目录下指定后缀文件的相对路径集合
    @param root_dir 根目录
    @param suffix 后缀名（如 ".json"）
    @return 相对路径集合
    """
    found: Set[str] = set()
    for root, _, files in os.walk(root_dir):
        for name in files:
            if name.endswith(suffix):
                found.add(os.path.relpath(os.path.join(root, name), root_dir))
    return found


def compare_json_dirs(dir_a: str, dir_b: str) -> Tuple[bool, List[str]]:
    """
    比较两个 JSON 目录（文件集合 + 逐字节内容）
    @param dir_a 首次导出目录
    @param dir_b 二次导出目录
    @return (是否一致, 差异描述列表)
    """
    files_a = relative_file_set(dir_a, ".json")
    files_b = relative_file_set(dir_b, ".json")
    diffs: List[str] = []

    only_a = sorted(files_a - files_b)
    only_b = sorted(files_b - files_a)
    if only_a:
        diffs.append(f"仅首次导出存在: {only_a}")
    if only_b:
        diffs.append(f"仅二次导出存在: {only_b}")

    for rel_path in sorted(files_a & files_b):
        path_a = os.path.join(dir_a, rel_path)
        path_b = os.path.join(dir_b, rel_path)
        if filecmp.cmp(path_a, path_b, shallow=False):
            continue
        diffs.append(f"内容不一致: {rel_path}")
        if len(diffs) <= 3:
            with open(path_a, "r", encoding="utf-8") as handle_a, open(path_b, "r", encoding="utf-8") as handle_b:
                diff_lines = list(difflib.unified_diff(
                    handle_a.read().splitlines(), handle_b.read().splitlines(),
                    fromfile=f"first/{rel_path}", tofile=f"second/{rel_path}", lineterm="",
                ))
            diffs.extend(diff_lines[:20])

    return (not diffs), diffs


def sync_excel_copy(excel_copy_dir: str, json_dir: str) -> Tuple[int, int, int, List[str]]:
    """
    对 Excel 副本逐表反向回写（不备份，不删行）
    @param excel_copy_dir Excel 副本根目录
    @param json_dir 首次导出的 JSON 根目录
    @return (处理表数, 变动表数, 转换失败数, 错误描述列表)
    """
    options = argparse.Namespace(
        dry_run=False, table=None, allow_delete=False,
        force_formula=False, fill_defaults=False,
        float_epsilon=1e-6, no_backup=True, strict=False,
        protect_formulas_config=True,
    )
    processed = 0
    changed = 0
    conversion_errors = 0
    errors: List[str] = []

    for root, _, files in os.walk(json_dir):
        for name in sorted(files):
            if not name.endswith(".json") or name.startswith("."):
                continue
            json_path = os.path.join(root, name)
            rel_dir = os.path.relpath(root, json_dir)
            table_name = os.path.splitext(name)[0]
            target_dir = os.path.join(excel_copy_dir, rel_dir) if rel_dir != "." else excel_copy_dir
            xlsx_path = os.path.join(target_dir, f"{table_name}.xlsx")
            if not os.path.isfile(xlsx_path):
                continue

            result = json_to_excel.perform_sync_for_table(xlsx_path, json_path, options, backup_session=None)
            processed += 1
            conversion_errors += len(result.conversion_errors)
            if result.error:
                errors.append(f"{table_name}: {result.error}")
            for issue in result.rule_issues:
                errors.append(f"{table_name}: 规则违规 {issue.code} [{issue.key}]")
            for drift in result.drift_issues:
                errors.append(f"{table_name}: 结构漂移 {drift}")
            if result.has_changes:
                changed += 1

    return processed, changed, conversion_errors, errors


def run_roundtrip(excel_dir: str, nested_struct_types: dict, no_write: bool = False, keep_temp: bool = False) -> int:
    """
    在隔离临时目录执行往返幂等校验，无论成功或失败均由 finally 保障临时目录零泄漏
    @param excel_dir 源 Excel 目录
    @param nested_struct_types 嵌套 USTRUCT 类型映射
    @param no_write 为 True 时仅验证正向导出确定性
    @param keep_temp 为 True 时保留临时工作目录
    @return 0 表示校验通过，1 表示失败
    """
    work_root = tempfile.mkdtemp(prefix="gamedata_roundtrip_")
    excel_copy = os.path.join(work_root, "excel_copy")
    json_first = os.path.join(work_root, "json_first")
    json_second = os.path.join(work_root, "json_second")
    os.makedirs(excel_copy, exist_ok=True)

    print("=" * 65)
    print(" 数据表往返幂等自检启动（真实 Excel 只读，全部在临时副本上进行）")
    print(f" 工作目录: {work_root}")
    print(f" 模式    : {'[仅正向确定性]' if no_write else '[正向 → 反向 → 正向]'}")
    print("=" * 65)

    try:
        copied = copy_xlsx_tree(excel_dir, excel_copy)
        print(f"[准备] 已复制 {copied} 张 Excel 副本")
        if copied == 0:
            print("[失败] 未找到任何 .xlsx，无法自检", file=sys.stderr)
            return 1

        print("[1/3] 正向导出（基准）...")
        if not excel_to_json.export_all(excel_copy, json_first, nested_struct_types):
            print("[失败] 首次正向导出存在解析或写入错误", file=sys.stderr)
            return 1

        if no_write:
            print("[2/2] 再正向导出一次（确定性校验）...")
            if not excel_to_json.export_all(excel_copy, json_second, nested_struct_types):
                print("[失败] 二次正向导出存在解析或写入错误", file=sys.stderr)
                return 1
            consistent, diffs = compare_json_dirs(json_first, json_second)
            print(f"  JSON 文件数: {len(relative_file_set(json_first, '.json'))}")
            for line in diffs[:20]:
                print(f"  {line}")
            if not consistent:
                print("[失败] 正向链路自身不确定：同一 Excel 两次导出结果不一致", file=sys.stderr)
                return 1
            print("[通过] 正向链路确定性校验通过")
            return 0

        print("[2/3] 反向回写副本...")
        processed, changed, conversion_errors, errors = sync_excel_copy(excel_copy, json_first)
        print(f"  处理 {processed} 张表，产生变动 {changed} 张，转换失败 {conversion_errors} 处")
        for err in errors:
            print(f"   [错误] {err}", file=sys.stderr)
        if errors or conversion_errors > 0:
            print("[失败] 反向回写阶段出现错误或类型转换失败，已阻断往返校验", file=sys.stderr)
            return 1

        print("[3/3] 再正向导出并比较...")
        if not excel_to_json.export_all(excel_copy, json_second, nested_struct_types):
            print("[失败] 二次正向导出存在解析或写入错误", file=sys.stderr)
            return 1
        consistent, diffs = compare_json_dirs(json_first, json_second)
        if not consistent:
            print("  差异明细（前 20 行）:", file=sys.stderr)
            for line in diffs[:20]:
                print(f"  {line}", file=sys.stderr)
            print("[失败] 往返不一致：反向回写改变了数据语义（或反向寻找/写入规则与正向不闭合）", file=sys.stderr)
            return 1

        print("[通过] 往返幂等校验通过（反向回写未改变数据语义）")
        return 0
    finally:
        if keep_temp:
            print(f"[提示] 临时目录已保留: {work_root}")
        else:
            shutil.rmtree(work_root, ignore_errors=True)
        print("=" * 65)


def main() -> int:
    """
    命令行入口：解析参数并执行数据表往返幂等自检
    @return 进程退出码（0 通过 / 1 失败）
    """
    parser = argparse.ArgumentParser(description="数据表往返幂等自检（Excel 副本上执行，真实表只读）")
    parser.add_argument("--config", default="", help="工具配置文件路径（缺省走共享配置发现）")
    parser.add_argument("--no-write", dest="no_write", action="store_true", help="仅校验正向导出两次结果一致（不执行反向回写）")
    parser.add_argument("--keep-temp", dest="keep_temp", action="store_true", help="保留临时工作目录以便排查")
    args = parser.parse_args()

    cfg = tool_config.load_config(args.config or None, (tool_config.KEY_DATA_TABLES_SOURCE_DIR, "json_output_dir"))
    return run_roundtrip(
        excel_dir=cfg[tool_config.KEY_DATA_TABLES_SOURCE_DIR],
        nested_struct_types=cfg.get("nested_struct_types") or {},
        no_write=args.no_write,
        keep_temp=args.keep_temp,
    )


if __name__ == "__main__":
    sys.exit(main())
