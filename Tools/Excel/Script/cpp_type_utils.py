#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
C++ 类型工具模块 (cpp_type_utils.py)

- clean_cpp_type: 清洗表头类型签名（去除跨表外键注解语法）
- get_default_init_expr: 根据 C++ 类型生成声明处初始化代码
- payload_mode: 统一的字段序列化方式判定（RowStruct 生成与序列化生成共用同一白名单）
- validate_field_type: 生成期字段类型校验（不支持类型直接阻断管线）
"""

import re
from typing import Any, Dict, Mapping, Optional

# FArchive 原生 operator<< 支持的类型（含引擎数学/时间/GUID 结构体）
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
    """清洗表头中可能包含的跨表外键注解语法 (如 FName<DT_Target> -> FName)"""
    if not raw_type:
        return "FString"
    clean_type = raw_type.strip()
    clean_type = re.sub(r"->\s*DT_[A-Za-z0-9_]+", "", clean_type)
    clean_type = re.sub(r"<DT_[A-Za-z0-9_]+>", "", clean_type)
    clean_type = re.sub(r"\[DT_[A-Za-z0-9_]+\]", "", clean_type)
    return clean_type.strip()


def get_default_init_expr(cpp_type: str) -> str:
    """根据 C++ 类型生成声明处初始化代码"""
    type_lower = cpp_type.lower()
    if "float" in type_lower or "double" in type_lower:
        return " = 0.0f;"
    if "int" in type_lower or "uint" in type_lower or "byte" in type_lower:
        return " = 0;"
    if "bool" in type_lower:
        return " = false;"
    return ";"


def _nested_types(nested_struct_types: Optional[Mapping[str, Any]]) -> Mapping[str, Any]:
    """归一化嵌套 USTRUCT 类型映射（类型名 -> 头文件 include 路径）"""
    return nested_struct_types or {}


def payload_mode(cpp_type: str, nested_struct_types: Optional[Mapping[str, Any]] = None) -> str:
    """
    判定字段负载读写方式
    @param cpp_type 清洗后的 C++ 类型
    @param nested_struct_types 配置声明的嵌套 USTRUCT 类型映射
    @return "text"（FText 显示字符串往返）/ "soft_object"（TSoftObjectPtr 软对象路径字符串往返）
            / "struct"（嵌套 USTRUCT 经 UScriptStruct 反射序列化）
            / "direct"（FArchive operator<< 直读直写）
    @raises ValueError 类型未纳入支持清单（避免静默产出错误布局或触发归档断言）
    """
    nested = _nested_types(nested_struct_types)
    if cpp_type == "FText":
        return "text"
    if cpp_type.startswith("TSoftObjectPtr<"):
        return "soft_object"
    if cpp_type in nested:
        return "struct"
    if cpp_type.startswith("TArray<") and cpp_type.endswith(">"):
        element_type = cpp_type[len("TArray<"):-1].strip()
        if element_type in nested:
            raise ValueError(f"数组嵌套 USTRUCT 暂不支持（元素类型 {element_type}），请改用扁平字段或独立子表")
        if element_type in DIRECT_TYPES:
            return "direct"
        raise ValueError(f"不支持的数组元素类型: {cpp_type}")
    if cpp_type in DIRECT_TYPES:
        return "direct"
    raise ValueError(f"不支持的字段类型: {cpp_type}（嵌套 USTRUCT 请在 nested_struct_types 中登记）")


def validate_field_type(
    cpp_type: str,
    nested_struct_types: Optional[Mapping[str, Any]] = None,
    context: str = "",
) -> str:
    """
    生成期字段类型校验（与序列化生成共用同一白名单）
    @param cpp_type 清洗后的 C++ 类型
    @param nested_struct_types 配置声明的嵌套 USTRUCT 类型映射
    @param context 诊断上下文（表名/字段名）
    @return payload_mode 判定结果
    @raises ValueError 类型不支持
    """
    try:
        return payload_mode(cpp_type, nested_struct_types)
    except ValueError as error:
        prefix = f"{context}: " if context else ""
        raise ValueError(f"{prefix}{error}") from error
