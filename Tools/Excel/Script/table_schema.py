#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
数据表架构契约与编解码单一真源 (table_schema.py) —— SSOT (Single Source of Truth)

核心职责：
1. 三行表头契约解析（Row 1 中文说明 / Row 2 字段名 / Row 3 C++类型）；
2. 跨工具统一的工作表定位策略（Sheet Resolution），彻底消除 sheet1.xml vs wb.active 选表漂移；
3. 强类型系统与双向编解码唯一实现：
   - 数组/集合类型严格优先于标量判定；
   - 精确标量集合匹配（不误伤 FIntPoint / FIntVector 等结构体）；
   - 统一布尔解析真源（convert_scalar_value / scalar_values_equal / table_rules 共用）；
   - 健壮解析标准 JSON 数组、未加引号中括号 [a, b]、元组 (a, b) 与分隔符切分；
4. 空值语义保护：区分“有意留空”与“类型零值”，防止覆写破坏 Excel 留空单元格；
5. 内存前置数据校验：在落盘前完成结构与类型合规性自检。
"""

import dataclasses
import decimal
import json
import math
import posixpath
import re
import xml.etree.ElementTree as ET
from typing import Any, List, Optional, Sequence, Tuple

from cpp_type_utils import (
    BOOL_SCALAR_TYPES,
    FLOATING_SCALAR_TYPES,
    INT_SCALAR_TYPES,
    INT_TYPE_BOUNDS,
    clean_cpp_type as clean_cpp_type_name,
)
from naming_utils import strip_dt_prefix

ARRAY_KEYWORDS = ("tarray", "tset")
ARRAY_SPLIT_REGEX = re.compile(r"[,;|，；\n]+")
DEFAULT_FLOAT_EPSILON = 1e-6

TEXT_LITERAL_PREFIXES = ("NSLOCTEXT(", "LOCTEXT(")
TEXT_TYPE_KEYWORDS = ("ftext", "fstring")

TAG_LITERAL_PREFIX = "(TagName="
TAG_TYPE_KEYWORDS = ("gameplaytag",)
SOFT_OBJECT_TYPE_KEYWORDS = ("softobjectptr", "softclassptr")
SOFT_OBJECT_NONE_LITERAL = "None"

RESERVED_FIELD_NAMES = ("name",)

BOOL_TRUE_VALUES = frozenset({"true", "1", "yes", "y"})
BOOL_FALSE_VALUES = frozenset({"false", "0", "no", "n"})
BOOL_ALL_LITERALS = BOOL_TRUE_VALUES | BOOL_FALSE_VALUES

_NS_MAIN = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"
_NS_REL = "{http://schemas.openxmlformats.org/package/2006/relationships}"
_NS_DOC_REL = "{http://schemas.openxmlformats.org/officeDocument/2006/relationships}"


class SchemaValidationError(ValueError):
    """
    数据格式或类型校验失败异常
    """


@dataclasses.dataclass
class ColumnInfo:
    """
    数据表列结构元数据
    """
    col_idx: int
    field_name: str
    cpp_type: str
    display_name: str = ""
    is_array: bool = False
    inner_type: str = "FString"
    is_key: bool = False


def is_array_type(cpp_type: Optional[str]) -> bool:
    """
    判定是否为数组或集合类型（必须先于标量类型判定调用）
    @param cpp_type C++ 类型声明
    @return 属于 TArray / TSet 返回 True
    """
    if not cpp_type:
        return False
    t_lower = cpp_type.strip().lower()
    return any(t_lower.startswith(f"{keyword}<") for keyword in ARRAY_KEYWORDS)


def extract_inner_type(cpp_type: Optional[str]) -> str:
    """
    提取泛型容器内部元素类型（如 TArray<int32> -> int32，TSet<FName> -> FName）
    @param cpp_type 容器 C++ 类型声明
    @return 清洗后的元素类型名
    """
    if not cpp_type:
        return "FString"
    matched = re.search(r"<\s*(.+?)\s*>", cpp_type)
    if matched:
        return clean_cpp_type_name(matched.group(1).strip())
    return "FString"


def get_scalar_zero_value(cpp_type: Optional[str]) -> Any:
    """
    获取精确标量类型的默认零值（结构体类型返回空串）
    @param cpp_type C++ 类型声明
    @return 零值（False / 0.0 / 0 / ""）
    """
    t_lower = clean_cpp_type_name(cpp_type).lower()
    if t_lower in BOOL_SCALAR_TYPES:
        return False
    if t_lower in FLOATING_SCALAR_TYPES:
        return 0.0
    if t_lower in INT_SCALAR_TYPES:
        return 0
    return ""


def parse_bool_value(raw_val: Any, strict: bool = False) -> bool:
    """
    统一布尔值解析真源（大小写无关，支持 bool / 0-1 数值 / true-false-yes-no-y-n 字面量）
    @param raw_val 原始值
    @param strict 严格模式（遇到非法字面量时抛出 SchemaValidationError）
    @return 解析后的布尔值
    """
    if isinstance(raw_val, bool):
        return raw_val
    if raw_val is None or raw_val == "":
        return False
    if isinstance(raw_val, (int, float)):
        if raw_val == 1:
            return True
        if raw_val == 0:
            return False
        if strict:
            raise SchemaValidationError(f"无法将数值 {raw_val!r} 转为布尔值")
        return bool(raw_val)

    norm = str(raw_val).strip().lower()
    if norm in BOOL_TRUE_VALUES:
        return True
    if norm in BOOL_FALSE_VALUES:
        return False
    if strict:
        raise SchemaValidationError(f"无法将 {raw_val!r} 转为布尔值")
    return False


def parse_raw_array_items(raw_val: Any) -> List[Any]:
    """
    健壮解析任意书写形态的数组单元格/字段值
    支持 Python list、标准 JSON 数组、未加引号中括号数组 [a, b]、虚幻元组 (a, b) 与常用分隔符
    @param raw_val 原始单元格或 JSON 字段值
    @return 切分后的元素列表
    """
    if raw_val is None:
        return []
    if isinstance(raw_val, list):
        return list(raw_val)

    text = str(raw_val).strip()
    if not text:
        return []

    if text.startswith("[") and text.endswith("]"):
        try:
            parsed = json.loads(text)
            if isinstance(parsed, list):
                return list(parsed)
        except json.JSONDecodeError:
            text = text[1:-1].strip()

    if text.startswith("(") and text.endswith(")"):
        text = text[1:-1].strip()

    if not text:
        return []

    tokens = []
    for tok in ARRAY_SPLIT_REGEX.split(text):
        cleaned = tok.strip().strip('"').strip("'").strip()
        if cleaned:
            tokens.append(cleaned)
    return tokens


def _base_type_lower(cpp_type: Optional[str]) -> str:
    """
    提取用于类型分类判定的基础类型小写名（若为容器则提取内部元素类型）
    @param cpp_type C++ 类型声明
    @return 清洗后的基础类型小写字符串
    """
    clean = clean_cpp_type_name(cpp_type) if cpp_type else ""
    if is_array_type(clean):
        clean = extract_inner_type(clean)
    return clean.strip().lower()


def _parse_exact_int(raw_val: Any) -> int:
    """
    精确解析整数值（优先直接 int 解析以保持 64 位大整数完整精度，仅接受小数部分为 0 的浮点或十进制文本）
    @param raw_val 原始单元格或 JSON 值
    @return 解析后的 Python int
    @raises ValueError 无法解析为精确有限整数时抛出
    """
    if isinstance(raw_val, (bool, list, dict)):
        raise ValueError("布尔值、容器或对象不可作为整数解析")
    if isinstance(raw_val, int):
        return raw_val
    if isinstance(raw_val, float):
        if not math.isfinite(raw_val) or not raw_val.is_integer():
            raise ValueError(f"非精确整数浮点值不可转为整数: {raw_val!r}")
        return int(raw_val)
    text = str(raw_val).strip()
    if not text:
        raise ValueError("空字符串不可转为整数")
    try:
        return int(text)
    except ValueError:
        try:
            dec = decimal.Decimal(text)
        except decimal.InvalidOperation as err:
            raise ValueError(f"非法数值文本: {raw_val!r}") from err
        if not dec.is_finite() or dec != int(dec):
            raise ValueError(f"非精确整数数值: {raw_val!r}")
        return int(dec)


def is_text_type(cpp_type: Optional[str]) -> bool:
    """
    判定是否为文本类列（FText / FString），反向形态归一只作用于文本列
    @param cpp_type C++ 类型声明
    @return 属于文本列返回 True
    """
    return _base_type_lower(cpp_type) in TEXT_TYPE_KEYWORDS


def _split_literal_args(inner: str) -> List[str]:
    """
    按顶层逗号切分文本字面量参数（尊重字符串引号与括号嵌套，支持 \\" 转义）
    @param inner 字面量括号内的原始内容
    @return 参数列表（已去除两端空白）
    """
    args: List[str] = []
    buf: List[str] = []
    in_string = False
    escaped = False
    depth = 0

    for ch in inner:
        if escaped:
            buf.append(ch)
            escaped = False
            continue
        if ch == "\\":
            buf.append(ch)
            escaped = True
            continue
        if ch == '"':
            in_string = not in_string
            buf.append(ch)
            continue
        if not in_string:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth = max(depth - 1, 0)
            elif ch == "," and depth == 0:
                args.append("".join(buf).strip())
                buf = []
                continue
        buf.append(ch)

    args.append("".join(buf).strip())
    return args


def _unquote_ue_string(arg: str) -> Optional[str]:
    """
    取出 UE 字符串字面量的显示文本并反转义（\\" \\\\ \\n \\t \\r）
    @param arg 形如 "文本" 的参数字面量
    @return 显示文本；不是合法字符串字面量时返回 None
    """
    text = arg.strip()
    if len(text) < 2 or not text.startswith('"') or not text.endswith('"'):
        return None

    body = text[1:-1]
    result: List[str] = []
    escaped = False
    for ch in body:
        if escaped:
            result.append({"n": "\n", "t": "\t", "r": "\r"}.get(ch, ch))
            escaped = False
            continue
        if ch == "\\":
            escaped = True
            continue
        result.append(ch)
    if escaped:
        result.append("\\")
    return "".join(result)


def unwrap_text_literal(raw_val: Any) -> Any:
    """
    把 UE 文本字面量还原为显示文本（仅处理完整的 NSLOCTEXT/LOCTEXT 调用，其余原样返回）
    @param raw_val 原始值（通常是 JSON 侧 FText 字段值）
    @return 显示文本；非字面量形态或解析失败时原样返回
    """
    if raw_val is None or isinstance(raw_val, (list, dict)):
        return raw_val

    text = str(raw_val).strip()
    prefix = next((p for p in TEXT_LITERAL_PREFIXES if text.startswith(p)), None)
    if prefix is None or not text.endswith(")"):
        return raw_val

    args = _split_literal_args(text[len(prefix):-1])
    required_args = 3 if prefix == "NSLOCTEXT(" else 2
    if len(args) != required_args:
        return raw_val

    literal = _unquote_ue_string(args[-1])
    return raw_val if literal is None else literal


def is_tag_type(cpp_type: Optional[str]) -> bool:
    """
    判定是否为 FGameplayTag 列
    @param cpp_type C++ 类型声明
    @return 属于 FGameplayTag 列返回 True
    """
    return _base_type_lower(cpp_type) in ("fgameplaytag", "fgameplaytagcontainer")


def is_soft_object_type(cpp_type: Optional[str]) -> bool:
    """
    判定是否为软对象引用列（TSoftObjectPtr / TSoftClassPtr）
    @param cpp_type C++ 类型声明
    @return 属于软引用列返回 True
    """
    base = _base_type_lower(cpp_type)
    return base.startswith(("tsoftobjectptr<", "tsoftclassptr<"))


def unwrap_tag_literal(raw_val: Any) -> Any:
    """
    把 UE 标签字面量还原为标签名（(TagName="A.B") 与 {"TagName": "A.B"} 两种形态）
    @param raw_val 原始值
    @return 标签名；非字面量形态返回原值（空标签 (TagName="") 归一为空串）
    """
    if raw_val is None:
        return raw_val

    if isinstance(raw_val, dict):
        tag_name = raw_val.get("TagName")
        return tag_name if isinstance(tag_name, str) else raw_val

    if isinstance(raw_val, list):
        return raw_val

    text = str(raw_val).strip()
    if not text.startswith(TAG_LITERAL_PREFIX) or not text.endswith(")"):
        return raw_val

    inner = text[len(TAG_LITERAL_PREFIX):-1].strip()
    tag_name = _unquote_ue_string(inner)
    return raw_val if tag_name is None else tag_name


def unwrap_soft_object_value(raw_val: Any) -> Any:
    """
    把 UE 侧软对象“空引用”字面量（字符串 None）归一为空值
    @param raw_val 原始值
    @return 归一后值（None → 空串；其余原样）
    """
    if isinstance(raw_val, str) and raw_val.strip() == SOFT_OBJECT_NONE_LITERAL:
        return ""
    return raw_val


def normalize_value_form(raw_val: Any, cpp_type: Optional[str]) -> Any:
    """
    反向入参形态归一（仅针对 UE 侧导出形态，不改变 Excel 侧可读值语义）
    @param raw_val 原始值
    @param cpp_type C++ 类型声明
    @return 归一后的值
    """
    if raw_val is None or isinstance(raw_val, list):
        return raw_val

    clean_type = clean_cpp_type_name(cpp_type)
    if is_text_type(clean_type):
        return unwrap_text_literal(raw_val)
    if is_tag_type(clean_type):
        return unwrap_tag_literal(raw_val)
    if is_soft_object_type(clean_type):
        return unwrap_soft_object_value(raw_val)
    return raw_val


def convert_scalar_value(raw_val: Any, cpp_type: Optional[str], strict: bool = False) -> Any:
    """
    精确标量值类型强转（FIntPoint / FIntVector 等结构体保持字符串原样）
    @param raw_val 原始值
    @param cpp_type C++ 类型声明
    @param strict 严格模式（失败抛 SchemaValidationError，非严格回落默认值）
    @return 强转后的标量值
    """
    t_lower = clean_cpp_type_name(cpp_type).lower()

    if isinstance(raw_val, (list, dict)):
        if strict:
            raise SchemaValidationError(f"标量字段 ({cpp_type}) 不接受容器或对象值: {raw_val!r}")
        return get_scalar_zero_value(cpp_type)

    if t_lower in FLOATING_SCALAR_TYPES:
        if raw_val is None or raw_val == "":
            return 0.0
        if isinstance(raw_val, bool):
            if strict:
                raise SchemaValidationError(f"无法将布尔值 {raw_val!r} 转为浮点数")
            return 0.0
        try:
            parsed_float = float(raw_val)
        except (ValueError, TypeError, OverflowError) as e:
            if strict:
                raise SchemaValidationError(f"无法将 {raw_val!r} 转为浮点数: {e}") from e
            return 0.0
        if not math.isfinite(parsed_float):
            if strict:
                raise SchemaValidationError(f"浮点数不允许为 NaN 或 Inf: {raw_val!r}")
            return 0.0
        return parsed_float

    if t_lower in INT_SCALAR_TYPES:
        if raw_val is None or raw_val == "":
            return 0
        try:
            parsed_int = _parse_exact_int(raw_val)
        except (ValueError, TypeError, OverflowError) as e:
            if strict:
                raise SchemaValidationError(f"无法将 {raw_val!r} 转为整型: {e}") from e
            return 0
        bounds = INT_TYPE_BOUNDS.get(t_lower)
        if bounds is not None and not (bounds[0] <= parsed_int <= bounds[1]):
            if strict:
                raise SchemaValidationError(
                    f"整型值 {parsed_int} 超出 {cpp_type} 范围 [{bounds[0]}, {bounds[1]}]"
                )
            return max(bounds[0], min(bounds[1], parsed_int))
        return parsed_int

    if t_lower in BOOL_SCALAR_TYPES:
        return parse_bool_value(raw_val, strict=strict)

    if raw_val is None:
        return ""
    return str(raw_val).strip()


def convert_for_json(
    raw_val: Any,
    cpp_type: Optional[str],
    strict: bool = False,
    nested_struct_types: Optional[dict] = None,
) -> Any:
    """
    转换 Excel 单元格原始值为标准的 JSON 对象属性值（正向导出使用）
    @param raw_val 原始值
    @param cpp_type C++ 类型声明
    @param strict 严格模式（失败抛 SchemaValidationError，非严格回落原值）
    @param nested_struct_types 配置声明的嵌套 USTRUCT 类型映射（类型名 -> include 相对路径）
    @return JSON 可序列化值（嵌套 USTRUCT 输出 JSON 对象）
    """
    clean_type = clean_cpp_type_name(cpp_type)
    if nested_struct_types and clean_type in nested_struct_types:
        if raw_val is None or raw_val == "":
            return {}
        if isinstance(raw_val, dict):
            return raw_val
        if isinstance(raw_val, str):
            text = raw_val.strip()
            if not text:
                return {}
            if text.startswith("{"):
                try:
                    parsed = json.loads(text)
                    if isinstance(parsed, dict):
                        return parsed
                except json.JSONDecodeError as e:
                    if strict:
                        raise SchemaValidationError(f"嵌套结构 {clean_type} 的 JSON 文本非法: {e}") from e
                    return raw_val
            if strict:
                raise SchemaValidationError(f"嵌套结构 {clean_type} 必须为 JSON 对象: {raw_val!r}")
            return raw_val
        if strict:
            raise SchemaValidationError(f"嵌套结构 {clean_type} 必须为 JSON 对象: {raw_val!r}")
        return raw_val

    if is_array_type(clean_type):
        if raw_val is None or raw_val == "":
            return []
        inner_type = extract_inner_type(clean_type)
        items = parse_raw_array_items(raw_val)
        return [convert_scalar_value(item, inner_type, strict=strict) for item in items]

    return convert_scalar_value(raw_val, clean_type, strict=strict)


def convert_for_excel(
    raw_val: Any,
    cpp_type: Optional[str],
    fill_defaults: bool = False,
    strict: bool = False,
) -> Any:
    """
    转换 JSON 字段值为适合写入 Excel 单元格的原生值（反向回写使用）
    @param raw_val JSON 字段原始值
    @param cpp_type C++ 类型声明
    @param fill_defaults 为 True 时空值回填 0/False/""；为 False 时空值保持 None/""
    @param strict 严格模式（非法标量抛 SchemaValidationError）
    @return 适合写入 Excel 的单元格值
    """
    clean_type = clean_cpp_type_name(cpp_type)
    raw_val = normalize_value_form(raw_val, clean_type)

    if raw_val is None or raw_val == "":
        if not fill_defaults:
            return "" if is_array_type(clean_type) else None
        return "" if is_array_type(clean_type) else get_scalar_zero_value(clean_type)

    if is_array_type(clean_type):
        inner_type = extract_inner_type(clean_type)
        items = parse_raw_array_items(raw_val)
        converted_items = [convert_scalar_value(normalize_value_form(item, inner_type), inner_type, strict=strict) for item in items]
        return json.dumps(converted_items, ensure_ascii=False)

    if isinstance(raw_val, dict):
        t_lower = clean_type.lower()
        if (
            t_lower in FLOATING_SCALAR_TYPES
            or t_lower in INT_SCALAR_TYPES
            or t_lower in BOOL_SCALAR_TYPES
            or t_lower in ("fstring", "fname", "ftext")
        ):
            if strict:
                raise SchemaValidationError(f"标量字段 ({clean_type}) 不接受 JSON 对象: {raw_val!r}")
            return get_scalar_zero_value(clean_type) if fill_defaults else None
        return json.dumps(raw_val, ensure_ascii=False)

    return convert_scalar_value(raw_val, clean_type, strict=strict)


def scalar_values_equal(v1: Any, v2: Any, cpp_type: Optional[str], epsilon: float = DEFAULT_FLOAT_EPSILON) -> bool:
    """
    标量值语义等价判定（支持浮点容差、整型精确比较、统一布尔归一、嵌套对象比较、字符串去空白）
    @param v1 值 1
    @param v2 值 2
    @param cpp_type C++ 类型声明
    @param epsilon 浮点比较容差
    @return 语义等价返回 True
    """
    v1 = normalize_value_form(v1, cpp_type)
    v2 = normalize_value_form(v2, cpp_type)

    if v1 is None and (v2 == "" or v2 is None):
        return True
    if v2 is None and (v1 == "" or v1 is None):
        return True

    t_lower = clean_cpp_type_name(cpp_type).lower()

    if t_lower in FLOATING_SCALAR_TYPES:
        try:
            f1 = float(v1)
            f2 = float(v2)
            if math.isfinite(f1) and math.isfinite(f2):
                return abs(f1 - f2) < epsilon
            return False
        except (ValueError, TypeError, OverflowError):
            return str(v1).strip() == str(v2).strip()

    if t_lower in INT_SCALAR_TYPES:
        try:
            return _parse_exact_int(v1) == _parse_exact_int(v2)
        except (ValueError, TypeError, OverflowError):
            return str(v1).strip() == str(v2).strip()

    if t_lower in BOOL_SCALAR_TYPES:
        return parse_bool_value(v1) == parse_bool_value(v2)

    if isinstance(v1, dict) or isinstance(v2, dict):
        try:
            d1 = v1 if isinstance(v1, dict) else json.loads(str(v1))
            d2 = v2 if isinstance(v2, dict) else json.loads(str(v2))
            if isinstance(d1, dict) and isinstance(d2, dict):
                return d1 == d2
        except (ValueError, TypeError, json.JSONDecodeError):
            pass

    return str(v1).strip() == str(v2).strip()


def values_are_equal(v1: Any, v2: Any, cpp_type: Optional[str], epsilon: float = DEFAULT_FLOAT_EPSILON) -> bool:
    """
    语义对拍函数（单一真源）：数组按元素序列等价互认，标量委托 scalar_values_equal
    @param v1 值 1
    @param v2 值 2
    @param cpp_type C++ 类型声明
    @param epsilon 浮点比较容差
    @return 语义等价返回 True
    """
    if v1 is None and (v2 == "" or v2 is None):
        return True
    if v2 is None and (v1 == "" or v1 is None):
        return True

    clean_type = clean_cpp_type_name(cpp_type)

    if is_array_type(clean_type):
        inner_type = extract_inner_type(clean_type)
        items1 = parse_raw_array_items(v1)
        items2 = parse_raw_array_items(v2)
        if len(items1) != len(items2):
            return False
        return all(scalar_values_equal(a, b, inner_type, epsilon) for a, b in zip(items1, items2))

    return scalar_values_equal(v1, v2, clean_type, epsilon)


def find_reserved_field_names(header_fields: Optional[Sequence[Any]]) -> List[Tuple[int, str]]:
    """
    检出表头字段行中使用的禁用字段名（硬校验，放行标准主键 Name/RowName，其余大小写变体如 name/NAME 均命中阻断）
    @param header_fields 表头第 2 行字段名序列
    @return [(1-based 列号, 字段名)]；无命中返回空列表
    """
    hits: List[Tuple[int, str]] = []
    for idx, field in enumerate(header_fields or [], start=1):
        name = str(field or "").strip()
        if name.lower() in RESERVED_FIELD_NAMES and name != "Name":
            hits.append((idx, name))
    return hits


def parse_header_columns(row_display: List[Any], row_fields: List[Any], row_types: List[Any]) -> List[ColumnInfo]:
    """
    解析表头前三行契约，产出列定义清单
    @param row_display 第 1 行中文说明
    @param row_fields 第 2 行字段名
    @param row_types 第 3 行 C++ 类型
    @return ColumnInfo 列表
    """
    max_len = max(len(row_display), len(row_fields), len(row_types))
    columns: List[ColumnInfo] = []

    for idx in range(max_len):
        field_name = str(row_fields[idx]).strip() if idx < len(row_fields) and row_fields[idx] is not None else ""
        if not field_name:
            continue

        raw_type = str(row_types[idx]).strip() if idx < len(row_types) and row_types[idx] is not None else "FString"
        display_name = str(row_display[idx]).strip() if idx < len(row_display) and row_display[idx] is not None else ""

        clean_type = clean_cpp_type_name(raw_type)
        arr = is_array_type(clean_type)
        inner = extract_inner_type(clean_type) if arr else clean_type
        is_key = field_name in ("RowName", "Name")

        columns.append(ColumnInfo(
            col_idx=idx,
            field_name=field_name,
            cpp_type=clean_type,
            display_name=display_name,
            is_array=arr,
            inner_type=inner,
            is_key=is_key,
        ))

    return columns


def resolve_openpyxl_sheet(wb: Any, table_name: str) -> Tuple[Any, str]:
    """
    统一解析 openpyxl 工作簿中的目标数据工作表（与 zipfile 选表规则完全对称）
    1. 与 table_name（或其剥离 DT_ 前缀后名称）精确匹配；
    2. 含有效 RowName / Name 第 2 行契约的首张工作表；
    3. 回落 wb.active。
    @param wb openpyxl Workbook 对象
    @param table_name 目标表名或文件名词干
    @return (worksheet, sheet_name)
    """
    target_clean = strip_dt_prefix(table_name)

    for name in wb.sheetnames:
        if name == table_name or strip_dt_prefix(name) == target_clean:
            return wb[name], name

    for name in wb.sheetnames:
        sheet = wb[name]
        if sheet.max_row >= 3 and sheet.max_column >= 1:
            row2_values = [str(sheet.cell(row=2, column=c).value or "").strip() for c in range(1, min(sheet.max_column + 1, 10))]
            if "RowName" in row2_values or "Name" in row2_values:
                return sheet, name

    return wb.active, wb.active.title


def read_zip_shared_strings(z: Any, strict_xml: bool = False) -> List[str]:
    """
    读取 xlsx 压缩包中的共享字符串池（table_schema 与 excel_parser 共用单一真源）
    @param z zipfile.ZipFile 对象
    @param strict_xml 为 True 时遇到损坏 XML 直接抛出 ET.ParseError
    @return 字符串列表
    """
    if "xl/sharedStrings.xml" not in z.namelist():
        return []
    try:
        tree = ET.fromstring(z.read("xl/sharedStrings.xml"))
    except ET.ParseError:
        if strict_xml:
            raise
        return []
    strings: List[str] = []
    for si in tree.findall(f"{_NS_MAIN}si"):
        strings.append("".join(t.text for t in si.iter(f"{_NS_MAIN}t") if t.text))
    return strings


_read_zip_shared_strings = read_zip_shared_strings


def _zip_sheet_has_key_header(z: Any, xml_path: str, shared_strings: Sequence[str]) -> bool:
    """
    检测 zipfile 内指定工作表第 2 行是否包含 RowName / Name 主键契约
    @param z zipfile.ZipFile 对象
    @param xml_path 工作表 XML 路径
    @param shared_strings 共享字符串池
    @return 含主键契约返回 True
    """
    try:
        sheet_tree = ET.fromstring(z.read(xml_path))
    except (KeyError, ET.ParseError):
        return False

    rows = sheet_tree.findall(f".//{_NS_MAIN}row")
    if len(rows) < 2:
        return False

    has_r_attr = any(r.get("r", "").isdigit() for r in rows)
    if has_r_attr:
        row2 = next((r for r in rows if r.get("r") == "2"), None)
        has_row3 = any(r.get("r") == "3" for r in rows)
        if row2 is None or not has_row3:
            return False
    else:
        if len(rows) < 3:
            return False
        row2 = rows[1]

    for seq_idx, cell in enumerate(row2.findall(f"{_NS_MAIN}c")):
        ref = cell.get("r", "")
        col_idx = seq_idx
        if ref:
            m = re.match(r"([A-Za-z]+)\d+", ref)
            if m:
                col_idx = 0
                for ch in m.group(1):
                    col_idx = col_idx * 26 + (ord(ch.upper()) - ord("A") + 1)
                col_idx -= 1
        if col_idx >= 9:
            continue
        cell_type = cell.get("t")
        value = ""
        if cell_type == "inlineStr":
            is_el = cell.find(f"{_NS_MAIN}is")
            if is_el is not None:
                value = "".join(x.text or "" for x in is_el.iter(f"{_NS_MAIN}t"))
        else:
            v_el = cell.find(f"{_NS_MAIN}v")
            raw = v_el.text if v_el is not None and v_el.text is not None else ""
            if cell_type == "s" and raw.isdigit():
                idx = int(raw)
                value = shared_strings[idx] if 0 <= idx < len(shared_strings) else ""
            else:
                value = raw
        if value.strip() in ("RowName", "Name"):
            return True
    return False


def resolve_zip_worksheet(
    z: Any,
    table_name: str,
    shared_strings: Optional[Sequence[str]] = None,
) -> Tuple[str, str]:
    """
    在 zipfile 中定位目标工作表的 (sheet_name, xml_path)（与 resolve_openpyxl_sheet 三级策略完全对称）
    @param z zipfile.ZipFile 对象
    @param table_name 目标表名或文件名词干
    @param shared_strings 可选已解析的共享字符串池（避免重复解析）
    @return (选中的工作表名, 工作表 XML 路径)
    """
    fallback = ("", "xl/worksheets/sheet1.xml")
    names = set(z.namelist())
    if "xl/workbook.xml" not in names or "xl/_rels/workbook.xml.rels" not in names:
        return fallback

    rid_to_target: dict[str, str] = {}
    try:
        rels_tree = ET.fromstring(z.read("xl/_rels/workbook.xml.rels"))
        for rel in rels_tree.findall(f".//{_NS_REL}Relationship"):
            r_id = rel.attrib.get("Id")
            target = rel.attrib.get("Target", "").lstrip("/")
            if r_id and target:
                raw_target = target if target.startswith("xl/") else f"xl/{target}"
                rid_to_target[r_id] = posixpath.normpath(raw_target)
    except ET.ParseError:
        return fallback

    try:
        wb_tree = ET.fromstring(z.read("xl/workbook.xml"))
    except ET.ParseError:
        return fallback

    candidates: List[Tuple[str, str]] = []
    for sheet in wb_tree.findall(f".//{_NS_MAIN}sheet"):
        sname = sheet.attrib.get("name", "")
        rid = sheet.attrib.get(f"{_NS_DOC_REL}id")
        target_path = rid_to_target.get(rid or "")
        if target_path and target_path in names:
            candidates.append((sname, target_path))

    if not candidates:
        return fallback

    target_clean = strip_dt_prefix(table_name)
    for sname, target_path in candidates:
        if sname == table_name or strip_dt_prefix(sname) == target_clean:
            return sname, target_path

    strings = shared_strings if shared_strings is not None else _read_zip_shared_strings(z)
    for sname, target_path in candidates:
        if _zip_sheet_has_key_header(z, target_path, strings):
            return sname, target_path

    view = wb_tree.find(f".//{_NS_MAIN}workbookView")
    if view is not None:
        try:
            active_idx = int(view.attrib.get("activeTab", "0"))
            if 0 <= active_idx < len(candidates):
                return candidates[active_idx]
        except ValueError:
            pass

    return candidates[0]


def resolve_zip_worksheet_path(z: Any, table_name: str) -> str:
    """
    在 zipfile 中定位目标表格对应的 worksheets/sheetX.xml 路径（委托至 resolve_zip_worksheet）
    @param z zipfile.ZipFile 对象
    @param table_name 目标表名或文件名词干
    @return 工作表 XML 路径
    """
    return resolve_zip_worksheet(z, table_name)[1]
