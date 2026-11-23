#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Excel 解析器、数据表规则引擎与完整性校验 (excel_parser / table_rules / validate_tables) 单元测试
"""

import json
import os
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

from test_helpers import create_mini_project, create_xlsx
import excel_parser
import naming_utils
import table_rules
import table_schema
import validate_tables


class TestExcelParserAndRules(unittest.TestCase):
    """验证多工作表真实表名读取、布尔单元格兼容、无符号整型拦截与损坏文件阻断"""

    def test_iter_xlsx_files_filters_temp_hidden_and_empty(self) -> None:
        """过滤临时 ~$ 文件、隐藏点文件与 0 字节空文件"""
        with tempfile.TemporaryDirectory() as tmp:
            valid = create_xlsx(
                os.path.join(tmp, "A", "DT_Valid.xlsx"),
                "DT_Valid",
                (["键"], ["RowName"], ["FName"]),
                [["K1"]],
            )
            for ignored in ("~$DT_Valid.xlsx", ".hidden.xlsx", "empty.xlsx"):
                with open(os.path.join(tmp, "A", ignored), "wb") as handle:
                    handle.write(b"" if ignored == "empty.xlsx" else b"junk")

            found = excel_parser.iter_xlsx_files(tmp)
            self.assertEqual(found, [valid])
            self.assertEqual(len(excel_parser.iter_xlsx_files(tmp, include_empty=True)), 2)
            self.assertEqual(excel_parser.iter_xlsx_files(os.path.join(tmp, "missing")), [])

    def test_read_sheet_grid_multi_sheet_and_bool_cells(self) -> None:
        """多工作表返回命中数据页真实名称，且含布尔单元格的数据行在 get_sheet_data 中不崩溃"""
        with tempfile.TemporaryDirectory() as tmp:
            xlsx_path = create_xlsx(
                os.path.join(tmp, "DT_Hero.xlsx"),
                sheet_name="DT_Hero",
                headers=(["主键", "显示名", "启用"], ["RowName", "title", "is_enabled"], ["FName", "FString", "bool"]),
                rows=[["Hero_01", "剑客", True], ["Hero_02", 123, False]],
                extra_sheets_before=[("Overview", [["目录页"]])],
            )
            sheet_name, grid = excel_parser.read_sheet_grid(xlsx_path)
            self.assertEqual(sheet_name, "DT_Hero")
            self.assertEqual(len(grid), 5)
            self.assertIs(grid[3][2], True)
            self.assertIs(grid[4][2], False)

            s_name, headers, row_items = excel_parser.get_sheet_data(xlsx_path)
            self.assertEqual(s_name, "DT_Hero")
            self.assertEqual(len(headers), 3)
            self.assertEqual(row_items, [("Hero_01", "剑客"), ("Hero_02", "Hero_02")])

            bool_col2_xlsx = create_xlsx(
                os.path.join(tmp, "DT_BoolSecond.xlsx"),
                sheet_name="DT_BoolSecond",
                headers=(["主键", "启用"], ["RowName", "is_enabled"], ["FName", "bool"]),
                rows=[["Flag_01", True], ["Flag_02", False]],
            )
            _, _, bool_items = excel_parser.get_sheet_data(bool_col2_xlsx)
            self.assertEqual(bool_items, [("Flag_01", "Flag_01"), ("Flag_02", "Flag_02")])

    def test_scalar_type_validation_all_integers_unsigned_and_nan(self) -> None:
        """覆盖全系列 8 种整型、无符号负数拦截、NaN/Inf 拦截与统一布尔字面量"""
        for signed in ("int8", "int16", "int32", "int64", "int"):
            self.assertIsNone(table_rules.scalar_type_reason(signed, "-42"))
            self.assertIsNotNone(table_rules.scalar_type_reason(signed, "3.14"))
        for unsigned in ("uint8", "uint16", "uint32", "uint64", "byte"):
            self.assertIsNone(table_rules.scalar_type_reason(unsigned, "42"))
            self.assertIsNotNone(table_rules.scalar_type_reason(unsigned, "-1"))
        self.assertIsNotNone(table_rules.scalar_type_reason("uint8", "256"))
        self.assertIsNotNone(table_rules.scalar_type_reason("int8", "128"))
        self.assertIsNotNone(table_rules.scalar_type_reason("int8", "-129"))
        for flt in ("float", "double"):
            self.assertIsNone(table_rules.scalar_type_reason(flt, "1.25"))
            self.assertIsNotNone(table_rules.scalar_type_reason(flt, "NaN"))
            self.assertIsNotNone(table_rules.scalar_type_reason(flt, "inf"))
            self.assertIsNotNone(table_rules.scalar_type_reason(flt, "abc"))
        self.assertIsNone(table_rules.scalar_type_reason("bool", "true"))
        self.assertIsNone(table_rules.scalar_type_reason("bool", "0"))
        self.assertIsNotNone(table_rules.scalar_type_reason("bool", "not_bool"))

        large_int64 = "9007199254740993"
        self.assertEqual(table_schema.convert_scalar_value(large_int64, "int64", strict=True), 9007199254740993)
        self.assertFalse(table_schema.scalar_values_equal("9007199254740992", "9007199254740993", "int64"))
        with self.assertRaises(table_schema.SchemaValidationError):
            table_schema.convert_scalar_value("NaN", "float", strict=True)
        with self.assertRaises(table_schema.SchemaValidationError):
            table_schema.convert_scalar_value("Inf", "double", strict=True)
        with self.assertRaises(table_schema.SchemaValidationError):
            table_schema.convert_scalar_value("-1", "uint32", strict=True)
        with self.assertRaises(table_schema.SchemaValidationError):
            table_schema.convert_scalar_value("300", "uint8", strict=True)
        self.assertEqual(table_schema.convert_for_excel({"A": 1}, "FNestedBonus"), '{"A": 1}')
        self.assertTrue(table_schema.scalar_values_equal({"A": 1}, '{"A": 1}', "FNestedBonus"))
        self.assertEqual(naming_utils.to_property_name("DTValue", "int32"), "DTValue")
        self.assertEqual(naming_utils.to_property_name("BGMUnlocked", "bool"), "bBGMUnlocked")
        self.assertEqual(naming_utils.to_property_name("bAlreadyBool", "bool"), "bAlreadyBool")

    def test_discover_and_check_references_all_tiers(self) -> None:
        """验证三级跨表外键发现（显式注解 / 强类型枚举 / 描述标记 / 外置规则 / 前缀推导 / 语义推导）与悬空报错"""
        with tempfile.TemporaryDirectory() as tmp:
            create_xlsx(
                os.path.join(tmp, "Item", "DT_Item.xlsx"),
                "DT_Item",
                (["主键", "价格"], ["RowName", "price"], ["FName", "uint32"]),
                [["Item_01", 100]],
            )
            create_xlsx(
                os.path.join(tmp, "Shop", "DT_ShopGoods.xlsx"),
                "DT_ShopGoods",
                (
                    ["主键", "物品引用", "枚举引用", "注解引用[FK:DT_Item]", "外置引用", "前缀引用"],
                    ["RowName", "item_ref", "enum_ref", "desc_ref", "ext_ref", "display_item_id"],
                    ["FName", "FName<DT_Item>", "EItemKey", "FName", "FName", "FName"],
                ),
                [["Goods_01", "Item_01", "Item_01", "Item_01", "Item_01", "Item_01"]],
            )
            fk_file = os.path.join(tmp, "foreign_keys.json")
            with open(fk_file, "w", encoding="utf-8") as handle:
                json.dump([{"source_sheet": "DT_ShopGoods", "source_field": "ext_ref", "target_sheet": "DT_Item"}], handle)

            self.assertEqual(validate_tables.validate_directory(tmp, fk_file), 0)
            self.assertEqual(validate_tables.validate_directory(tmp, fk_file, nested_struct_types={}), 0)

            create_xlsx(
                os.path.join(tmp, "Shop", "DT_ShopGoods.xlsx"),
                "DT_ShopGoods",
                (["主键", "物品引用", "数量"], ["RowName", "item_ref", "qty"], ["FName", "FName<DT_Item>", "uint32"]),
                [["Goods_01", "Item_MISSING", -5], ["Goods_01", "Item_01", 1], ["Bad Key!", "Item_01", 1]],
            )
            err_count = validate_tables.validate_directory(tmp)
            self.assertGreaterEqual(err_count, 4)

    def test_validate_directory_fails_on_corrupted_xlsx_and_reserved_name(self) -> None:
        """损坏的 .xlsx 文件与禁用字段名 'name' 均必须计入 error_count 阻断管线"""
        with tempfile.TemporaryDirectory() as tmp:
            corrupt_path = os.path.join(tmp, "DT_Corrupt.xlsx")
            with open(corrupt_path, "wb") as handle:
                handle.write(b"NOT_A_VALID_ZIP_FILE")
            self.assertEqual(validate_tables.validate_directory(tmp), 1)

            os.remove(corrupt_path)
            create_xlsx(
                os.path.join(tmp, "DT_Reserved.xlsx"),
                "DT_Reserved",
                (["主键", "非法名"], ["RowName", "name"], ["FName", "FString"]),
                [["Row_1", "abc"]],
            )
            self.assertEqual(validate_tables.validate_directory(tmp), 1)
            os.remove(os.path.join(tmp, "DT_Reserved.xlsx"))

            create_xlsx(
                os.path.join(tmp, "DT_BadKeyCol_And_Props.xlsx"),
                "DT_BadKeyCol",
                (["主键", "属性A", "属性B", "坏类型"], ["ID", "is_active", "b_is_active", "bad_ptr"], ["FName", "bool", "bool", "UObject*"]),
                [["Row_1", True, False, "None"]],
            )
            self.assertGreaterEqual(validate_tables.validate_directory(tmp), 3)

    def test_parser_shared_strings_empty_sheets_and_validate_cli_branches(self) -> None:
        """覆盖 sharedStrings XML、空表、缺失主键、语义外键推断、目标表缺失告警及 validate_tables CLI"""
        self.assertEqual(excel_parser.parse_cell_ref("12345"), (0, 0))
        with tempfile.TemporaryDirectory() as tmp:
            shared_xlsx = os.path.join(tmp, "DT_Shared.xlsx")
            with zipfile.ZipFile(shared_xlsx, "w") as z:
                z.writestr(
                    "xl/sharedStrings.xml",
                    '<?xml version="1.0"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
                    "<si><t>主键</t></si><si><t>RowName</t></si><si><t>FName</t></si><si><t>K1</t></si></sst>",
                )
                z.writestr(
                    "xl/worksheets/sheet1.xml",
                    '<?xml version="1.0"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
                    '<sheetData>'
                    '<row r="1"><c r="A1" t="s"><v>0</v></c></row>'
                    '<row r="2"><c r="A2" t="s"><v>1</v></c></row>'
                    '<row r="3"><c r="A3" t="s"><v>2</v></c></row>'
                    '<row r="4"><c r="A4" t="s"><v>3</v></c></row>'
                    '<row r="5"><c r="A5" t="s"><v>99</v></c></row>'
                    "</sheetData></worksheet>",
                )
            sname, grid = excel_parser.read_sheet_grid(shared_xlsx)
            self.assertEqual(sname, "")
            self.assertEqual(grid[3][0], "K1")
            self.assertEqual(grid[4][0], "")
            _, headers, items = excel_parser.get_sheet_data(shared_xlsx)
            self.assertEqual(len(headers), 3)
            self.assertEqual(items, [("K1", "K1")])

            sparse_xlsx = os.path.join(tmp, "DT_SparseFormula.xlsx")
            with zipfile.ZipFile(sparse_xlsx, "w") as z:
                z.writestr(
                    "xl/worksheets/sheet1.xml",
                    '<?xml version="1.0"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
                    "<sheetData>"
                    '<row r="2"><c t="inlineStr"><is><t>RowName</t></is></c><c t="str"><v>formula_col</v></c></row>'
                    '<row r="3"><c t="inlineStr"><is><t>FName</t></is></c><c t="inlineStr"><is><t>FString</t></is></c></row>'
                    '<row r="4"><c t="inlineStr"><is><t>Row_1</t></is></c><c t="str"><f>"A"&amp;"B"</f><v>AB</v></c></row>'
                    "</sheetData></worksheet>",
                )
            _, sparse_grid = excel_parser.read_sheet_grid(sparse_xlsx)
            self.assertEqual(len(sparse_grid), 4)
            self.assertEqual(sparse_grid[1], ["RowName", "formula_col"])
            self.assertEqual(sparse_grid[3], ["Row_1", "AB"])

            no_sheet = os.path.join(tmp, "DT_NoSheet.xlsx")
            with zipfile.ZipFile(no_sheet, "w") as z:
                z.writestr("dummy.txt", "x")
            self.assertEqual(excel_parser.read_sheet_grid(no_sheet)[1], [])
            self.assertEqual(excel_parser.get_sheet_data(no_sheet)[1], [])

            empty_xml = os.path.join(tmp, "DT_EmptyXml.xlsx")
            with zipfile.ZipFile(empty_xml, "w") as z:
                z.writestr(
                    "xl/worksheets/sheet1.xml",
                    '<?xml version="1.0"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData/></worksheet>',
                )
            self.assertEqual(excel_parser.read_sheet_grid(empty_xml)[1], [])

            empty_dir = os.path.join(tmp, "EmptyTables")
            os.makedirs(empty_dir, exist_ok=True)
            self.assertEqual(validate_tables.validate_directory(empty_dir, os.path.join(tmp, "missing_fk.json")), 0)

            bad_fk = os.path.join(tmp, "bad_fk.json")
            with open(bad_fk, "w", encoding="utf-8") as handle:
                handle.write("{bad json")
            self.assertEqual(validate_tables.validate_directory(empty_dir, bad_fk), 0)

            dt_dir = os.path.join(tmp, "SemanticDT")
            create_xlsx(
                os.path.join(dt_dir, "DT_Short.xlsx"),
                "DT_Short",
                (["仅一行"], [], []),
                rows=[],
            )
            create_xlsx(
                os.path.join(dt_dir, "DT_Item.xlsx"),
                "DT_Item",
                (["主键", "名字"], ["RowName", "title"], ["FName", "FString"]),
                [["Sword_01", "铁剑"]],
            )
            create_xlsx(
                os.path.join(dt_dir, "DT_Shop_Goods.xlsx"),
                "DT_Shop_Goods",
                (["主键", "小整型数组"], ["RowName", "small_nums"], ["FName", "TArray<uint8>"]),
                [["Goods_01", "[10, 999]"]],
            )
            create_xlsx(
                os.path.join(dt_dir, "DT_Quest.xlsx"),
                "DT_Quest",
                (
                    ["主键", "语义物品", "未知引用", "多词枚举外键", "数组外键"],
                    ["RowName", "reward_item_id", "ghost_ref", "goods_enum", "item_list"],
                    ["FName", "FName", "FName<DT_Ghost>", "EShopGoodsKey", "TArray<FName><DT_Item>"],
                ),
                [["Quest_1", "Sword_01", "Ghost_1", "Goods_01", '["Sword_01", "Missing_Sword"]'], ["", "Sword_01", "Ghost_1", "Goods_01", "[]"]],
            )
            self.assertEqual(validate_tables.validate_directory(dt_dir), 3)

            proj = create_mini_project(tmp)
            create_xlsx(
                os.path.join(proj["data_tables"], "DT_Ok.xlsx"),
                "DT_Ok",
                (["主键", "数量"], ["RowName", "qty"], ["FName", "int32"]),
                [["Ok_1", 5]],
            )
            with mock.patch.object(sys, "argv", ["validate_tables.py", "--config", proj["config"]]):
                validate_tables.main()

            create_xlsx(
                os.path.join(proj["data_tables"], "DT_Bad.xlsx"),
                "DT_Bad",
                (["主键", "数量"], ["RowName", "qty"], ["FName", "uint32"]),
                [["Bad_1", -9]],
            )
            with mock.patch.object(sys, "argv", ["validate_tables.py", "--config", proj["config"]]):
                with self.assertRaises(SystemExit) as cm:
                    validate_tables.main()
                self.assertEqual(cm.exception.code, 1)


if __name__ == "__main__":
    unittest.main()
