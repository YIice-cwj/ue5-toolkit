#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
数据表架构契约与编解码单一真源 (table_schema.py) —— SSOT (Single Source of Truth)

核心职责：
1. 三行表头契约解析（Row 1 中文说明 / Row 2 字段名 / Row 3 C++类型）；
2. 跨工具统一的工作表定位策略（Sheet Resolution），彻底消除 sheet1.xml vs wb.active 选表漂移；
3. 强类型系统与双向编解码唯一实现：
   - 数组/集合类型严格优先于标量判定；
   - 健壮解析：支持标准 JSON 数组、未加引号中括号 [a, b]、元组 (a, b) 与分隔符切分，消灭脏 token；
   - 输出统一规范：反向写出标准 JSON 数组语法；
   - 语义对拍：浮点容差参数化、整型取整、布尔归一、数组元素序列等价；
4. 空值语义保护：区分“有意留空”与“类型零值”，防止覆写破坏 Excel 留空单元格；
5. 内存前置数据校验：在落盘前完成结构与类型合规性自检。
"""

import dataclasses
import json
import re
from typing import Any, List, Optional, Sequence, Tuple

from cpp_type_utils import clean_cpp_type as clean_cpp_type_name

# 数组 / 集合类型关键词
ARRAY_KEYWORDS = ("tarray", "tset")

# 数组常用分隔符正则（中英文逗号、分号、管道符、换行）
ARRAY_SPLIT_REGEX = re.compile(r"[,;|，；\n]+")

# 默认浮点比较容差
DEFAULT_FLOAT_EPSILON = 1e-6

# UE 侧文本字面量前缀（DataTable 导出物形态：FText 列可能是 NSLOCTEXT/LOCTEXT 调用而非显示文本）
TEXT_LITERAL_PREFIXES = ("NSLOCTEXT(", "LOCTEXT(")

# 文本类 C++ 类型关键字（反向形态归一只作用于文本列）
TEXT_TYPE_KEYWORDS = ("ftext", "fstring")

# UE 侧标签字面量前缀（FGameplayTag 列在导出物中形如 (TagName="A.B")）
TAG_LITERAL_PREFIX = "(TagName="

# 标签列 / 软对象列类型关键字
TAG_TYPE_KEYWORDS = ("gameplaytag",)
SOFT_OBJECT_TYPE_KEYWORDS = ("softobjectptr", "softclassptr")

# UE 侧软对象“空引用”字面量（导出物中以字符串 None 表示未设置资产）
SOFT_OBJECT_NONE_LITERAL = "None"

# 禁用字段名（硬校验：与 UE/DataTable 约定冲突，禁止作为表头字段名出现；精确匹配，不含大写 Name 键别名）
RESERVED_FIELD_NAMES = ("name",)


class SchemaValidationError(ValueError):
    """数据格式或类型校验失败异常"""


@dataclasses.dataclass
class ColumnInfo:
    """列结构元数据"""
    col_idx: int                 # 0-indexed 列下标
    field_name: str              # Row 2 字段名 (如 RowName, Health)
    cpp_type: str                # Row 3 C++ 类型声明 (如 int32, TArray<FString>)
    display_name: str = ""       # Row 1 中文说明
    is_array: bool = False       # 是否为数组/集合
    inner_type: str = "FString"  # 数组元素类型或标量类型
    is_key: bool = False         # 是否为主键 (RowName / Name)


def is_array_type(cpp_type: Optional[str]) -> bool:
    """判定是否为数组或集合类型（必须先于标量类型判定调用）"""
    if not cpp_type:
        return False
    t_lower = cpp_type.lower()
    return any(keyword in t_lower for keyword in ARRAY_KEYWORDS)


def extract_inner_type(cpp_type: Optional[str]) -> str:
    """提取泛型容器内部元素类型（如 TArray<int32> -> int32，TSet<FName> -> FName）"""
    if not cpp_type:
        return "FString"
    matched = re.search(r"<\s*(.+?)\s*>", cpp_type)
    if matched:
        return clean_cpp_type_name(matched.group(1).strip())
    return "FString"


def get_scalar_zero_value(cpp_type: Optional[str]) -> Any:
    """获取标量类型的默认零值"""
    t_lower = (cpp_type or "").lower()
    if "bool" in t_lower:
        return False
    if "float" in t_lower or "double" in t_lower:
        return 0.0
    if "int" in t_lower or "uint" in t_lower or "byte" in t_lower:
        return 0
    return ""


def parse_raw_array_items(raw_val: Any) -> List[Any]:
    """
    健壮解析任意书写形态的数组单元格/字段值
    支持：
    1. Python list / 原生容器
    2. 标准 JSON 数组：["a", "b"]、[1, 2]
    3. 未加引号的中括号数组：[a, b]（彻底修复切出 '[a' 和 'b]' 的脏 token 问题）
    4. 虚幻元组语法：("a", "b")
    5. 策划常用分隔符：a, b, c / a；b；c
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


def is_text_type(cpp_type: Optional[str]) -> bool:
    """判定是否为文本类列（FText / FString），反向形态归一只作用于文本列"""
    t_lower = (cpp_type or "").lower()
    return any(keyword in t_lower for keyword in TEXT_TYPE_KEYWORDS)


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

    实现思路：仅当整串构成完整调用（以 NSLOCTEXT(/LOCTEXT( 开头、以 ) 结尾）才归一；
    NSLOCTEXT 取第 3 个参数、LOCTEXT 取第 2 个参数作为显示文本，避免误伤文本中恰好含该字样的普通字符串
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
    """判定是否为 FGameplayTag 列"""
    t_lower = (cpp_type or "").lower()
    return any(keyword in t_lower for keyword in TAG_TYPE_KEYWORDS)


def is_soft_object_type(cpp_type: Optional[str]) -> bool:
    """判定是否为软对象引用列（TSoftObjectPtr / TSoftClassPtr）"""
    t_lower = (cpp_type or "").lower()
    return any(keyword in t_lower for keyword in SOFT_OBJECT_TYPE_KEYWORDS)


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
    - FText / FString 列：NSLOCTEXT / LOCTEXT 字面量 → 显示文本
    - FGameplayTag 列：(TagName="X") 字面量 → 标签名 X
    - 软对象列：字符串 None → 空值
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
    标量值类型强转
    @param raw_val 原始值
    @param cpp_type C++ 类型声明
    @param strict 严格模式（失败抛 SchemaValidationError，非严格回落默认值）
    """
    t_lower = (cpp_type or "").lower()

    if "float" in t_lower or "double" in t_lower:
        if raw_val is None or raw_val == "":
            return 0.0
        try:
            return float(raw_val)
        except (ValueError, TypeError) as e:
            if strict:
                raise SchemaValidationError(f"无法将 {raw_val!r} 转为浮点数: {e}")
            return 0.0

    if "int" in t_lower or "uint" in t_lower or "byte" in t_lower:
        if raw_val is None or raw_val == "":
            return 0
        try:
            return int(round(float(raw_val)))
        except (ValueError, TypeError) as e:
            if strict:
                raise SchemaValidationError(f"无法将 {raw_val!r} 转为整型: {e}")
            return 0

    if "bool" in t_lower:
        if isinstance(raw_val, bool):
            return raw_val
        if raw_val is None or raw_val == "":
            return False
        return str(raw_val).strip().upper() in ("TRUE", "1", "YES", "Y")

    if raw_val is None:
        return ""
    return str(raw_val).strip()


def convert_for_json(raw_val: Any, cpp_type: Optional[str], strict: bool = False,
                     nested_struct_types: Optional[dict] = None) -> Any:
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
        if isinstance(raw_val, dict):
            return raw_val
        if isinstance(raw_val, str):
            text = raw_val.strip()
            if text.startswith("{") or text.startswith("["):
                try:
                    return json.loads(text)
                except json.JSONDecodeError as e:
                    if strict:
                        raise SchemaValidationError(f"嵌套结构 {clean_type} 的 JSON 文本非法: {e}")
                    return raw_val
        return raw_val

    if is_array_type(clean_type):
        if raw_val is None or raw_val == "":
            return []
        inner_type = extract_inner_type(clean_type)
        items = parse_raw_array_items(raw_val)
        return [convert_scalar_value(item, inner_type, strict=strict) for item in items]

    return convert_scalar_value(raw_val, clean_type, strict=strict)


def convert_for_excel(raw_val: Any, cpp_type: Optional[str], fill_defaults: bool = False, strict: bool = False) -> Any:
    """
    转换 JSON 字段值为适合写入 Excel 单元格的原生值（反向回写使用）
    @param fill_defaults 为 True 时空值回填 0/False/""；为 False 时空值保持 None/""（保护 Excel 留空）

    实现思路：先归一 UE 导出形态（NSLOCTEXT / (TagName="X") / 软对象 None）为 Excel 可读值，
    空值按 fill_defaults 决定回填零值或保持留空，数组类型输出标准 JSON 数组文本。
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
        converted_items = [convert_scalar_value(item, inner_type, strict=strict) for item in items]
        return json.dumps(converted_items, ensure_ascii=False)

    return convert_scalar_value(raw_val, clean_type, strict=strict)


def scalar_values_equal(v1: Any, v2: Any, cpp_type: Optional[str], epsilon: float = DEFAULT_FLOAT_EPSILON) -> bool:
    """标量值语义等价判定（支持浮点容差、整型四舍五入、布尔归一、字符串去空白）

    实现思路：先做形态归一（UE 导出形态与 Excel 可读形态视为等价，避免无意义重写），再按类型比较。
    """
    v1 = normalize_value_form(v1, cpp_type)
    v2 = normalize_value_form(v2, cpp_type)

    if v1 is None and (v2 == "" or v2 is None):
        return True
    if v2 is None and (v1 == "" or v1 is None):
        return True

    t_lower = (cpp_type or "").lower()

    if "float" in t_lower or "double" in t_lower:
        try:
            return abs(float(v1) - float(v2)) < epsilon
        except (ValueError, TypeError):
            return str(v1).strip() == str(v2).strip()

    if "int" in t_lower or "uint" in t_lower or "byte" in t_lower:
        try:
            return int(round(float(v1))) == int(round(float(v2)))
        except (ValueError, TypeError):
            return str(v1).strip() == str(v2).strip()

    if "bool" in t_lower:
        b1 = v1 in (True, 1, "1", "TRUE", "True", "yes", "YES")
        b2 = v2 in (True, 1, "1", "TRUE", "True", "yes", "YES")
        return b1 == b2

    return str(v1).strip() == str(v2).strip()


def values_are_equal(v1: Any, v2: Any, cpp_type: Optional[str], epsilon: float = DEFAULT_FLOAT_EPSILON) -> bool:
    """
    语义对拍函数（单一真源）：
    - 数组：元素序列等价互认（'a, b' 与 '["a", "b"]' 视为相等，消除样式漂移与无意义 diff）
    - 标量：调用 scalar_values_equal
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
    检出表头字段行中使用的禁用字段名（硬校验，命中即应阻断）
    @param header_fields 表头第 2 行字段名序列
    @return [(1-based 列号, 字段名)]；无命中返回空列表

    实现思路：按**精确匹配**判定，避免误伤项目既有的 `Name`（RowName 列的键别名约定）；
    `name` 与 UE/DataTable 约定冲突，禁止作为字段名出现
    """
    hits: List[Tuple[int, str]] = []
    for idx, field in enumerate(header_fields or [], start=1):
        name = str(field or "").strip()
        if name in RESERVED_FIELD_NAMES:
            hits.append((idx, name))
    return hits


def parse_header_columns(row_display: List[Any], row_fields: List[Any], row_types: List[Any]) -> List[ColumnInfo]:
    """
    解析表头前三行契约，产出列定义清单
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
        is_key = (field_name == "RowName" or field_name == "Name")

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
    统一解析 openpyxl 工作簿中的目标数据工作表（彻底消除 wb.active 盲猜）
    规则：
    1. 寻找与 table_name（剥离 DT_ 前缀后）匹配的 Sheet；
    2. 若未匹配，寻找包含有效 RowName/Name 契约的第一张工作表；
    3. 若仍无，回落 wb.active 并返回
    """
    target_clean = table_name.replace("DT_", "").strip()

    for name in wb.sheetnames:
        clean_sname = name.replace("DT_", "").strip()
        if clean_sname == target_clean or name == table_name:
            return wb[name], name

    for name in wb.sheetnames:
        sheet = wb[name]
        if sheet.max_row >= 3 and sheet.max_column >= 1:
            row2_values = [str(sheet.cell(row=2, column=c).value or "").strip() for c in range(1, min(sheet.max_column + 1, 10))]
            if "RowName" in row2_values or "Name" in row2_values:
                return sheet, name

    return wb.active, wb.active.title


def resolve_zip_worksheet_path(z: Any, table_name: str) -> str:
    """
    在 zipfile 中定位目标表格对应的 worksheets/sheetX.xml 路径（与 openpyxl 选表规则完全对称）
    """
    import xml.etree.ElementTree as ET

    fallback = "xl/worksheets/sheet1.xml"
    if "xl/workbook.xml" not in z.namelist() or "xl/_rels/workbook.xml.rels" not in z.namelist():
        return fallback

    rid_to_target = {}
    try:
        rels_tree = ET.fromstring(z.read("xl/_rels/workbook.xml.rels"))
        for rel in rels_tree.findall(".//{http://schemas.openxmlformats.org/package/2006/relationships}Relationship"):
            r_id = rel.attrib.get("Id")
            target = rel.attrib.get("Target", "")
            if r_id and target:
                target_path = target if target.startswith("xl/") else f"xl/{target}"
                rid_to_target[r_id] = target_path
    except ET.ParseError:
        return fallback

    try:
        wb_tree = ET.fromstring(z.read("xl/workbook.xml"))
        target_clean = table_name.replace("DT_", "").strip()

        first_target = None
        for sheet in wb_tree.findall(".//{http://schemas.openxmlformats.org/spreadsheetml/2006/main}sheet"):
            sname = sheet.attrib.get("name", "")
            rid = sheet.attrib.get("{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id")
            target_path = rid_to_target.get(rid)
            if not target_path or target_path not in z.namelist():
                continue

            if first_target is None:
                first_target = target_path

            if sname == table_name or sname.replace("DT_", "").strip() == target_clean:
                return target_path

        return first_target or fallback
    except ET.ParseError:
        return fallback
