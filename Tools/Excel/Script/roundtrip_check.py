#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
数据表往返幂等自检 (roundtrip_check.py)

- 在临时目录对 Excel **副本**执行「正向导出 → 反向回写 → 再正向导出」，比较两份 JSON 是否逐字节一致
  （等价性检查：反向回写不应改变数据语义，第二轮回写应完全无变动）
- 真实 Excel 只读不写；--no-write 仅做两次正向导出比较（校验正向链路自身确定性）
- 供 CI 门使用：任一处不一致即非零退出

用法:
  python3 roundtrip_check.py [--config <path>] [--no-write] [--keep-temp]
"""

import argparse
import difflib
import filecmp
import os
import shutil
import sys
import tempfile
from typing import List, Set, Tuple

import excel_to_json
import json_to_excel
import tool_config


def copy_xlsx_tree(src_dir: str, dst_dir: str) -> int:
    """只复制 .xlsx（镜像子目录结构，跳过临时文件），返回复制数量"""
    copied = 0
    for root, _, files in os.walk(src_dir):
        for name in files:
            if not name.endswith(".xlsx") or name.startswith("~$"):
                continue
            rel_dir = os.path.relpath(root, src_dir)
            out_dir = os.path.join(dst_dir, rel_dir) if rel_dir != "." else dst_dir
            os.makedirs(out_dir, exist_ok=True)
            shutil.copy2(os.path.join(root, name), os.path.join(out_dir, name))
            copied += 1
    return copied


def relative_file_set(root_dir: str, suffix: str) -> Set[str]:
    """收集目录下指定后缀文件的相对路径集合"""
    found = set()
    for root, _, files in os.walk(root_dir):
        for name in files:
            if name.endswith(suffix):
                found.add(os.path.relpath(os.path.join(root, name), root_dir))
    return found


def compare_json_dirs(dir_a: str, dir_b: str) -> Tuple[bool, List[str]]:
    """
    比较两个 JSON 目录（文件集合 + 逐字节内容）
    @return (是否一致, 差异描述列表)
    """
    files_a = relative_file_set(dir_a, ".json")
    files_b = relative_file_set(dir_b, ".json")
    diffs = []

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
                    fromfile=f"first/{rel_path}", tofile=f"second/{rel_path}", lineterm=""))
            diffs.extend(diff_lines[:20])

    return (not diffs), diffs


def sync_excel_copy(excel_copy_dir: str, json_dir: str) -> Tuple[int, int, int, List[str]]:
    """
    对 Excel 副本逐表反向回写（不备份，不删行）
    @return (处理表数, 变动表数, 转换失败数, 错误描述列表)
    """
    options = argparse.Namespace(dry_run=False, table=None, allow_delete=False, force_formula=False, no_backup=True, strict=False)
    processed = 0
    changed = 0
    conversion_errors = 0
    errors = []

    for root, _, files in os.walk(json_dir):
        for name in files:
            if not name.endswith(".json") or name.startswith("."):
                continue
            json_path = os.path.join(root, name)
            rel_dir = os.path.relpath(root, json_dir)
            table_name = os.path.splitext(name)[0]
            target_dir = os.path.join(excel_copy_dir, rel_dir) if rel_dir != "." else excel_copy_dir
            xlsx_path = os.path.join(target_dir, table_name + ".xlsx")
            if not os.path.isfile(xlsx_path):
                continue

            result = json_to_excel.perform_sync_for_table(xlsx_path, json_path, options, backup_session=None)
            processed += 1
            conversion_errors += len(result.conversion_errors)
            if result.error:
                errors.append(f"{table_name}: {result.error}")
            if result.has_changes:
                changed += 1

    return processed, changed, conversion_errors, errors


def main() -> int:
    parser = argparse.ArgumentParser(description="数据表往返幂等自检（Excel 副本上执行，真实表只读）")
    parser.add_argument("--config", default="", help="工具配置文件路径（缺省走共享配置发现）")
    parser.add_argument("--no-write", dest="no_write", action="store_true", help="仅校验正向导出两次结果一致（不执行反向回写）")
    parser.add_argument("--keep-temp", dest="keep_temp", action="store_true", help="保留临时工作目录以便排查")
    args = parser.parse_args()

    cfg = tool_config.load_config(args.config, (tool_config.KEY_DATA_TABLES_SOURCE_DIR, "json_output_dir"))
    excel_dir = cfg[tool_config.KEY_DATA_TABLES_SOURCE_DIR]

    work_root = tempfile.mkdtemp(prefix="taoist_roundtrip_")
    excel_copy = os.path.join(work_root, "excel_copy")
    json_first = os.path.join(work_root, "json_first")
    json_second = os.path.join(work_root, "json_second")
    os.makedirs(excel_copy, exist_ok=True)

    print("=" * 65)
    print(" 数据表往返幂等自检启动（真实 Excel 只读，全部在临时副本上进行）")
    print(f" 工作目录: {work_root}")
    print(f" 模式    : {'[仅正向确定性]' if args.no_write else '[正向 → 反向 → 正向]'}")
    print("=" * 65)

    copied = copy_xlsx_tree(excel_dir, excel_copy)
    print(f"[准备] 已复制 {copied} 张 Excel 副本")
    if copied == 0:
        print("[失败] 未找到任何 .xlsx，无法自检", file=sys.stderr)
        return 1

    print("[1/3] 正向导出（基准）...")
    excel_to_json.export_all(str(excel_copy), str(json_first))

    if args.no_write:
        print("[2/2] 再正向导出一次（确定性校验）...")
        excel_to_json.export_all(str(excel_copy), str(json_second))
        consistent, diffs = compare_json_dirs(json_first, json_second)
        print(f"  JSON 文件数: {len(relative_file_set(json_first, '.json'))}")
        for line in diffs[:20]:
            print(f"  {line}")
        if not consistent:
            print("[失败] 正向链路自身不确定：同一 Excel 两次导出结果不一致", file=sys.stderr)
            return 1
        print("[通过] 正向链路确定性校验通过")
    else:
        print("[2/3] 反向回写副本...")
        processed, changed, conversion_errors, errors = sync_excel_copy(excel_copy, json_first)
        print(f"  处理 {processed} 张表，产生变动 {changed} 张，转换失败 {conversion_errors} 处")
        for err in errors:
            print(f"   [错误] {err}")

        print("[3/3] 再正向导出并比较...")
        excel_to_json.export_all(str(excel_copy), str(json_second))
        consistent, diffs = compare_json_dirs(json_first, json_second)
        if not consistent:
            print("  差异明细（前 20 行）:", file=sys.stderr)
            for line in diffs[:20]:
                print(f"  {line}", file=sys.stderr)
            print("[失败] 往返不一致：反向回写改变了数据语义（或反向寻找/写入规则与正向不闭合）", file=sys.stderr)
            return 1
        print("[通过] 往返幂等校验通过（反向回写未改变数据语义）")

    if args.keep_temp:
        print(f"[提示] 临时目录已保留: {work_root}")
    else:
        shutil.rmtree(work_root, ignore_errors=True)
    print("=" * 65)
    return 0


if __name__ == "__main__":
    sys.exit(main())
