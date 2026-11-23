#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
JSON 反写 Excel 数据表同步工具 (json_to_excel.py) —— 双向数据同步

核心特性：
1. 单一真源（table_schema.py 契约）：三行表头契约与类型编解码完全对齐；
2. 跨工作表安全：基于 table_schema.resolve_openpyxl_sheet 精确定位目标 Sheet；
3. 写前快照与原子落盘：秒内防冲突快照目录、sha1 完整性清单、原子替换；
4. 内存前置数据校验：落盘前检查主键非空、类型可转性与表头结构漂移；
5. 空值语义保护与公式盾牌：保留 Excel 留空与公式单元格，按需参数覆盖；
6. 完整回滚支持：--rollback [--snapshot <目录>] 经 sha1 校验并原子还原。
"""

import argparse
from copy import copy
import dataclasses
from datetime import datetime
import hashlib
import json
import os
import re
import shutil
import sys
import zipfile
from typing import Any, Dict, List, Optional, Set, Tuple

import openpyxl
from openpyxl.utils.exceptions import InvalidFileException

TOOLS_SCRIPT_DIR = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Script")
)
if TOOLS_SCRIPT_DIR not in sys.path:
    sys.path.insert(0, TOOLS_SCRIPT_DIR)

import table_rules  # noqa: E402
import table_schema  # noqa: E402
import tool_config  # noqa: E402
from naming_utils import strip_dt_prefix, to_property_name  # noqa: E402

SNAPSHOT_DIR_PATTERN = re.compile(r"^\d{8}_\d{6}(?:_\d+)?$")
MANIFEST_FILENAME = "_manifest.json"
TMP_SUFFIX = ".tmp"


@dataclasses.dataclass
class SyncResult:
    """
    单表同步结果（含差异、告警、转换失败、规则问题与错误）
    """
    has_changes: bool = False
    diff_list: List[Dict[str, Any]] = dataclasses.field(default_factory=list)
    warnings: List[str] = dataclasses.field(default_factory=list)
    conversion_errors: List[str] = dataclasses.field(default_factory=list)
    drift_issues: List[str] = dataclasses.field(default_factory=list)
    rule_issues: List[table_rules.RuleIssue] = dataclasses.field(default_factory=list)
    text_literal_count: int = 0
    backup_path: Optional[str] = None
    error: Optional[str] = None


def file_sha1(path: str) -> str:
    """
    计算文件 sha1（备份清单与回滚完整性校验用）
    @param path 文件绝对路径
    @return 小写十六进制 SHA-1 摘要
    """
    digest = hashlib.sha1()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def copy_cell_style(src_cell: Any, dst_cell: Any) -> None:
    """
    从源单元格完整深拷贝样式至目标单元格（字体、填充、边框、对齐、数字格式）
    @param src_cell 源单元格
    @param dst_cell 目标单元格
    """
    if src_cell.has_style:
        dst_cell.font = copy(src_cell.font)
        dst_cell.border = copy(src_cell.border)
        dst_cell.fill = copy(src_cell.fill)
        dst_cell.number_format = src_cell.number_format
        dst_cell.protection = copy(src_cell.protection)
        dst_cell.alignment = copy(src_cell.alignment)


def rotate_backups(backup_root: str, max_backups: int) -> None:
    """
    清理多余的历史快照，保证最多保留 max_backups 份最新快照
    @param backup_root 备份根目录
    @param max_backups 最大保留快照份数
    """
    if not os.path.isdir(backup_root) or max_backups <= 0:
        return

    snapshots = sorted(
        name for name in os.listdir(backup_root)
        if os.path.isdir(os.path.join(backup_root, name)) and SNAPSHOT_DIR_PATTERN.match(name)
    )
    while len(snapshots) > max_backups:
        oldest = snapshots.pop(0)
        oldest_path = os.path.join(backup_root, oldest)
        try:
            shutil.rmtree(oldest_path)
            print(f"[备份轮转] 已自动清理历史快照: {oldest}")
        except OSError as e:
            print(f"[警告] 无法清理过期备份目录 {oldest_path}: {e}", file=sys.stderr)


class BackupSession:
    """
    写前快照会话：惰性创建防冲突时间戳目录，逐表备份原文件并落 sha1 清单
    """

    def __init__(self, backup_root: str, excel_dir: str, max_backups: int = 10) -> None:
        """
        初始化写前快照会话
        @param backup_root 备份根目录
        @param excel_dir Excel 数据表源目录
        @param max_backups 最大保留历史快照份数
        """
        self.backup_root = backup_root
        self.excel_dir = excel_dir
        self.max_backups = max_backups
        self.snapshot_dir: Optional[str] = None
        self.entries: List[Dict[str, str]] = []

    def _allocate_snapshot_dir(self) -> str:
        """
        分配唯一快照目录（同秒内连续运行自动追加递增序号避免覆盖）
        @return 已创建的快照目录绝对路径
        """
        os.makedirs(self.backup_root, exist_ok=True)
        base_stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        candidate = os.path.join(self.backup_root, base_stamp)
        seq = 1
        while True:
            try:
                os.makedirs(candidate, exist_ok=False)
                return candidate
            except FileExistsError:
                candidate = os.path.join(self.backup_root, f"{base_stamp}_{seq:02d}")
                seq += 1

    def backup(self, xlsx_path: str) -> str:
        """
        在写入前备份指定 Excel 原文件
        @param xlsx_path 待写入的 Excel 绝对路径
        @return 备份文件路径
        """
        if self.snapshot_dir is None:
            self.snapshot_dir = self._allocate_snapshot_dir()

        rel_path = os.path.relpath(xlsx_path, self.excel_dir)
        dst_path = os.path.join(self.snapshot_dir, rel_path)
        os.makedirs(os.path.dirname(dst_path), exist_ok=True)
        shutil.copy2(xlsx_path, dst_path)
        self.entries.append({"rel_path": rel_path, "sha1": file_sha1(dst_path)})
        return dst_path

    def finalize(self) -> Optional[str]:
        """
        原子落盘快照清单并轮转历史快照；无备份条目时自动清理空快照目录并返回 None
        @return 快照目录路径，未产生备份返回 None
        """
        if self.snapshot_dir is None or not self.entries:
            if self.snapshot_dir and os.path.isdir(self.snapshot_dir):
                shutil.rmtree(self.snapshot_dir, ignore_errors=True)
                self.snapshot_dir = None
            return None

        manifest = {
            "timestamp": os.path.basename(self.snapshot_dir),
            "excel_dir": self.excel_dir,
            "entries": self.entries,
        }
        manifest_path = os.path.join(self.snapshot_dir, MANIFEST_FILENAME)
        tmp_manifest = f"{manifest_path}{TMP_SUFFIX}"
        with open(tmp_manifest, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(manifest, handle, ensure_ascii=False, indent=2)
        os.replace(tmp_manifest, manifest_path)

        rotate_backups(self.backup_root, self.max_backups)
        return self.snapshot_dir


def resolve_backup_dir(cfg: Dict[str, Any], cfg_path: Optional[str]) -> str:
    """
    解析备份根目录：配置 back_write.backup_dir 优先，否则回落工具目录旁的 DataTables_Backup
    @param cfg 工具配置字典
    @param cfg_path 配置文件路径
    @return 备份根目录路径
    """
    configured = cfg.get("backup_dir")
    if configured:
        return configured
    base = os.path.dirname(os.path.abspath(cfg_path)) if cfg_path else os.getcwd()
    return os.path.normpath(os.path.join(base, "../../DataTables_Backup"))


def resolve_snapshot_dir(backup_root: str, snapshot: Optional[str] = None) -> Optional[str]:
    """
    解析待回滚快照目录：显式指定优先（支持绝对路径或备份根下子目录名），否则取含完整清单的最新时间戳快照
    @param backup_root 备份根目录
    @param snapshot 显式快照目录（可选）
    @return 快照目录路径，未找到返回 None
    """
    if snapshot:
        if os.path.isdir(snapshot):
            return snapshot
        if backup_root:
            candidate = os.path.join(backup_root, snapshot)
            if os.path.isdir(candidate):
                return candidate
        return None
    if not os.path.isdir(backup_root):
        return None

    snapshots = sorted(
        name for name in os.listdir(backup_root)
        if (
            os.path.isdir(os.path.join(backup_root, name))
            and SNAPSHOT_DIR_PATTERN.match(name)
            and os.path.isfile(os.path.join(backup_root, name, MANIFEST_FILENAME))
        )
    )
    if not snapshots:
        return None
    return os.path.join(backup_root, snapshots[-1])


def _copy_atomic(src_path: str, dst_path: str) -> None:
    """
    原子复制文件（先复制至目标同目录 .tmp，再 os.replace）
    @param src_path 源文件路径
    @param dst_path 目标文件路径
    """
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    tmp_path = f"{dst_path}{TMP_SUFFIX}"
    try:
        shutil.copy2(src_path, tmp_path)
        os.replace(tmp_path, dst_path)
    except OSError:
        if os.path.isfile(tmp_path):
            try:
                os.remove(tmp_path)
            except OSError:
                pass
        raise


def perform_rollback(
    backup_root: str,
    snapshot: Optional[str] = None,
    table: Optional[str] = None,
    expected_excel_dir: Optional[str] = None,
) -> Tuple[bool, str]:
    """
    按快照清单校验路径边界与 sha1 后把写前文件原子还原回 Excel
    @param backup_root 备份根目录
    @param snapshot 显式快照目录（可选）
    @param table 仅还原指定表名（可选）
    @param expected_excel_dir 期望的 Excel 源目录（提供时强制核对清单 excel_dir 防止篡改越界）
    @return (是否成功, 结果描述)
    """
    snapshot_dir = resolve_snapshot_dir(backup_root, snapshot)
    if snapshot_dir is None:
        return False, f"未找到可用快照目录（备份根: {backup_root}）"

    manifest_path = os.path.join(snapshot_dir, MANIFEST_FILENAME)
    if not os.path.isfile(manifest_path):
        return False, f"快照清单缺失（该快照可能由旧版本产生）: {manifest_path}"

    try:
        with open(manifest_path, "r", encoding="utf-8") as handle:
            manifest = json.load(handle)
    except (OSError, json.JSONDecodeError, UnicodeDecodeError) as e:
        return False, f"读取快照清单失败 {manifest_path}: {e}"

    excel_dir = manifest.get("excel_dir") or ""
    if not excel_dir:
        return False, f"快照清单缺少 excel_dir 字段: {manifest_path}"

    snap_real = os.path.normpath(os.path.realpath(snapshot_dir))
    excel_real = os.path.normpath(os.path.realpath(excel_dir))
    if expected_excel_dir:
        expected_real = os.path.normpath(os.path.realpath(expected_excel_dir))
        if excel_real != expected_real:
            return False, f"快照清单 excel_dir 与当前配置不一致（已拦截）: {excel_dir} != {expected_excel_dir}"

    to_restore: List[Tuple[str, str, str]] = []
    skipped: List[str] = []
    for entry in manifest.get("entries", []):
        if not isinstance(entry, dict):
            continue
        rel_path = entry.get("rel_path") or ""
        if not rel_path:
            continue
        if os.path.isabs(rel_path):
            return False, f"快照清单包含非法绝对路径（已拦截）: {rel_path}"
        if table:
            table_name = os.path.splitext(os.path.basename(rel_path))[0]
            if table != table_name and strip_dt_prefix(table) != strip_dt_prefix(table_name):
                skipped.append(rel_path)
                continue

        src_path = os.path.normpath(os.path.realpath(os.path.join(snap_real, rel_path)))
        dst_path = os.path.normpath(os.path.realpath(os.path.join(excel_real, rel_path)))
        try:
            if os.path.commonpath([snap_real, src_path]) != snap_real or src_path == snap_real:
                return False, f"快照源路径越界（已拦截）: {rel_path}"
            if os.path.commonpath([excel_real, dst_path]) != excel_real or dst_path == excel_real:
                return False, f"快照目标路径越界（已拦截）: {rel_path}"
        except ValueError:
            return False, f"快照路径跨卷越界（已拦截）: {rel_path}"

        if not os.path.isfile(src_path):
            return False, f"快照备份文件缺失（快照不完整）: {src_path}"

        expected_sha1 = str(entry.get("sha1") or "").strip()
        if not expected_sha1 or file_sha1(src_path) != expected_sha1:
            return False, f"快照文件完整性校验失败（SHA-1 不匹配或缺失）: {src_path}"

        to_restore.append((rel_path, src_path, dst_path))

    if not to_restore:
        return False, f"快照中未找到可还原的数据表条目（table={table or '*'}）"

    restored: List[str] = []
    for rel_path, src_path, dst_path in to_restore:
        try:
            _copy_atomic(src_path, dst_path)
        except OSError as e:
            return False, f"还原文件失败 {dst_path}: {e}"
        restored.append(rel_path)

    return True, f"已从快照 {os.path.basename(snapshot_dir)} 还原 {len(restored)} 张表（跳过 {len(skipped)}）"


def _extract_record_key(record: Dict[str, Any]) -> Optional[str]:
    """
    安全提取 JSON 记录主键（兼容整型 0 等假值键）
    @param record JSON 行对象
    @return 非空主键字符串，缺失返回 None
    """
    raw_key = record["RowName"] if ("RowName" in record and record["RowName"] is not None) else record.get("Name")
    if raw_key is None:
        return None
    key_str = str(raw_key).strip()
    return key_str if key_str else None


def _lookup_record_value(record: Dict[str, Any], col: table_schema.ColumnInfo) -> Tuple[bool, Any]:
    """
    在 JSON 记录中按字段名 / UE 属性名 / Name 键别名查找列值
    @param record JSON 行对象
    @param col 列元数据
    @return (是否命中, 原始值)
    """
    if col.field_name in record:
        return True, record[col.field_name]
    prop_name = to_property_name(col.field_name, col.cpp_type)
    if prop_name in record:
        return True, record[prop_name]
    if col.field_name == "RowName" and "Name" in record:
        return True, record["Name"]
    if col.field_name == "Name" and "RowName" in record:
        return True, record["RowName"]
    return False, None


def _load_json_records(
    json_path: str,
) -> Tuple[Optional[List[Any]], Dict[str, Dict[str, Any]], List[str], Optional[str]]:
    """
    读取并索引 JSON 数据记录
    @param json_path JSON 文件路径
    @return (原始记录列表, 主键->记录映射, 有序主键列表, 错误描述)
    """
    try:
        with open(json_path, "r", encoding="utf-8") as handle:
            json_records = json.load(handle)
    except (OSError, json.JSONDecodeError, UnicodeDecodeError) as e:
        return None, {}, [], f"读取 JSON 失败 {json_path}: {e}"

    if not isinstance(json_records, list):
        return None, {}, [], f"JSON 根结构必须为对象数组: {json_path}"

    json_map: Dict[str, Dict[str, Any]] = {}
    json_keys_ordered: List[str] = []
    for record in json_records:
        if not isinstance(record, dict):
            continue
        key_str = _extract_record_key(record)
        if key_str is not None:
            json_map[key_str] = record
            json_keys_ordered.append(key_str)

    return json_records, json_map, json_keys_ordered, None


def _run_prewrite_checks(
    json_records: List[Any],
    json_map: Dict[str, Dict[str, Any]],
    columns: List[table_schema.ColumnInfo],
    table_name: str,
    fill_defaults: bool,
    result: SyncResult,
) -> Set[Tuple[str, str]]:
    """
    执行写前内存校验：类型可转性、表头结构漂移、非派生 FText 探测与主键规则校验
    @return 转换失败的 (key, field_name) 坐标集合
    """
    invalid_cells: Set[Tuple[str, str]] = set()
    for key, record in json_map.items():
        for col in columns:
            found, raw_val = _lookup_record_value(record, col)
            if not found:
                continue
            try:
                table_schema.convert_for_excel(raw_val, col.cpp_type, fill_defaults=fill_defaults, strict=True)
            except table_schema.SchemaValidationError as e:
                result.conversion_errors.append(f"[{key}.{col.field_name}] {e}")
                invalid_cells.add((key, col.field_name))

    if json_map:
        record_keys: Set[str] = set()
        for record in json_map.values():
            record_keys.update(record.keys())
        column_names = {c.field_name for c in columns}
        recognized_names = set(column_names) | {to_property_name(c.field_name, c.cpp_type) for c in columns} | {"Name", "RowName"}
        json_extra_fields = sorted(record_keys - recognized_names)
        matched_excel_cols = {
            c.field_name for c in columns
            if c.field_name in record_keys or to_property_name(c.field_name, c.cpp_type) in record_keys or c.is_key
        }
        excel_columns_without_json = sorted(column_names - matched_excel_cols)
        if json_extra_fields:
            result.drift_issues.append(f"JSON 存在 Excel 无对应列的字段: {', '.join(json_extra_fields)}")
        if excel_columns_without_json:
            result.drift_issues.append(f"Excel 存在但 JSON 缺失的字段列: {', '.join(excel_columns_without_json)}")

    for record in json_map.values():
        for val in record.values():
            if isinstance(val, str) and val.strip().startswith(table_schema.TEXT_LITERAL_PREFIXES):
                result.text_literal_count += 1
    if result.text_literal_count > 0:
        result.warnings.append(
            f"[非派生 JSON] 本表 JSON 含 {result.text_literal_count} 处 FText 字面量（NSLOCTEXT/LOCTEXT），"
            f"应为 UE 侧导出物而非 Excel 正向派生；本次回写将按 Excel 可读形态归一，属预期规范化。"
        )

    rule_records: List[Tuple[int, str, Dict[str, str]]] = []
    for seq, rec in enumerate(json_records, start=1):
        if not isinstance(rec, dict):
            result.conversion_errors.append(f"[Row#{seq}] JSON 数组元素必须为对象，实际为: {type(rec).__name__}")
            continue
        rule_records.append((seq, _extract_record_key(rec) or "", {c.field_name: str(rec.get(c.field_name, "")) for c in columns}))
    result.rule_issues.extend(table_rules.check_row_keys(rule_records, table_name))
    for issue in result.rule_issues:
        if issue.code in ("INVALID_KEY", "DUPLICATE_KEY") and issue.key:
            for col in columns:
                invalid_cells.add((issue.key, col.field_name))
    return invalid_cells


def _sync_single_row(
    sheet: Any,
    row_num: int,
    key: str,
    record: Dict[str, Any],
    columns: List[table_schema.ColumnInfo],
    invalid_cells: Set[Tuple[str, str]],
    options: Any,
    protect_formulas: bool,
    float_eps: float,
    fill_defaults: bool,
    result: SyncResult,
) -> int:
    """
    同步单个已存在的 Excel 数据行
    @return 该行实际产生的单元格变更数
    """
    changes = 0
    for col in columns:
        found, raw_json_val = _lookup_record_value(record, col)
        if not found or (key, col.field_name) in invalid_cells:
            continue

        cell = sheet.cell(row=row_num, column=col.col_idx + 1)
        orig_val = cell.value

        if orig_val is not None and str(orig_val).strip().startswith("=") and protect_formulas:
            result.warnings.append(f"[{key}.{col.field_name}] 单元格为公式 ({orig_val})，已自动保护跳过。若需覆盖请用 --force-formula-overwrite")
            continue

        target_val = table_schema.convert_for_excel(raw_json_val, col.cpp_type, fill_defaults=fill_defaults, strict=False)
        if target_val is None and (orig_val is None or orig_val == ""):
            continue
        if table_schema.values_are_equal(orig_val, target_val, col.cpp_type, epsilon=float_eps):
            continue

        if target_val is None and not fill_defaults:
            result.warnings.append(f"[{key}.{col.field_name}] JSON 中为空值，保留 Excel 现有值 '{orig_val}'（如需清零请加 --fill-defaults）")
            continue

        result.diff_list.append({
            "type": "MODIFY",
            "key": key,
            "field": col.field_name,
            "old": orig_val,
            "new": target_val,
            "row": row_num,
            "col": col.col_idx + 1,
        })
        changes += 1
        if not options.dry_run:
            cell.value = target_val
    return changes


def _sync_existing_rows(
    sheet: Any,
    columns: List[table_schema.ColumnInfo],
    excel_rows: Dict[str, int],
    json_map: Dict[str, Dict[str, Any]],
    invalid_cells: Set[Tuple[str, str]],
    options: Any,
    protect_formulas: bool,
    float_eps: float,
    fill_defaults: bool,
    result: SyncResult,
) -> Tuple[List[int], int]:
    """
    比对并更新 Excel 中所有既有行，同时收集待物理删除的行号
    @return (rows_to_delete, changes_count)
    """
    rows_to_delete: List[int] = []
    changes_count = 0

    for key, row_num in excel_rows.items():
        if key not in json_map:
            if options.allow_delete:
                rows_to_delete.append(row_num)
                result.diff_list.append({
                    "type": "DELETE_ROW",
                    "key": key,
                    "field": "*",
                    "old": f"Row {row_num}",
                    "new": "<DELETED>",
                    "row": row_num,
                    "col": 0,
                })
                changes_count += 1
            else:
                result.warnings.append(f"行 [{key}] 在 JSON 中缺失，已安全保留在 Excel 中（如需删除请指定 --allow-delete）。")
            continue

        changes_count += _sync_single_row(
            sheet, row_num, key, json_map[key], columns, invalid_cells,
            options, protect_formulas, float_eps, fill_defaults, result,
        )

    return rows_to_delete, changes_count


def _append_new_rows(
    sheet: Any,
    columns: List[table_schema.ColumnInfo],
    excel_rows: Dict[str, int],
    json_map: Dict[str, Dict[str, Any]],
    json_keys_ordered: List[str],
    invalid_cells: Set[Tuple[str, str]],
    options: Any,
    fill_defaults: bool,
    result: SyncResult,
) -> int:
    """
    将 JSON 中新增的行插入数据区末尾并克隆样式
    @return 新增行数
    """
    last_valid_row = max(excel_rows.values()) if excel_rows else 3
    sample_style_row = last_valid_row if last_valid_row >= 4 else None
    added = 0

    for key in json_keys_ordered:
        if key in excel_rows:
            continue
        if any((key, c.field_name) in invalid_cells and c.is_key for c in columns):
            continue

        new_row_idx = last_valid_row + 1
        record = json_map[key]
        result.diff_list.append({
            "type": "ADD_ROW",
            "key": key,
            "field": "*",
            "old": "<NONE>",
            "new": f"New Row {new_row_idx}",
            "row": new_row_idx,
            "col": 0,
        })
        added += 1

        if options.dry_run:
            last_valid_row = new_row_idx
            excel_rows[key] = new_row_idx
            continue

        if last_valid_row < sheet.max_row:
            sheet.insert_rows(new_row_idx, 1)

        last_valid_row = new_row_idx
        excel_rows[key] = new_row_idx

        if sample_style_row:
            for col in columns:
                copy_cell_style(
                    sheet.cell(row=sample_style_row, column=col.col_idx + 1),
                    sheet.cell(row=new_row_idx, column=col.col_idx + 1),
                )

        for col in columns:
            if (key, col.field_name) in invalid_cells:
                continue
            found, raw_val = _lookup_record_value(record, col)
            if not found and col.is_key:
                raw_val = key
            sheet.cell(row=new_row_idx, column=col.col_idx + 1).value = table_schema.convert_for_excel(
                raw_val, col.cpp_type, fill_defaults=fill_defaults, strict=False
            )

    return added


def _save_workbook_atomic(
    wb: Any,
    xlsx_path: str,
    backup_session: Optional[BackupSession],
    result: SyncResult,
) -> bool:
    """
    执行写前快照备份并以临时文件 + os.replace 原子保存工作簿
    @return 成功返回 True，失败填充 result.error 并返回 False
    """
    if backup_session is not None:
        try:
            result.backup_path = backup_session.backup(xlsx_path)
        except OSError as e:
            result.error = f"写前备份原文件失败（中止写入）{xlsx_path}: {e}"
            return False

    tmp_path = xlsx_path + TMP_SUFFIX
    try:
        wb.save(tmp_path)
        os.replace(tmp_path, xlsx_path)
        return True
    except OSError as e:
        if os.path.isfile(tmp_path):
            try:
                os.remove(tmp_path)
            except OSError as cleanup_error:
                print(f"[警告] 无法清理临时文件 {tmp_path}: {cleanup_error}", file=sys.stderr)
        result.error = f"保存 Excel 失败（原文件未被破坏）{xlsx_path}: {e}"
        return False


def perform_sync_for_table(
    xlsx_path: str,
    json_path: str,
    options: Any,
    backup_session: Optional[BackupSession] = None,
) -> SyncResult:
    """
    针对单张数据表执行 JSON ➔ Excel 反向同步（写前备份 + 临时文件原子替换）
    @param xlsx_path 目标 Excel 路径
    @param json_path 源 JSON 路径
    @param options 命令行与配置选项
    @param backup_session 写前快照会话（None 表示不备份）
    @return SyncResult
    """
    result = SyncResult()
    table_name = os.path.splitext(os.path.basename(xlsx_path))[0]
    float_eps = getattr(options, "float_epsilon", table_schema.DEFAULT_FLOAT_EPSILON)
    fill_defaults = getattr(options, "fill_defaults", False)
    force_formula = getattr(options, "force_formula", False)
    protect_formulas = False if force_formula else bool(getattr(options, "protect_formulas_config", True))

    json_records, json_map, json_keys_ordered, load_err = _load_json_records(json_path)
    if load_err or json_records is None:
        result.error = load_err
        return result

    try:
        wb = openpyxl.load_workbook(xlsx_path, data_only=False)
    except (OSError, zipfile.BadZipFile, InvalidFileException) as e:
        result.error = f"打开 Excel 失败 {xlsx_path}: {e}"
        return result

    try:
        sheet, sheet_title = table_schema.resolve_openpyxl_sheet(wb, table_name)
        if len(wb.sheetnames) > 1 and sheet_title != wb.active.title:
            result.warnings.append(f"多工作表匹配：已锁定数据表工作表 '{sheet_title}'（当前活动工作表为 '{wb.active.title}'）")

        if sheet.max_row < 3:
            result.error = f"Excel 表头不足 3 行契约: {xlsx_path} (工作表: {sheet_title})"
            return result

        row1 = [sheet.cell(row=1, column=c).value for c in range(1, sheet.max_column + 1)]
        row2 = [sheet.cell(row=2, column=c).value for c in range(1, sheet.max_column + 1)]
        row3 = [sheet.cell(row=3, column=c).value for c in range(1, sheet.max_column + 1)]
        columns = table_schema.parse_header_columns(row1, row2, row3)
        if not columns:
            result.error = f"未检测到有效字段列: {xlsx_path}"
            return result

        row_name_col = next((c for c in columns if c.is_key), columns[0])
        excel_rows: Dict[str, int] = {}
        for r in range(4, sheet.max_row + 1):
            cell_val = sheet.cell(row=r, column=row_name_col.col_idx + 1).value
            if cell_val is not None and str(cell_val).strip():
                excel_rows[str(cell_val).strip()] = r

        invalid_cells = _run_prewrite_checks(json_records, json_map, columns, table_name, fill_defaults, result)
        if options.strict and (result.conversion_errors or result.rule_issues or result.drift_issues):
            result.error = (
                f"前置数据校验失败（类型违规 {len(result.conversion_errors)} 处 / 规则违规 "
                f"{len(result.rule_issues)} 处 / 结构漂移 {len(result.drift_issues)} 处，已终止写入）"
            )
            return result

        rows_to_delete, changes_count = _sync_existing_rows(
            sheet, columns, excel_rows, json_map, invalid_cells,
            options, protect_formulas, float_eps, fill_defaults, result,
        )

        if rows_to_delete:
            deleted_row_set = set(rows_to_delete)
            overwritten_coords = {
                (d["row"], d["col"]) for d in result.diff_list if d.get("type") == "MODIFY"
            }
            has_surviving_formula = False
            for r in range(4, sheet.max_row + 1):
                if r in deleted_row_set:
                    continue
                for c_idx in range(1, sheet.max_column + 1):
                    if (r, c_idx) in overwritten_coords:
                        continue
                    c_val = sheet.cell(row=r, column=c_idx).value
                    if c_val is not None and str(c_val).strip().startswith("="):
                        has_surviving_formula = True
                        break
                if has_surviving_formula:
                    break

            if has_surviving_formula:
                formula_message = "[公式安全告警] 本表包含计算公式；执行物理删行可能导致外部公式引用位移或变为 #REF!。"
                if options.strict:
                    result.error = f"{formula_message}（--strict 下已阻断删行，未做任何修改）"
                    return result
                result.warnings.append(formula_message)

            if not options.dry_run:
                for r in sorted(rows_to_delete, reverse=True):
                    sheet.delete_rows(r, 1)

            sorted_deleted = sorted(rows_to_delete)
            updated_excel_rows: Dict[str, int] = {}
            for k, old_r in excel_rows.items():
                if old_r in deleted_row_set:
                    continue
                shift = sum(1 for del_r in sorted_deleted if del_r < old_r)
                updated_excel_rows[k] = old_r - shift
            excel_rows = updated_excel_rows

        changes_count += _append_new_rows(
            sheet, columns, excel_rows, json_map, json_keys_ordered,
            invalid_cells, options, fill_defaults, result,
        )

        if changes_count > 0 and not options.dry_run:
            if not _save_workbook_atomic(wb, xlsx_path, backup_session, result):
                return result

        result.has_changes = changes_count > 0
        return result
    finally:
        wb.close()


def sync_all(cfg_path: Optional[str], options: Any) -> bool:
    """
    全量或定向执行 JSON ➔ Excel 反向同步
    @param cfg_path 工具配置文件路径
    @param options 命令行选项命名空间
    @return 无阻塞错误返回 True，否则返回 False
    """
    cfg = tool_config.load_config(cfg_path, (tool_config.KEY_DATA_TABLES_SOURCE_DIR, "json_output_dir"))
    excel_dir = cfg[tool_config.KEY_DATA_TABLES_SOURCE_DIR]
    json_dir = cfg["json_output_dir"]
    backup_dir = resolve_backup_dir(cfg, cfg_path)
    max_backups = int(cfg.get("max_backups", 10))
    options.protect_formulas_config = cfg.get("protect_formulas", True)

    print("=" * 65)
    print(" JSON ➔ Excel 反向同步管线启动（单一真源 · 写前快照 · 原子落盘）")
    print(f" Excel 源目录 : {excel_dir}")
    print(f" JSON 数据目录: {json_dir}")
    print(f" 运行模式     : {'[DRY-RUN 仅预览]' if options.dry_run else '[正式回写]'}")
    print(f" 备份目录     : {backup_dir if not options.no_backup else '[已禁用备份]'}")
    print(f" 公式保护     : {'[强制覆盖]' if options.force_formula else ('[启用]' if options.protect_formulas_config else '[禁用]')}")
    print(f" 空值策略     : {'[回填类型默认值]' if options.fill_defaults else '[保持原单元格留空]'}")
    print(f" 浮点容差     : {options.float_epsilon}")
    print("=" * 65)

    matched_pairs: List[Tuple[str, str, str]] = []
    for root, _, files in os.walk(json_dir):
        for file_name in files:
            if not file_name.endswith(".json") or file_name.startswith("."):
                continue

            json_path = os.path.join(root, file_name)
            rel_dir = os.path.relpath(root, json_dir)
            table_name = os.path.splitext(file_name)[0]

            if options.table:
                if options.table != table_name and strip_dt_prefix(options.table) != strip_dt_prefix(table_name):
                    continue

            xlsx_path = os.path.normpath(os.path.join(excel_dir, rel_dir, f"{table_name}.xlsx"))
            if not os.path.isfile(xlsx_path):
                root_xlsx_path = os.path.normpath(os.path.join(excel_dir, f"{table_name}.xlsx"))
                if os.path.isfile(root_xlsx_path):
                    xlsx_path = root_xlsx_path
                else:
                    continue

            matched_pairs.append((xlsx_path, json_path, table_name))

    matched_pairs.sort(key=lambda item: item[2])
    if not matched_pairs:
        if options.table:
            print(f"[错误] 未找到指定表 '{options.table}' 对应的 JSON ➔ Excel 数据表对（搜索目录: {json_dir}）。", file=sys.stderr)
            return False
        print(f"[提示] 未找到匹配的 JSON ➔ Excel 数据表对（搜索目录: {json_dir}）。")
        return True

    backup_session = BackupSession(backup_dir, excel_dir, max_backups) if not options.no_backup and not options.dry_run else None

    total_changed_tables = 0
    total_diff_count = 0
    total_conversion_errors = 0
    total_drift_issues = 0
    total_rule_issues = 0
    has_error = False

    snapshot_dir: Optional[str] = None
    try:
        for xlsx_path, json_path, table_name in matched_pairs:
            result = perform_sync_for_table(xlsx_path, json_path, options, backup_session)

            for w in result.warnings:
                print(f"  \033[33m[告警] {w}\033[0m")
            for ce in result.conversion_errors:
                print(f"  \033[33m[转换失败] {ce}\033[0m")
            for drift in result.drift_issues:
                print(f"  \033[33m[结构漂移] {drift}\033[0m")
            for issue in result.rule_issues:
                print(f"  \033[31m[规则违规] {issue.code} 主键[{issue.key or '-'}] 第 {issue.row_idx} 行\033[0m")
            total_conversion_errors += len(result.conversion_errors)
            total_drift_issues += len(result.drift_issues)
            total_rule_issues += len(result.rule_issues)

            if result.error:
                print(f"  \033[31m[错误] {result.error}\033[0m")
                has_error = True
                continue

            if result.has_changes:
                total_changed_tables += 1
                total_diff_count += len(result.diff_list)
                print(f"\033[36m▶ [{table_name}] 发现 {len(result.diff_list)} 处变动:\033[0m")
                for diff in result.diff_list[:15]:
                    if diff["type"] == "MODIFY":
                        print(f"    • {diff['key']}.{diff['field']}: {diff['old']} ➔ \033[32m{diff['new']}\033[0m")
                    elif diff["type"] == "ADD_ROW":
                        print(f"    • \033[32m[+新增行]\033[0m {diff['key']}")
                    elif diff["type"] == "DELETE_ROW":
                        print(f"    • \033[31m[-删除行]\033[0m {diff['key']}")
                if len(result.diff_list) > 15:
                    print(f"    ... 以及其余 {len(result.diff_list) - 15} 处变更")

                status_text = "[已预览]" if options.dry_run else "[✓ 已更新]"
                backup_note = f"（写前快照: {os.path.basename(result.backup_path)}）" if result.backup_path else ""
                print(f"  {status_text} {os.path.basename(xlsx_path)}{backup_note}\n")
            else:
                print(f"  [无变动] {table_name}")
    finally:
        if backup_session is not None:
            snapshot_dir = backup_session.finalize()

    print("=" * 65)
    print(f" 同步完成！共检测 {len(matched_pairs)} 张表，{total_changed_tables} 张表产生变动（共 {total_diff_count} 处变更）。")
    if total_conversion_errors > 0:
        print(f" \033[33m[转换失败汇总] 共 {total_conversion_errors} 处（已跳过、未写入；--strict 时计入退出码）\033[0m")
    if total_rule_issues > 0:
        print(f" \033[33m[规则违规汇总] 共 {total_rule_issues} 处（主键缺失/非法/重复；--strict 时计入退出码）\033[0m")
    if total_drift_issues > 0:
        print(f" \033[33m[结构漂移汇总] 共 {total_drift_issues} 处（JSON 与 Excel 表头不同步；--strict 时计入退出码）\033[0m")
    if snapshot_dir:
        print(f" \033[32m[写前快照已归档] 快照位置: {snapshot_dir}\033[0m")
        print(" \033[32m[回滚方式] python3 json_to_excel.py --config <配置> --rollback\033[0m")
    elif options.dry_run:
        print(" \033[33m[Dry Run] 仅完成差异分析，未向磁盘写入任何更改。\033[0m")
    elif options.no_backup:
        print(" \033[33m[提示] 本次以 --no-backup 运行，未生成写前快照。\033[0m")
    print("=" * 65)

    if has_error:
        return False
    if options.strict and any((total_conversion_errors, total_rule_issues, total_drift_issues)):
        return False
    return True


def main() -> None:
    """
    命令行入口：解析参数并执行 JSON ➔ Excel 反向同步或快照回滚
    """
    parser = argparse.ArgumentParser(description="JSON 反写 Excel 数据表同步工具（单一真源 · 写前快照 · 原子落盘）")
    parser.add_argument("--config", dest="config_path", default=None, help="JSON 配置文件路径")
    parser.add_argument("--table", dest="table", default=None, help="仅同步指定数据表名（如 DT_Example_Table）")
    parser.add_argument("--dry-run", dest="dry_run", action="store_true", help="仅打印变动差异报告，不修改 Excel 文件")
    parser.add_argument("--allow-delete", dest="allow_delete", action="store_true", help="允许物理删除 Excel 中存在但 JSON 中缺失的行")
    parser.add_argument("--force-formula-overwrite", dest="force_formula", action="store_true", help="强制覆盖带有计算公式的单元格")
    parser.add_argument("--fill-defaults", dest="fill_defaults", action="store_true", help="空值/null 写入对应 C++ 类型的默认零值（0/False）而不是保持 Excel 留空")
    parser.add_argument("--float-epsilon", dest="float_epsilon", type=float, default=table_schema.DEFAULT_FLOAT_EPSILON, help=f"浮点比较容差（默认 {table_schema.DEFAULT_FLOAT_EPSILON}）")
    parser.add_argument("--no-backup", dest="no_backup", action="store_true", help="禁用写前自动快照备份")
    parser.add_argument("--strict", dest="strict", action="store_true", help="存在转换失败/规则违规/结构漂移时以非零退出码结束")
    parser.add_argument("--rollback", dest="rollback", action="store_true", help="按快照清单把写前文件还原回 Excel（不写回数据）")
    parser.add_argument("--snapshot", dest="snapshot", default=None, help="回滚时指定快照目录（缺省取最新时间戳快照）")
    parsed_args = parser.parse_args()

    if parsed_args.rollback:
        cfg = tool_config.load_config(parsed_args.config_path, (tool_config.KEY_DATA_TABLES_SOURCE_DIR,))
        backup_dir = resolve_backup_dir(cfg, parsed_args.config_path)
        ok, message = perform_rollback(
            backup_dir,
            parsed_args.snapshot,
            parsed_args.table,
            expected_excel_dir=cfg.get(tool_config.KEY_DATA_TABLES_SOURCE_DIR),
        )
        print(f"[回滚] {message}")
        sys.exit(0 if ok else 1)

    success = sync_all(parsed_args.config_path, parsed_args)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
