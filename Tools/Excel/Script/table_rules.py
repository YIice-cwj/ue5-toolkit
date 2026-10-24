#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
数据表规则单一真源 (table_rules.py) —— 主键 / 标量类型 / 跨表外键

- 规则判定为纯函数，**不打印**：由调用方（validate_tables.py 的阻断式校验、json_to_excel.py 的写前校验）各自决定呈现与处置
- validate_tables.py 与 json_to_excel.py 共用本模块，消除「两套规则实现必然漂移」的隐患
- 表信息字典（tables）期望键：file / chinese_names / field_names / cpp_types / keys / rows / ordered_rows / prefixes
"""

import dataclasses
import re
from typing import Any, Dict, List, Optional, Sequence, Set, Tuple

# 主键字符合法性（不允许空格与特殊标点）
KEY_PATTERN = re.compile(r"^[A-Za-z0-9_]+$")

# 问题级别：error 触发阻断，warning 仅提示
LEVEL_ERROR = "error"
LEVEL_WARNING = "warning"

# 语义 Stem 匹配时忽略的通用大类词
DEFAULT_GENERIC_STEM_WORDS = ("zombie", "player", "item", "gameplay", "ui", "shared", "audio")
GENERIC_STEM_WORDS = set(DEFAULT_GENERIC_STEM_WORDS)


@dataclasses.dataclass
class RuleIssue:
    """规则问题条目（判定结果的结构化载体，交由调用方格式化）"""
    code: str                      # MISSING_KEY / INVALID_KEY / DUPLICATE_KEY / INVALID_SCALAR / DANGLING_FK / TARGET_TABLE_MISSING
    level: str                     # LEVEL_ERROR / LEVEL_WARNING
    table: str                     # 工作表名（表标识）
    file: str = ""                 # 源文件名
    row_idx: int = 0               # 行号（Excel 行号或记录序号）
    key: str = ""                  # 主键值
    field: str = ""                # 字段名
    type_name: str = ""            # 字段类型声明（标量类型问题用）
    value: str = ""                # 触发值
    detail: str = ""               # 附加说明（标量原因 / "目标表|推导来源"）


def is_valid_key(key: str) -> bool:
    """主键字符合法性判定"""
    return bool(KEY_PATTERN.match(key or ""))


def _key_issue(key: str, row_idx: int, seen: Set[str]) -> Optional[RuleIssue]:
    """
    主键三类问题判定（缺失 / 非法字符 / 重复）
    @param key 主键值（已 strip）
    @param row_idx 行号
    @param seen 已出现的主键集合（命中重复时不重复登记）
    @return 问题条目；无问题返回 None
    """
    if not key:
        return RuleIssue(code="MISSING_KEY", level=LEVEL_ERROR, table="", row_idx=row_idx)
    if not is_valid_key(key):
        return RuleIssue(code="INVALID_KEY", level=LEVEL_ERROR, table="", row_idx=row_idx, key=key)
    if key in seen:
        return RuleIssue(code="DUPLICATE_KEY", level=LEVEL_ERROR, table="", row_idx=row_idx, key=key)
    seen.add(key)
    return None


def build_row_records(data_rows: Sequence[Sequence[Any]], field_names: Sequence[Any],
                      file_name: str = "", table_name: str = "") -> Tuple[List[Tuple[int, str, Dict[str, str]]], Set[str], List[RuleIssue]]:
    """
    由 Excel 数据行构造规则校验用记录，并完成主键三类校验
    @param data_rows 数据行（含坐标索引的二维表，行序即文件顺序）
    @param field_names 字段名行（第 2 行）
    @param file_name 源文件名（用于问题归属）
    @param table_name 工作表名
    @return (记录列表[(行号, 主键, {字段: 值})], 已出现主键集合, 主键问题列表)

    实现思路：整行全空视为尾部空白行跳过；主键不合法的行不进入记录集（与既有校验语义一致，
    即重复/非法主键的行不参与后续标量类型校验）
    """
    records: List[Tuple[int, str, Dict[str, str]]] = []
    row_keys: Set[str] = set()
    issues: List[RuleIssue] = []

    for idx, row in enumerate(data_rows, start=4):
        if not any(str(v).strip() for v in row):
            continue

        key = str(row[0]).strip() if row[0] else ""
        issue = _key_issue(key, idx, row_keys)
        if issue is not None:
            issue.table = table_name
            issue.file = file_name
            issues.append(issue)
            continue

        row_dict: Dict[str, str] = {}
        for col_idx, col_name in enumerate(field_names):
            val = str(row[col_idx]).strip() if col_idx < len(row) and row[col_idx] else ""
            row_dict[col_name] = val
        records.append((idx, key, row_dict))

    return records, row_keys, issues


def check_row_keys(records: Sequence[Tuple[int, str, Dict[str, str]]], table_name: str = "", file_name: str = "") -> List[RuleIssue]:
    """
    对既有记录集执行主键校验（供 JSON 侧写前校验使用，不构造 Excel 记录）
    """
    issues: List[RuleIssue] = []
    seen: Set[str] = set()
    for idx, key, *_ in records:
        issue = _key_issue(key, idx, seen)
        if issue is not None:
            issue.table = table_name
            issue.file = file_name
            issues.append(issue)
    return issues


def scalar_type_reason(cpp_type: Optional[str], value: str) -> Optional[str]:
    """
    标量类型合法性判定（float/double、int 系列、bool）
    @param cpp_type C++ 类型声明
    @param value 字段值
    @return 不合法原因描述；合法或类型不在判定范围返回 None
    """
    c_type = (cpp_type or "").lower()
    val = str(value).strip()

    if c_type in ("float", "double"):
        try:
            float(val)
        except ValueError:
            return "不是合法浮点数"
    elif c_type in ("int32", "int64", "uint32", "uint8", "int"):
        if not re.match(r"^-?\d+$", val):
            return "不是合法整数"
    elif c_type == "bool":
        if val.lower() not in ("true", "false", "1", "0"):
            return "不是合法布尔值 (应为 true/false 或 1/0)"
    return None


def check_scalar_values(records: Sequence[Tuple[int, str, Dict[str, str]]],
                        field_names: Sequence[Any], cpp_types: Sequence[Any],
                        table_name: str = "", file_name: str = "") -> List[RuleIssue]:
    """
    对记录集执行标量类型防呆校验（空值跳过）
    """
    issues: List[RuleIssue] = []
    for idx, key, row_dict, *_ in records:
        for col_idx, field in enumerate(field_names):
            if col_idx >= len(cpp_types):
                continue
            val = row_dict.get(field, "")
            if not val:
                continue
            reason = scalar_type_reason(cpp_types[col_idx], val)
            if reason is not None:
                issues.append(RuleIssue(
                    code="INVALID_SCALAR", level=LEVEL_ERROR, table=table_name, file=file_name,
                    row_idx=idx, key=key, field=field, type_name=str(cpp_types[col_idx]),
                    value=val, detail=reason,
                ))
    return issues


def build_table_index(tables: Dict[str, Dict[str, Any]], generic_stem_words: Optional[Sequence[str]] = None) -> Tuple[Dict[str, str], Dict[str, str]]:
    """
    构建主键前缀库与语义 Stem 库（供跨表外键智能推导）
    @param tables 表信息字典（按扫描顺序插入，顺序影响「前缀先到先得」语义）
    @param generic_stem_words 可选自定义或扩展的大类词集合
    @return (主键前缀映射, 语义 Stem 映射)
    """
    ignored_words = set(generic_stem_words) if generic_stem_words is not None else GENERIC_STEM_WORDS
    prefix_to_table: Dict[str, str] = {}
    stem_to_table: Dict[str, str] = {}

    for sheet_name, info in tables.items():
        for pfx in info.get("prefixes", ()):
            if pfx not in prefix_to_table:
                prefix_to_table[pfx] = sheet_name

        clean_stem = sheet_name.replace("DT_", "").replace("DT", "").lower()
        stem_to_table[clean_stem] = sheet_name
        for part in clean_stem.split("_"):
            if part and part not in ignored_words:
                stem_to_table[part] = sheet_name

    return prefix_to_table, stem_to_table


def discover_references(tables: Dict[str, Dict[str, Any]], external_rule_map: Dict[Tuple[str, str], str],
                        generic_stem_words: Optional[Sequence[str]] = None) -> List[Dict[str, Any]]:
    """
    三级跨表外键发现（表头显式声明 / 外置规则文件 / 智能前缀与语义推导）
    @param tables 表信息字典
    @param external_rule_map {(源表, 源字段): 目标表}
    @param generic_stem_words 可选自定义或扩展的大类词集合
    @return 引用规则列表（含 source_sheet / source_field / target_sheet / infer_source / col_idx）
    """
    prefix_to_table, stem_to_table = build_table_index(tables, generic_stem_words)
    discovered: List[Dict[str, Any]] = []

    for src_name, src_info in tables.items():
        fields = src_info["field_names"]
        types = src_info["cpp_types"]
        descs = src_info["chinese_names"]

        for col_idx, field in enumerate(fields):
            if not field or col_idx == 0:
                continue

            col_type = types[col_idx] if col_idx < len(types) else ""
            col_desc = descs[col_idx] if col_idx < len(descs) else ""

            target_table = None
            infer_source = ""

            matched = (re.search(r"[<\[(](DT_[A-Za-z0-9_]+)[>\])]", col_type)
                       or re.search(r"->\s*(DT_[A-Za-z0-9_]+)", col_type))
            if not matched and col_type.startswith("E") and col_type.endswith("Key"):
                enum_stem = col_type[1:-3].lower()
                for s_name in tables.keys():
                    if s_name.replace("DT_", "").replace("DT", "").lower() == enum_stem:
                        target_table = s_name
                        infer_source = f"强类型枚举显式声明 ({col_type})"
                        break

            if not matched and not target_table:
                matched = (re.search(r"\[FK:(DT_[A-Za-z0-9_]+)\]", col_desc)
                           or re.search(r"@(DT_[A-Za-z0-9_]+)", col_desc))

            if matched and not target_table:
                target_table = matched.group(1)
                infer_source = f"表头显式声明 ({matched.group(0)})"

            if not target_table and (src_name, field) in external_rule_map:
                target_table = external_rule_map[(src_name, field)]
                infer_source = "外置规则文件（配置提供）"

            if not target_table and (field.endswith("_id") or field.endswith("_key") or "_id_" in field):
                sample_vals = [r[2].get(field, "").strip() for r in src_info["ordered_rows"] if r[2].get(field, "").strip()]
                if sample_vals:
                    val_prefixes = {v.split("_")[0] + "_" for v in sample_vals if "_" in v}
                    matched_pfx = [p for p in val_prefixes if p in prefix_to_table]
                    if len(matched_pfx) == 1:
                        target_table = prefix_to_table[matched_pfx[0]]
                        infer_source = f"键值前缀自识别 [{matched_pfx[0]}]"
                    elif not matched_pfx:
                        f_stem = field.replace("_id", "").replace("_key", "").lower()
                        for part in f_stem.split("_"):
                            if part in stem_to_table:
                                target_table = stem_to_table[part]
                                infer_source = f"字段命名语义自识别 [{part}]"
                                break

            if target_table:
                discovered.append({
                    "source_sheet": src_name,
                    "source_field": field,
                    "target_sheet": target_table,
                    "infer_source": infer_source,
                    "col_idx": col_idx,
                })

    return discovered


def check_references(tables: Dict[str, Dict[str, Any]], rules: Sequence[Dict[str, Any]]) -> List[RuleIssue]:
    """
    外键引用完整性校验
    @return 问题列表（目标表缺位为 warning，悬空引用为 error）
    """
    issues: List[RuleIssue] = []

    for rule in rules:
        src_sheet = rule["source_sheet"]
        src_field = rule["source_field"]
        tgt_sheet = rule["target_sheet"]
        infer_source = rule["infer_source"]

        if src_sheet not in tables:
            continue

        if tgt_sheet not in tables:
            issues.append(RuleIssue(
                code="TARGET_TABLE_MISSING", level=LEVEL_WARNING, table=src_sheet,
                field=src_field, detail=f"{tgt_sheet}|{infer_source}",
            ))
            continue

        src_info = tables[src_sheet]
        tgt_keys = tables[tgt_sheet]["keys"]

        for idx, key, row_dict, _ in src_info["ordered_rows"]:
            fk_val = row_dict.get(src_field, "").strip()
            if not fk_val:
                continue
            if fk_val not in tgt_keys:
                issues.append(RuleIssue(
                    code="DANGLING_FK", level=LEVEL_ERROR, table=src_sheet, file=src_info["file"],
                    row_idx=idx, key=key, field=src_field, value=fk_val,
                    detail=f"{tgt_sheet}|{infer_source}",
                ))

    return issues
