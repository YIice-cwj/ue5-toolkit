#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
文件事务写入器与全部 14 个 C++ 代码生成器 (file_writer / generators/*) 单元测试
"""

import os
import tempfile
import unittest
from unittest import mock

from test_helpers import create_mini_project
import file_writer
import main as codegen_main
from generators import (
    GeneratorContext,
    aggregator_generator,
    composite_data_generator,
    doxygen_brief,
    escape_cpp_string,
    game_registration_generator,
    interface_generator,
    query_generator,
    registration_generator,
    row_struct_generator,
    serialization_generator,
    service_generator,
    static_data_generator,
    static_data_registration_generator,
    static_data_snapshot_generator,
    subsystem_generator,
)


def _sample_context(module_dir: str, row_key_mode: str = "fname") -> GeneratorContext:
    """构造覆盖多类型字段的生成器测试上下文"""
    cfg = codegen_main.ToolConfig(
        data_tables_source_dir=os.path.join(module_dir, "DT"),
        output_module_dir=module_dir,
        api_macro="DEMOMOD_API",
        subsystem_header_include="Generated/Data/StaticData/UStaticDataSubsystem.h",
        json_root_dir_name="DataTables_Json",
        static_data_bin_content_path="DataTables_Bin/StaticDataCache.bin",
        project_prefix="Demo",
        blueprint_category="Demo|StaticData",
        log_header="Logging/GameLogCategories.h",
        log_macro="GAME_LOG",
        log_category_name="LogGame",
        export_bin_command="Demo.ExportStaticDataBin",
        report_integrity_command="Demo.ReportStaticDataIntegrity",
        static_data_channel_cmd_arg="DemoStaticDataChannel",
        verify_launch_switch="DemoStaticDataVerify",
        export_commandlet="DemoStaticDataExport",
        registration_class_name="FDemoRegistration",
        game_instance_class_name="UDemoGameInstance",
        row_serializer_register_func="RegisterDemoStaticDataRowSerializers",
        game_config_defaults={"Demo.Region": "CN"},
        row_key_mode=row_key_mode,
        nested_struct_types={"FNestedBonus": "Custom/NestedBonus.h"},
    )
    headers_item = [
        ["主键", '名称"带引号"', "价格", "权重", "标签", "图标", "蓝图类", "坐标", "奖励", "列表"],
        ["RowName", "display_name", "price", "weight", "tag", "icon", "actor_cls", "grid_pos", "bonus", "ids"],
        ["FName", "FText", "uint32", "double", "FGameplayTag", "TSoftObjectPtr<UTexture2D>", "TSoftClassPtr<AActor>", "FIntPoint", "FNestedBonus", "TArray<int32>"],
    ]
    fields_item = serialization_generator.extract_fields(headers_item)
    header_name, struct_name, enum_name = row_struct_generator.table_names("ItemDefinition", row_key_mode)
    return GeneratorContext(
        cfg=cfg,
        category_headers={"Item": [header_name]},
        category_tables={
            "Item": [{
                "stem": "ItemDefinition",
                "desc": "Item_Definition数据行",
                "struct_name": struct_name,
                "enum_name": enum_name,
                "header_name": header_name,
                "json_filename": "DT_Item_Definition.json",
                "fields": fields_item,
            }],
        },
    )


class TestFileWriterAndStaging(unittest.TestCase):
    """验证事务暂存区路径隔离、原子换入、失败回滚与脚手架修复检测"""

    def test_staging_dir_outside_source_module_and_atomic_commit(self) -> None:
        """暂存树位于插件 Intermediate 下，绝不污染 Source/<Module>/"""
        with tempfile.TemporaryDirectory() as tmp:
            mod_dir = os.path.join(tmp, "Plugins", "Sample", "Source", "Sample")
            os.makedirs(mod_dir, exist_ok=True)
            old_cat = os.path.join(mod_dir, "Public", "Generated", "OldCat")
            os.makedirs(old_cat, exist_ok=True)
            with open(os.path.join(old_cat, "Legacy.h"), "w", encoding="utf-8") as handle:
                handle.write("// stale")

            staging = file_writer.GenerationStaging(mod_dir)
            self.assertFalse(staging.staging_root.startswith(mod_dir))
            self.assertFalse(os.path.exists(os.path.join(mod_dir, "Intermediate")))

            new_file = os.path.join(mod_dir, "Public", "Generated", "NewCat", "New.h")
            in_mod_reg = os.path.join(mod_dir, "Public", "Registration", "InModRegistration.h")
            ext_file = os.path.join(tmp, "Source", "App", "Public", "Reg.h")
            action1, existed1 = staging.stage_file(new_file, "#pragma once\n", "overwrite")
            action_in_mod, existed_in_mod = staging.stage_file(in_mod_reg, "#pragma once\n", "overwrite")
            action2, existed2 = staging.stage_file(ext_file, "#pragma once\n", "overwrite")
            self.assertEqual((action1, existed1), ("overwritten", False))
            self.assertEqual((action_in_mod, existed_in_mod), ("overwritten", False))
            self.assertEqual((action2, existed2), ("overwritten", False))
            self.assertEqual(staging.staged_count, 3)

            staging.commit()
            self.assertTrue(os.path.isfile(new_file))
            self.assertTrue(os.path.isfile(in_mod_reg))
            self.assertTrue(os.path.isfile(ext_file))
            self.assertFalse(os.path.exists(old_cat))
            self.assertFalse(os.path.exists(staging.staging_root))

    def test_staging_abort_preserves_disk(self) -> None:
        """abort() 清理所有暂存文件且磁盘既有文件完好无损"""
        with tempfile.TemporaryDirectory() as tmp:
            mod_dir = os.path.join(tmp, "Mod")
            existing = os.path.join(mod_dir, "Public", "Generated", "Keep.h")
            file_writer.write_file(existing, "ORIGINAL", "overwrite")

            staging = file_writer.GenerationStaging(mod_dir)
            staging.stage_file(existing, "MODIFIED", "overwrite")
            staging.abort()

            with open(existing, "r", encoding="utf-8") as handle:
                self.assertEqual(handle.read(), "ORIGINAL")
            self.assertFalse(os.path.exists(staging.staging_root))

    def test_scaffold_repair_detects_pascalcase_map_drift(self) -> None:
        """is_broken_or_empty_scaffold 准确识别 PascalCase Map 名漂移并保护用户自定义实现"""
        with tempfile.TemporaryDirectory() as tmp:
            cpp_path = os.path.join(tmp, "Public", "Generated", "Data", "StaticData", "Item", "ItemStaticDataInterface.cpp")
            broken_content = (
                "// Automatically generated initial interface implementation for Item.\n"
                "void Foo() { InData.OldDeletedMap.Find(Key); }\n"
            )
            file_writer.write_file(cpp_path, broken_content, "overwrite")
            self.assertTrue(file_writer.is_broken_or_empty_scaffold(cpp_path, {"ItemMap"}))

            valid_content = (
                "// Automatically generated initial interface implementation for Item.\n"
                "void Foo() { TMap<FName, int32> UserLocalMap; InData.ItemMap.Find(Key); }\n"
            )
            file_writer.write_file(cpp_path, valid_content, "overwrite")
            self.assertFalse(file_writer.is_broken_or_empty_scaffold(cpp_path, {"ItemMap"}))

            custom_content = (
                "// Automatically generated initial interface implementation for Item.\n"
                "void Foo() { GAME_LOG(Log, TEXT(\"custom\")); InData.OldDeletedMap.Find(Key); }\n"
            )
            file_writer.write_file(cpp_path, custom_content, "overwrite")
            self.assertFalse(file_writer.is_broken_or_empty_scaffold(cpp_path, {"ItemMap"}))

            act, existed = file_writer.write_file(cpp_path, "NEW", "preserve")
            self.assertEqual((act, existed), ("preserved", True))
            with self.assertRaises(ValueError):
                file_writer.write_file(cpp_path, "X", "unknown_policy")

            empty_dir = os.path.join(tmp, "EmptySub")
            os.makedirs(empty_dir)
            self.assertTrue(file_writer.remove_dir_if_empty(empty_dir))
            self.assertFalse(file_writer.remove_dir_if_empty(empty_dir))

    def test_file_writer_repair_include_check_and_commit_rollback(self) -> None:
        """覆盖脚手架头存在判定、preserve/repair 创建与修复、非法策略与 commit 失败自动回滚"""
        with tempfile.TemporaryDirectory() as tmp:
            mod_dir = os.path.join(tmp, "MyMod")
            row_h = os.path.join(mod_dir, "Public", "Generated", "DataTables", "Item", "ItemRow.h")
            file_writer.write_file(row_h, "#pragma once\n", "overwrite")
            iface_cpp = os.path.join(mod_dir, "Private", "Generated", "Data", "StaticData", "Item", "ItemInterface.cpp")

            self.assertTrue(file_writer.is_broken_or_empty_scaffold(iface_cpp, {"ItemMap"}))

            custom_non_scaffold = "// Hand written user code\nvoid Custom() {}\n"
            file_writer.write_file(iface_cpp, custom_non_scaffold, "overwrite")
            self.assertFalse(file_writer.is_broken_or_empty_scaffold(iface_cpp, {"ItemMap"}))

            with_log_marker = "// Automatically generated initial interface\nMY_CUSTOM_LOG();\n"
            file_writer.write_file(iface_cpp, with_log_marker, "overwrite")
            self.assertFalse(file_writer.is_broken_or_empty_scaffold(iface_cpp, {"ItemMap"}, log_marker="MY_CUSTOM_LOG"))

            valid_inc = '// Automatically generated initial interface\n#include "Generated/DataTables/Item/ItemRow.h"\n'
            file_writer.write_file(iface_cpp, valid_inc, "overwrite")
            self.assertFalse(file_writer.is_broken_or_empty_scaffold(iface_cpp, {"ItemMap"}))

            broken_inc = '// Automatically generated initial interface\n#include "Generated/DataTables/Item/DeletedRow.h"\n'
            file_writer.write_file(iface_cpp, broken_inc, "overwrite")
            self.assertTrue(file_writer.is_broken_or_empty_scaffold(iface_cpp, {"ItemMap"}))

            bad_utf8 = os.path.join(tmp, "BadUtf8.cpp")
            with open(bad_utf8, "wb") as handle:
                handle.write(b"\xff\xfe\xfd")
            self.assertFalse(file_writer.is_broken_or_empty_scaffold(bad_utf8, {"ItemMap"}))

            new_preserve = os.path.join(mod_dir, "Private", "Generated", "FreshPreserve.h")
            self.assertEqual(file_writer.write_file(new_preserve, "A", "preserve"), ("created", False))
            new_repair = os.path.join(mod_dir, "Private", "Generated", "Data", "StaticData", "Item", "FreshRepair.cpp")
            self.assertEqual(file_writer.write_file(new_repair, valid_inc, "repair", {"ItemMap"}), ("created", False))
            self.assertEqual(file_writer.write_file(new_repair, "X", "repair", {"ItemMap"}), ("preserved", True))
            file_writer.write_file(new_repair, broken_inc, "overwrite")
            self.assertEqual(file_writer.write_file(new_repair, valid_inc, "repair", {"ItemMap"}), ("repaired", True))

            staging = file_writer.GenerationStaging(mod_dir)
            staged_new_row = os.path.join(mod_dir, "Public", "Generated", "DataTables", "Item", "ItemNewRow.h")
            staging.stage_file(staged_new_row, "#pragma once\n", "overwrite")
            stale_inc_on_live_only = '// Automatically generated initial interface\n#include "Generated/DataTables/Item/ItemRow.h"\n'
            file_writer.write_file(iface_cpp, stale_inc_on_live_only, "overwrite")
            self.assertEqual(staging.stage_file(iface_cpp, "REPAIRED_CONTENT", "repair", {"ItemMap"}), ("repaired", True))
            staging.stage_file(
                os.path.join(mod_dir, "Public", "Generated", "DataTables", "Item", "ItemRow.h"),
                "#pragma once\n",
                "overwrite",
            )
            self.assertEqual(staging.stage_file(row_h, "IGNORED", "preserve"), ("preserved", True))
            file_writer.write_file(iface_cpp, valid_inc, "overwrite")
            self.assertEqual(staging.stage_file(iface_cpp, "IGNORED", "repair", {"ItemMap"}), ("preserved", True))
            with self.assertRaises(ValueError):
                staging.stage_file(row_h, "X", "invalid_policy")
            ext_target = os.path.join(tmp, "OutsideDir", "OutsideReg.h")
            staging.stage_file(ext_target, "#pragma once\n", "overwrite")
            self.assertFalse(os.path.exists(os.path.dirname(ext_target)))
            with mock.patch.object(file_writer.os, "replace", side_effect=OSError("disk full")):
                with self.assertRaises(OSError):
                    staging.commit()
            self.assertTrue(os.path.isfile(row_h))
            self.assertFalse(os.path.exists(ext_target))


class TestAllGenerators(unittest.TestCase):
    """覆盖全部 14 个生成器模块的产物结构、注释规范、错误码与跨平台行为"""

    def test_escape_and_doxygen_helpers(self) -> None:
        """转义双引号/反斜杠/换行并产出多行 Doxygen"""
        self.assertEqual(escape_cpp_string('a\\b"c\nd'), 'a\\\\b\\"c d')
        lines = doxygen_brief("说明 */ 文本", indent="    ")
        self.assertEqual(len(lines), 3)
        self.assertEqual(lines[0], "    /**")
        self.assertIn("@brief", lines[1])
        self.assertEqual(lines[2], "     */")

    def test_row_struct_generator_forward_decls_escaping_and_enum_mode(self) -> None:
        """自动提取全部 TSoftObjectPtr/TSoftClassPtr 前置声明、转义 DisplayName、支持 enum 键模式及 >255 拦截"""
        with tempfile.TemporaryDirectory() as tmp:
            ctx = _sample_context(tmp, row_key_mode="enum")
            headers = [
                ["主键", '名称"引号"', "图标", "类", "坐标", "权重", "数组"],
                ["RowName", "title", "icon", "actor_cls", "pos", "weight", "nums"],
                ["FName", "FText", "TSoftObjectPtr<UTexture2D>", "TSoftClassPtr<AActor>", "FIntPoint", "double", "TArray<int32>"],
            ]
            row_items = [("1_Item", '铁"剑"'), ("_1_Item", "碰撞键"), ("Item_B", "木盾")]
            files = row_struct_generator.generate(ctx, "DT_Item.xlsx", "ItemDefinition", "物品数据行", headers, row_items, "Item")
            self.assertEqual(len(files), 1)
            code = files[0].content
            self.assertIn("class AActor;", code)
            self.assertIn("class UTexture2D;", code)
            self.assertIn('DisplayName = "名称\\"引号\\""', code)
            self.assertIn("double Weight = 0.0;", code)
            self.assertIn("FIntPoint Pos;", code)
            self.assertIn("TArray<int32> Nums;", code)
            self.assertIn("inline FName ToRowName(EItemDefinitionKey InKey) noexcept", code)
            self.assertIn("_1_Item", code)
            self.assertIn("_1_Item_2", code)

            too_many = [(f"Key_{i}", f"Disp_{i}") for i in range(255)]
            with self.assertRaises(ValueError):
                row_struct_generator.generate_struct_header("DT_Big.xlsx", "Big", "大表", headers, too_many, key_mode="enum")

    def test_interface_primary_table_case_insensitive(self) -> None:
        """select_primary_table 大小写无关优先选中 Definition 主表"""
        tables = [
            {"stem": "ItemCost", "header_name": "ItemCostRow.h", "struct_name": "FItemCostRow"},
            {"stem": "ItemDefinition", "header_name": "ItemDefinitionRow.h", "struct_name": "FItemDefinitionRow"},
        ]
        primary = interface_generator.select_primary_table("Item", tables)
        self.assertIsNotNone(primary)
        self.assertEqual(primary["stem"], "ItemDefinition")

    def test_snapshot_subsystem_serialization_and_registration_generators(self) -> None:
        """验证所有域/全局生成器使用最新无反射模板、薄 RCU 别名、GetPtr/Get 与自动控制台命令"""
        with tempfile.TemporaryDirectory() as tmp:
            ctx = _sample_context(tmp)

            agg_files = aggregator_generator.generate(ctx)
            self.assertEqual(len(agg_files), 3)
            self.assertIn("Item 分类数据表行聚合头文件", agg_files[0].content)
            self.assertNotIn("物品/商店", agg_files[0].content)

            comp_files = composite_data_generator.generate(ctx)
            self.assertEqual(len(comp_files), 1)
            self.assertIn(" * @brief", comp_files[0].content)

            sd_files = static_data_generator.generate(ctx)
            self.assertEqual(len(sd_files), 1)
            self.assertNotIn("CompositeData.h", sd_files[0].content)
            self.assertIn("TGameStatusResult<void> LoadAllFromJson", sd_files[0].content)
            self.assertIn("由 FStaticDataSnapshot 只读持有", sd_files[0].content)
            self.assertIn("const FItemDefinitionRow* Find(const FName& Key) const noexcept", sd_files[0].content)
            self.assertIn("const FItemDefinitionRow* FindDefinition(const FName& Key) const noexcept", sd_files[0].content)

            multi_ctx = _sample_context(tmp)
            multi_ctx.category_tables["Item"].append({
                "stem": "ItemWeapon",
                "desc": "Item_Weapon数据行",
                "struct_name": "FItemWeaponRow",
                "enum_name": "",
                "header_name": "ItemWeaponRow.h",
                "json_filename": "DT_Item_Weapon.json",
                "json_rel_dir": "Item",
                "fields": [],
            })
            multi_ctx.category_tables["Shared"] = [{
                "stem": "GlobalConfig",
                "desc": "GlobalConfig数据行",
                "struct_name": "FGlobalConfigRow",
                "enum_name": "",
                "header_name": "GlobalConfigRow.h",
                "json_filename": "DT_GlobalConfig.json",
                "json_rel_dir": "",
                "fields": [],
            }]
            multi_sd_files = static_data_generator.generate(multi_ctx)
            item_multi_code = next(f.content for f in multi_sd_files if f.path.endswith("ItemStaticData.h"))
            shared_root_code = next(f.content for f in multi_sd_files if f.path.endswith("SharedStaticData.h"))
            self.assertIn("template <typename TRow>", item_multi_code)
            self.assertIn("const TRow* Find(const FName& Key) const noexcept", item_multi_code)
            self.assertIn("const FItemDefinitionRow* FindDefinition(const FName& Key) const noexcept", item_multi_code)
            self.assertIn("const FItemWeaponRow* FindWeapon(const FName& Key) const noexcept", item_multi_code)
            self.assertNotIn("const FItemDefinitionRow* Find(const FName& Key)", item_multi_code)
            self.assertIn('? (FPaths::ProjectDir() / TEXT("DataTables_Json"))', shared_root_code)
            self.assertNotIn('TEXT("DataTables_Json") / TEXT("Shared")', shared_root_code)

            snap_files = static_data_snapshot_generator.generate(ctx)
            self.assertEqual(len(snap_files), 4)
            store_h = next(f for f in snap_files if f.path.endswith("FStaticDataSnapshotStore.h"))
            self.assertIn("using FStaticDataSnapshotStore = TStaticDataSnapshotStore<FStaticDataSnapshot>;", store_h.content)
            self.assertFalse(any(f.path.endswith("FStaticDataSnapshotStore.cpp") for f in snap_files))
            cache_h = next(f for f in snap_files if f.path.endswith("FStaticDataCache.h"))
            self.assertNotIn("void Reset();", cache_h.content)

            ser_files = serialization_generator.generate(ctx)
            self.assertEqual(len(ser_files), 2)
            ser_cpp = ser_files[1].content
            self.assertIn("void SerializeRowText(", ser_cpp)
            self.assertIn("FTextStringHelper::WriteToBuffer(", ser_cpp)
            self.assertIn("FTextStringHelper::ReadFromBuffer(", ser_cpp)
            self.assertIn("FObjectAndNameAsStringProxyArchive StructProxyAr(Ar, true);", ser_cpp)
            self.assertIn("uint32 SchemaHash = 0x", ser_cpp)
            self.assertIn("void SerializeRowSoftObject(", ser_cpp)
            self.assertIn("void SerializeRowSoftClass(", ser_cpp)
            self.assertIn("Repository.RegisterRowSerializer<FItemDefinitionRow>(&SerializeFItemDefinitionRow);", ser_cpp)

            sub_files = subsystem_generator.generate(ctx)
            self.assertEqual(len(sub_files), 2)
            sub_h, sub_cpp = sub_files[0].content, sub_files[1].content
            self.assertIn("static UStaticDataSubsystem* GetPtr(", sub_h)
            self.assertIn("static UStaticDataSubsystem& Get(", sub_h)
            self.assertIn("static TGameStatusResult<void> ExportStaticDataBin();", sub_h)
            self.assertNotIn("GetLastError()", sub_cpp)
            self.assertIn("GetError().ErrorCode", sub_cpp)
            self.assertIn("SnapshotStore.MakeReloadLatchTicket()", sub_cpp)
            self.assertIn("MakeCancelableScopeGuard", sub_cpp)
            self.assertIn("【静态数据装载耗时】", sub_cpp)

            reg_static_files = static_data_registration_generator.generate(ctx)
            self.assertEqual(len(reg_static_files), 6)
            reg_cpp = next(f for f in reg_static_files if f.path.endswith("FStaticDataRegistration.cpp")).content
            self.assertNotIn("bGlobalCommandRegistered", reg_cpp)
            self.assertIn("FAutoConsoleCommandWithWorldArgsAndOutputDevice GReloadStaticDataCommand", reg_cpp)
            self.assertIn("UStaticDataSubsystem::GetPtr(World)", reg_cpp)
            cmdlet_cpp = next(f for f in reg_static_files if f.path.endswith("Commandlet.cpp")).content
            self.assertIn("ExportStaticDataBin().IsSuccess() ? 0 : 1", cmdlet_cpp)

            game_reg_files = game_registration_generator.generate(ctx)
            self.assertEqual(len(game_reg_files), 2)
            self.assertIn("@file FDemoGameRegistration.h", game_reg_files[0].content)
            self.assertIn("@file FDemoGameRegistration.cpp", game_reg_files[1].content)

            self.assertEqual(len(query_generator.generate(ctx)), 2)
            self.assertEqual(len(service_generator.generate(ctx)), 1)
            self.assertEqual(len(interface_generator.generate(ctx)), 2)

            enum_ctx = _sample_context(tmp, row_key_mode="enum")
            enum_sd_files = static_data_generator.generate(enum_ctx)
            self.assertIn("const FItemDefinitionRow* Find(EItemDefinitionKey InKey) const noexcept", enum_sd_files[0].content)

    def test_registration_generator_windows_paths_and_extra_roots(self) -> None:
        """支持 Windows 反斜杠路径归一及 extra_roots 首轮暂存目录扫描"""
        self.assertEqual(
            registration_generator._include_path(r"C:\Proj\Plugins\Mod\Public\Generated\Registration\FTestRegistration.h"),
            "Generated/Registration/FTestRegistration.h",
        )
        with tempfile.TemporaryDirectory() as tmp:
            proj = create_mini_project(tmp)
            self.assertEqual(registration_generator.generate({"project_prefix": "Game", "registration": {}}), [])
            self.assertEqual(
                registration_generator.generate({
                    "project_prefix": "Game",
                    "registration": {"output_header": proj["reg_header"], "source_roots": []},
                }),
                [],
            )
            non_reg_h = os.path.join(tmp, "Source", "Dummy.h")
            file_writer.write_file(non_reg_h, "struct FDummy {};\n", "overwrite")
            staged_root = os.path.join(tmp, "StagedTree")
            staged_reg = os.path.join(staged_root, "Public", "Generated", "Registration", "FFirstRunRegistration.h")
            file_writer.write_file(
                staged_reg,
                "class DEMOMOD_API FFirstRunRegistration {\n"
                "public:\n"
                "    static void RegisterAll(UGameInstance* InGI);\n"
                "    static void UnregisterAll(UGameInstance* InGI);\n"
                "};\n",
                "overwrite",
            )
            raw = {
                "project_prefix": "Game",
                "registration": {
                    "output_header": proj["reg_header"],
                    "source_roots": [os.path.join(tmp, "Source"), os.path.join(tmp, "NonExistentRoot")],
                },
            }
            files = registration_generator.generate(raw, extra_roots=[staged_root])
            self.assertEqual(len(files), 1)
            self.assertIn("FFirstRunRegistration::RegisterAll(InGameInstance);", files[0].content)


if __name__ == "__main__":
    unittest.main()
