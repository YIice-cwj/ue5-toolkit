#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
C++ 类型工具模块 (cpp_type_utils.py)

- clean_cpp_type: 清洗表头类型签名（去除跨表外键注解语法）
- get_default_init_expr: 根据精确 C++ 标量类型生成声明处初始化代码
- payload_mode: 统一的字段序列化方式判定（RowStruct 生成与序列化生成共用同一白名单）
- validate_field_type: 生成期字段类型校验（不支持类型直接阻断管线）
"""

import re
from typing import Any, Mapping, Optional, Set

FLOAT_SCALAR_TYPES = frozenset({"float"})
DOUBLE_SCALAR_TYPES = frozenset({"double"})
FLOATING_SCALAR_TYPES = FLOAT_SCALAR_TYPES | DOUBLE_SCALAR_TYPES
SIGNED_INT_SCALAR_TYPES = frozenset({"int8", "int16", "int32", "int64", "int"})
UNSIGNED_INT_SCALAR_TYPES = frozenset({"uint8", "uint16", "uint32", "uint64", "byte"})
INT_SCALAR_TYPES = SIGNED_INT_SCALAR_TYPES | UNSIGNED_INT_SCALAR_TYPES
BOOL_SCALAR_TYPES = frozenset({"bool"})

INT_TYPE_BOUNDS = {
    "int8": (-128, 127),
    "int16": (-32768, 32767),
    "int32": (-2147483648, 2147483647),
    "int64": (-9223372036854775808, 9223372036854775807),
    "int": (-2147483648, 2147483647),
    "uint8": (0, 255),
    "uint16": (0, 65535),
    "uint32": (0, 4294967295),
    "uint64": (0, 18446744073709551615),
    "byte": (0, 255),
}

DIRECT_TYPES = (
    "bool", "float", "double",
    "int8", "int16", "int32", "int64",
    "uint8", "uint16", "uint32", "uint64",
    "FString", "FName", "FGameplayTag",
    "FVector", "FVector2D", "FVector4", "FQuat",
    "FRotator", "FTransform", "FColor", "FLinearColor",
    "FIntPoint", "FIntVector", "FDateTime", "FTimespan", "FGuid",
)


def clean_cpp_type(raw_type: Optional[str]) -> str:
    """
    清洗表头中可能包含的跨表外键注解语法 (如 FName<DT_Target> -> FName)
    @param raw_type 原始表头类型声明
    @return 清洗后的 C++ 类型名
    """
    if not raw_type:
        return "FString"
    clean_type = str(raw_type).strip()
    clean_type = re.sub(r"->\s*DT_[A-Za-z0-9_]+", "", clean_type)
    clean_type = re.sub(r"<DT_[A-Za-z0-9_]+>", "", clean_type)
    clean_type = re.sub(r"\[DT_[A-Za-z0-9_]+\]", "", clean_type)
    return clean_type.strip()


def get_default_init_expr(cpp_type: str) -> str:
    """
    根据精确 C++ 标量类型生成声明处初始化代码（容器及结构体类型默认构造，不追加标量字面量）
    @param cpp_type 清洗后的 C++ 类型声明
    @return 声明处初始化后缀（如 " = 0.0f;" / " = 0.0;" / " = 0;" / " = false;" / ";"）
    """
    type_lower = cpp_type.strip().lower()
    if type_lower in FLOAT_SCALAR_TYPES:
        return " = 0.0f;"
    if type_lower in DOUBLE_SCALAR_TYPES:
        return " = 0.0;"
    if type_lower in INT_SCALAR_TYPES:
        return " = 0;"
    if type_lower in BOOL_SCALAR_TYPES:
        return " = false;"
    return ";"


def _nested_types(nested_struct_types: Optional[Mapping[str, Any]]) -> Mapping[str, Any]:
    """
    归一化嵌套 USTRUCT 类型映射（类型名 -> 头文件 include 路径）
    @param nested_struct_types 可选嵌套结构体映射
    @return 非空映射对象
    """
    return nested_struct_types or {}


def payload_mode(
    cpp_type: str,
    nested_struct_types: Optional[Mapping[str, Any]] = None,
    allowed_enum_types: Optional[Set[str]] = None,
) -> str:
    """
    判定字段负载读写方式
    @param cpp_type 清洗后的 C++ 类型
    @param nested_struct_types 配置声明的嵌套 USTRUCT 类型映射
    @param allowed_enum_types 可选允许的强类型枚举键集合（如 EItemKey）
    @return "text" / "soft_object" / "soft_class" / "struct" / "direct"
    @raises ValueError 类型未纳入支持清单（避免静默产出错误布局或触发归档断言）
    """
    nested = _nested_types(nested_struct_types)
    enums = allowed_enum_types or set()
    if cpp_type == "FText":
        return "text"
    if cpp_type.startswith("TSoftObjectPtr<") and cpp_type.endswith(">"):
        return "soft_object"
    if cpp_type.startswith("TSoftClassPtr<") and cpp_type.endswith(">"):
        return "soft_class"
    if cpp_type in nested:
        return "struct"
    for container_prefix in ("TArray<", "TSet<"):
        if cpp_type.startswith(container_prefix) and cpp_type.endswith(">"):
            element_type = cpp_type[len(container_prefix):-1].strip()
            if element_type in nested:
                raise ValueError(f"容器嵌套 USTRUCT 暂不支持（元素类型 {element_type}），请改用扁平字段或独立子表")
            if element_type in DIRECT_TYPES or element_type in enums:
                return "direct"
            raise ValueError(f"不支持的容器元素类型: {cpp_type}")
    if cpp_type in DIRECT_TYPES or cpp_type in enums:
        return "direct"
    raise ValueError(f"不支持的字段类型: {cpp_type}（嵌套 USTRUCT 请在 nested_struct_types 中登记）")


def validate_field_type(
    cpp_type: str,
    nested_struct_types: Optional[Mapping[str, Any]] = None,
    context: str = "",
    allowed_enum_types: Optional[Set[str]] = None,
) -> str:
    """
    生成期字段类型校验（与序列化生成共用同一白名单）
    @param cpp_type 清洗后的 C++ 类型
    @param nested_struct_types 配置声明的嵌套 USTRUCT 类型映射
    @param context 诊断上下文（表名/字段名）
    @param allowed_enum_types 可选允许的强类型枚举键集合（如 EItemKey）
    @return payload_mode 判定结果
    @raises ValueError 类型不支持
    """
    try:
        return payload_mode(cpp_type, nested_struct_types, allowed_enum_types=allowed_enum_types)
    except ValueError as error:
        prefix = f"{context}: " if context else ""
        raise ValueError(f"{prefix}{error}") from error
