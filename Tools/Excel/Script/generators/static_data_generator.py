#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
静态数据容器生成器 (static_data_generator.py)

为每个分类生成 F{cat_folder}StaticData 头文件（每次覆盖），
内含各分表 TMap 成员、LoadAllFromJson / LoadAllFromRepository / AddAllToRepository
（统一返回 TGameStatusResult<void> 透传底层精确错误码）与类型安全的 Find(Key) 接口。
"""

import os
from typing import Any

from generators import GeneratedFile, GeneratorContext, doxygen_brief, escape_cpp_string
from naming_utils import to_field_name


def generate_static_data_header(cfg: Any, cat_folder: str, tables: list) -> str:
    """
    生成分类的 StaticData 头文件 (F{cat_folder}StaticData)
    @param cfg 运行配置
    @param cat_folder 分类目录名
    @param tables 该分类的表元信息列表
    @return 头文件代码文本
    """
    generated_header = f"{cat_folder}StaticData.generated.h"
    agg_header = f"{cfg.row_types_include_prefix}/{cat_folder}/{cat_folder}DataTableRowTypes.h"
    multi_table = len(tables) > 1
    rel_dirs = [item.get("json_rel_dir", cat_folder) for item in tables]
    uniform_rel_dir = rel_dirs[0] if (rel_dirs and len(set(rel_dirs)) == 1) else None

    lines = [
        "/**",
        f" * @file {cat_folder}StaticData.h",
        f" * @brief {cat_folder} 静态数据容器",
        " */",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
    ]
    if multi_table:
        lines.append("#include <type_traits>")
    lines.extend([
        "",
        '#include "Misc/Paths.h"',
        '#include "StaticData/FStaticDataRepository.h"',
        '#include "Types/TGameResult.h"',
        '#include "Utils/DataTableLoaderUtils.h"',
        f'#include "{agg_header}"',
        f'#include "{generated_header}"',
        "",
        "/**",
        f" * @brief {cat_folder} 静态数据容器（存储该模块所有分表的 Map，由 FStaticDataSnapshot 只读持有）",
        " */",
        "USTRUCT(BlueprintType)",
        f"struct {cfg.api_macro} F{cat_folder}StaticData {{",
        "    GENERATED_BODY()",
        "",
        "public:",
        "    /**",
        "     * @brief 从指定 JSON 目录加载当前模块所有分表数据",
        f"     * @param InJsonDir JSON 所在根目录，缺省自动使用 [项目目录]/{cfg.json_root_dir_name}/{cat_folder}",
        "     * @return 全部加载成功返回 Success，任一分表失败透传底层 Game::DataErrorCodes 失败结果",
        "     */",
        '    [[nodiscard]] TGameStatusResult<void> LoadAllFromJson(const FString& InJsonDir = TEXT("")) {',
    ])

    if uniform_rel_dir is not None:
        default_dir_expr = (
            f'(FPaths::ProjectDir() / TEXT("{cfg.json_root_dir_name}") / TEXT("{uniform_rel_dir}"))'
            if uniform_rel_dir
            else f'(FPaths::ProjectDir() / TEXT("{cfg.json_root_dir_name}"))'
        )
        lines.extend([
            "        const FString BaseDir = InJsonDir.IsEmpty()",
            f"            ? {default_dir_expr}",
            "            : InJsonDir;",
            "",
        ])
    else:
        lines.extend([
            f'        const FString RootJsonDir = FPaths::ProjectDir() / TEXT("{cfg.json_root_dir_name}");',
            "",
        ])

    for item in tables:
        stem = item["stem"]
        struct_name = item["struct_name"]
        field_name = to_field_name(stem, cat_folder)
        map_name = f"{field_name}Map"
        json_file = item["json_filename"]
        if uniform_rel_dir is not None:
            path_expr = f'BaseDir / TEXT("{json_file}")'
        else:
            item_rel = item.get("json_rel_dir", cat_folder)
            default_table_dir = f'RootJsonDir / TEXT("{item_rel}")' if item_rel else "RootJsonDir"
            path_expr = f'(InJsonDir.IsEmpty() ? ({default_table_dir}) : InJsonDir) / TEXT("{json_file}")'
        lines.extend([
            f"        auto LoadRes_{field_name} = FDataTableLoaderUtils::LoadTableFromJson<{struct_name}>({path_expr});",
            f"        if (LoadRes_{field_name}.IsFailure()) {{",
            f"            return TGameStatusResult<void>::Failure(LoadRes_{field_name}.GetError());",
            "        }",
            f"        {map_name} = LoadRes_{field_name}.ExtractValue();",
        ])
    lines.extend([
        "        return TGameStatusResult<void>::Success();",
        "    }",
        "",
        "    /**",
        "     * @brief 从生成式二进制仓库整体装载当前模块所有分表数据",
        "     * @param Repository 已登记读写器并完成 .bin 装载的仓库",
        "     * @return 全部提取成功返回 Success，任一分表失败透传底层 Game::DataErrorCodes 失败结果",
        "     */",
        "    [[nodiscard]] TGameStatusResult<void> LoadAllFromRepository(const FStaticDataRepository& Repository) {",
    ])
    for item in tables:
        struct_name = item["struct_name"]
        field_name = to_field_name(item["stem"], cat_folder)
        map_name = f"{field_name}Map"
        lines.extend([
            f"        auto ExtractRes_{field_name} = Repository.ExtractTable<{struct_name}>();",
            f"        if (ExtractRes_{field_name}.IsFailure()) {{",
            f"            return TGameStatusResult<void>::Failure(ExtractRes_{field_name}.GetError());",
            "        }",
            f"        {map_name} = ExtractRes_{field_name}.ExtractValue();",
        ])
    lines.extend([
        "        return TGameStatusResult<void>::Success();",
        "    }",
        "",
        "    /**",
        "     * @brief 将当前模块所有分表数据写入生成式二进制仓库",
        "     * @param Repository 目标仓库（须已登记读写器）",
        "     * @return 全部写入成功返回 Success，任一分表失败透传底层 Game::DataErrorCodes 失败结果",
        "     */",
        "    [[nodiscard]] TGameStatusResult<void> AddAllToRepository(FStaticDataRepository& Repository) const {",
    ])
    for item in tables:
        field_name = to_field_name(item["stem"], cat_folder)
        lines.extend([
            f"        if (const TGameStatusResult<void> AddRes_{field_name} = Repository.AddTable({field_name}Map); AddRes_{field_name}.IsFailure()) {{",
            f"            return AddRes_{field_name};",
            "        }",
        ])
    lines.extend([
        "        return TGameStatusResult<void>::Success();",
        "    }",
        "",
    ])

    if multi_table:
        lines.extend([
            "    /**",
            "     * @brief 按行结构类型与行键查找指定分表数据行（多表域消除同名重载冲突）",
            "     * @tparam TRow 目标分表行结构类型",
            "     * @param Key DataTable 行键（FName）",
            "     * @return 数据行指针，未找到则返回 nullptr",
            "     */",
            "    template <typename TRow>",
            "    const TRow* Find(const FName& Key) const noexcept {",
        ])
        for idx, item in enumerate(tables):
            struct_name = item["struct_name"]
            field_name = to_field_name(item["stem"], cat_folder)
            map_name = f"{field_name}Map"
            branch = "if constexpr" if idx == 0 else "} else if constexpr"
            lines.extend([
                f"        {branch} (std::is_same_v<TRow, {struct_name}>) {{",
                f"            return {map_name}.Find(Key);",
            ])
        lines.extend([
            "        } else {",
            '            static_assert(sizeof(TRow) == 0, "TRow does not belong to this StaticData category");',
            "            return nullptr;",
            "        }",
            "    }",
            "",
        ])

    for item in tables:
        stem = item["stem"]
        desc = item["desc"]
        struct_name = item["struct_name"]
        enum_name = item.get("enum_name", "") or ""
        field_name = to_field_name(stem, cat_folder)
        map_name = f"{field_name}Map"
        if not multi_table:
            lines.extend([
                "    /**",
                f"     * @brief 通过行键查找 {desc}",
                "     * @param Key DataTable 行键（FName）",
                "     * @return 数据行指针，未找到则返回 nullptr",
                "     */",
                f"    const {struct_name}* Find(const FName& Key) const noexcept {{",
                f"        return {map_name}.Find(Key);",
                "    }",
                "",
            ])
        lines.extend([
            "    /**",
            f"     * @brief 按分表限定名与行键查找 {desc}",
            "     * @param Key DataTable 行键（FName）",
            "     * @return 数据行指针，未找到则返回 nullptr",
            "     */",
            f"    const {struct_name}* Find{field_name}(const FName& Key) const noexcept {{",
            f"        return {map_name}.Find(Key);",
            "    }",
            "",
        ])
        if cfg.row_key_mode == "enum" and enum_name:
            lines.extend([
                "    /**",
                f"     * @brief 通过唯一键枚举查找 {desc}",
                "     * @param InKey 唯一键枚举值",
                "     * @return 数据行指针，未找到则返回 nullptr",
                "     */",
                f"    const {struct_name}* Find({enum_name} InKey) const noexcept {{",
                f"        return {map_name}.Find(ToRowName(InKey));",
                "    }",
                "",
            ])

    for item in tables:
        stem = item["stem"]
        desc = item["desc"]
        safe_desc = escape_cpp_string(desc)
        struct_name = item["struct_name"]
        field_name = to_field_name(stem, cat_folder)
        map_name = f"{field_name}Map"
        lines.extend(doxygen_brief(f"{desc} 映射表 (RowName -> {struct_name})"))
        lines.append(f'    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "{cat_folder}|StaticData", meta = (DisplayName = "{safe_desc}映射表"))')
        lines.append(f"    TMap<FName, {struct_name}> {map_name};")
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
    for cat_folder in sorted(ctx.category_tables.keys()):
        tables = ctx.category_tables[cat_folder]
        content = generate_static_data_header(ctx.cfg, cat_folder, tables)
        out_path = os.path.join(ctx.cfg.static_data_public_dir, cat_folder, f"{cat_folder}StaticData.h")
        files.append(GeneratedFile(path=out_path, content=content, policy="overwrite"))
    return files
