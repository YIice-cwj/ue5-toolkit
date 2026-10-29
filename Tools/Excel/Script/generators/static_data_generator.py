#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
静态数据容器生成器 (static_data_generator.py)

为每个分类生成 F{cat_folder}StaticData 头文件（每次覆盖），
内含各分表 TMap 成员、LoadAllFromJson 加载入口与类型安全的 Find(Key) 接口。
遵循接口优先原则（Public 成员函数优先于成员变量排布）。
"""

import os

from typing import Any

from generators import GeneratedFile, GeneratorContext
from naming_utils import to_field_name


def generate_static_data_header(cfg: Any, cat_folder: str, tables: list) -> str:
    """
    生成分类的 StaticData 头文件 (F{cat_folder}StaticData)
    @param cfg 运行配置
    @param cat_folder 分类目录名
    @param tables 该分类的表元信息列表
    """
    generated_header = f"{cat_folder}StaticData.generated.h"
    agg_header = f"{cfg.row_types_include_prefix}/{cat_folder}/{cat_folder}DataTableRowTypes.h"
    composite_header = f"{cat_folder}CompositeData.h"

    lines = []
    lines.append("/**")
    lines.append(f" * @file {cat_folder}StaticData.h")
    lines.append(f" * @brief 静态数据容器")
    lines.append(" */")
    lines.append("#pragma once")
    lines.append("")
    lines.append('#include "CoreMinimal.h"')
    lines.append("")
    lines.append('#include "Misc/Paths.h"')
    lines.append('#include "Utils/DataTableLoaderUtils.h"')
    lines.append('#include "StaticData/FStaticDataRepository.h"')
    lines.append(f'#include "{agg_header}"')
    lines.append(f'#include "{composite_header}"')
    lines.append(f'#include "{generated_header}"')
    lines.append("")

    lines.append("/**")
    lines.append(f" * @brief {cat_folder} 静态数据容器（存储该模块所有分表的 Map，由 UStaticDataSubsystem 持有）")
    lines.append(" */")
    lines.append("USTRUCT(BlueprintType)")
    lines.append(f"struct {cfg.api_macro} F{cat_folder}StaticData {{")
    lines.append("    GENERATED_BODY()")
    lines.append("")
    lines.append("public:")

    lines.append("    /**")
    lines.append(f"     * @brief 从指定 JSON 目录加载当前模块所有分表数据")
    lines.append(f'     * @param InJsonDir JSON 所在根目录，缺省自动使用 [项目目录]/{cfg.json_root_dir_name}/{cat_folder}')
    lines.append('     * @return 是否所有分表均加载成功')
    lines.append("     */")
    lines.append('    bool LoadAllFromJson(const FString& InJsonDir = TEXT("")) {')
    lines.append("        const FString BaseDir = InJsonDir.IsEmpty()")
    lines.append(f"            ? (FPaths::ProjectDir() / TEXT(\"{cfg.json_root_dir_name}\") / TEXT(\"{cat_folder}\"))")
    lines.append("            : InJsonDir;")
    lines.append("")
    lines.append("        bool bAllSuccess = true;")
    for item in tables:
        stem = item["stem"]
        field_name = to_field_name(stem, cat_folder)
        map_name = f"{field_name}Map"
        json_file = item["json_filename"]
        lines.append(f'        bAllSuccess &= FDataTableLoaderUtils::LoadTableFromJson(BaseDir / TEXT("{json_file}"), {map_name});')
    lines.append("        return bAllSuccess;")
    lines.append("    }")
    lines.append("")

    lines.append("    /**")
    lines.append(f"     * @brief 从生成式二进制仓库整体装载当前模块所有分表数据")
    lines.append("     * @param Repository 已登记读写器并完成 .bin 装载的仓库")
    lines.append("     * @return 是否所有分表均提取成功（失败时可用仓库错误码定位原因）")
    lines.append("     */")
    lines.append("    bool LoadAllFromRepository(const FStaticDataRepository& Repository) {")
    lines.append("        bool bAllSuccess = true;")
    for item in tables:
        field_name = to_field_name(item["stem"], cat_folder)
        lines.append(f"        bAllSuccess &= Repository.ExtractTable({field_name}Map);")
    lines.append("        return bAllSuccess;")
    lines.append("    }")
    lines.append("")

    lines.append("    /**")
    lines.append(f"     * @brief 将当前模块所有分表数据写入生成式二进制仓库")
    lines.append("     * @param Repository 目标仓库（须已登记读写器）")
    lines.append("     * @return 是否所有分表均写入成功（失败时可用仓库错误码定位原因）")
    lines.append("     */")
    lines.append("    bool AddAllToRepository(FStaticDataRepository& Repository) const {")
    lines.append("        bool bAllSuccess = true;")
    for item in tables:
        field_name = to_field_name(item["stem"], cat_folder)
        lines.append(f"        bAllSuccess &= Repository.AddTable({field_name}Map);")
    lines.append("        return bAllSuccess;")
    lines.append("    }")
    lines.append("")

    for item in tables:
        stem = item["stem"]
        desc = item["desc"]
        struct_name = item["struct_name"]
        enum_name = item.get("enum_name", "") or ""
        field_name = to_field_name(stem, cat_folder)
        map_name = f"{field_name}Map"
        lines.append("    /**")
        lines.append(f"     * @brief 通过行键查找 {desc}")
        lines.append("     * @param Key DataTable 行键（FName）")
        lines.append("     * @return 数据行指针，未找到则返回 nullptr")
        lines.append("     */")
        lines.append(f"    const {struct_name}* Find(const FName& Key) const noexcept {{")
        lines.append(f"        return {map_name}.Find(Key);")
        lines.append("    }")
        lines.append("")
        if cfg.row_key_mode == "enum" and enum_name:
            lines.append("    /**")
            lines.append(f"     * @brief 通过唯一键枚举查找 {desc}")
            lines.append("     * @param key 唯一键枚举值")
            lines.append("     * @return 数据行指针，未找到则返回 nullptr")
            lines.append("     */")
            lines.append(f"    const {struct_name}* Find({enum_name} key) const noexcept {{")
            lines.append(f"        return {map_name}.Find(ToRowName(key));")
            lines.append("    }")
            lines.append("")

    for item in tables:
        stem = item["stem"]
        desc = item["desc"]
        struct_name = item["struct_name"]
        field_name = to_field_name(stem, cat_folder)
        map_name = f"{field_name}Map"
        lines.append(f'    /** {desc} 映射表 (RowName -> {struct_name}) */')
        lines.append(f'    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "{cat_folder}|StaticData", meta = (DisplayName = "{desc}映射表"))')
        lines.append(f'    TMap<FName, {struct_name}> {map_name};')
        lines.append("")

    lines.append("};")
    lines.append("")

    return "\n".join(lines)


def generate(ctx: GeneratorContext) -> list:
    """
    为全部分类生成 StaticData 头文件
    @param ctx 生成器运行上下文
    @return 各分类 F{cat_folder}StaticData.h（overwrite）
    """
    files = []
    for cat_folder, tables in ctx.category_tables.items():
        content = generate_static_data_header(ctx.cfg, cat_folder, tables)
        out_path = os.path.join(ctx.cfg.static_data_public_dir, cat_folder, f"{cat_folder}StaticData.h")
        files.append(GeneratedFile(path=out_path, content=content, policy="overwrite"))
    return files
