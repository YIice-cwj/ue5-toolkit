#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
命名工具、C++ 类型工具与数据表架构契约 (naming_utils / cpp_type_utils / table_schema) 单元测试
"""

import os
import tempfile
import unittest
import zipfile

import openpyxl

from test_helpers import create_xlsx
import cpp_type_utils
import naming_utils
import table_schema


class TestNamingAndCppTypeUtils(unittest.TestCase):
    """验证命名转换、精确标量初始化表达式与序列化白名单"""

    def test_strip_dt_prefix_preserves_inner_dt(self) -> None:
        """精确剥离前缀 DT_ / DT，绝不误伤表名中间的 DT 字样"""
        self.assertEqual(naming_utils.strip_dt_prefix("DT_Item_Definition"), "Item_Definition")
        self.assertEqual(naming_utils.strip_dt_prefix("DTItem"), "Item")
        self.assertEqual(naming_utils.strip_dt_prefix("DT_HUDType_Config"), "HUDType_Config")
        self.assertEqual(naming_utils.strip_dt_prefix("MyDT_Table"), "MyDT_Table")
        self.assertEqual(naming_utils.to_pascal_case("DT_HUDType_Config"), "HUDTypeConfig")

    def test_naming_conversions(self) -> None:
        """
        验证 PascalCase / snake_case / 字段名（含数字与词中前缀保护）/ UE 属性名转换
        """
        self.assertEqual(naming_utils.pascal_to_snake("ShopGoods"), "shop_goods")
        self.assertEqual(naming_utils.to_field_name("ShopGoods", "Shop"), "Goods")
        self.assertEqual(naming_utils.to_field_name("Item", "Item"), "Item")
        self.assertEqual(naming_utils.to_field_name("Item2026", "Item"), "Item2026")
        self.assertEqual(naming_utils.to_field_name("Item_2026", "Item"), "Item2026")
        self.assertEqual(naming_utils.to_field_name("Items", "Item"), "Items")
        self.assertEqual(naming_utils.to_property_name("RowName", "FName"), "RowName")
        self.assertEqual(naming_utils.to_property_name("is_active", "bool"), "bIsActive")
        self.assertEqual(naming_utils.to_property_name("bAlreadyBool", "bool"), "bAlreadyBool")
        self.assertEqual(naming_utils.to_property_name("BUpperBool", "bool"), "bUpperBool")
        self.assertEqual(naming_utils.to_property_name("max_stack", "int32"), "MaxStack")
        self.assertEqual(naming_utils.to_property_name("", "int32"), "")

    def test_clean_cpp_type(self) -> None:
        """清洗跨表外键注解语法"""
        self.assertEqual(cpp_type_utils.clean_cpp_type("FName<DT_Item>"), "FName")
        self.assertEqual(cpp_type_utils.clean_cpp_type("FName -> DT_Item"), "FName")
        self.assertEqual(cpp_type_utils.clean_cpp_type("FName[DT_Item]"), "FName")
        self.assertEqual(cpp_type_utils.clean_cpp_type(None), "FString")

    def test_default_init_expr_exact_scalar_matching(self) -> None:
        """容器与复合结构体不得误附标量零值初始化，double 使用 0.0"""
        self.assertEqual(cpp_type_utils.get_default_init_expr("float"), " = 0.0f;")
        self.assertEqual(cpp_type_utils.get_default_init_expr("double"), " = 0.0;")
        self.assertEqual(cpp_type_utils.get_default_init_expr("int32"), " = 0;")
        self.assertEqual(cpp_type_utils.get_default_init_expr("uint8"), " = 0;")
        self.assertEqual(cpp_type_utils.get_default_init_expr("bool"), " = false;")
        self.assertEqual(cpp_type_utils.get_default_init_expr("TArray<int32>"), ";")
        self.assertEqual(cpp_type_utils.get_default_init_expr("TArray<float>"), ";")
        self.assertEqual(cpp_type_utils.get_default_init_expr("TArray<bool>"), ";")
        self.assertEqual(cpp_type_utils.get_default_init_expr("TSet<int32>"), ";")
        self.assertEqual(cpp_type_utils.get_default_init_expr("FIntPoint"), ";")
        self.assertEqual(cpp_type_utils.get_default_init_expr("FIntVector"), ";")
        self.assertEqual(cpp_type_utils.get_default_init_expr("FString"), ";")

    def test_payload_mode_and_validation(self) -> None:
        """白名单覆盖 FText / TSoftObjectPtr / TSoftClassPtr / TArray / TSet / 嵌套 USTRUCT 并拒绝非法类型"""
        nested = {"FMyNested": "Nested/MyNested.h"}
        self.assertEqual(cpp_type_utils.payload_mode("FText"), "text")
        self.assertEqual(cpp_type_utils.payload_mode("TSoftObjectPtr<UTexture2D>"), "soft_object")
        self.assertEqual(cpp_type_utils.payload_mode("TSoftClassPtr<AActor>"), "soft_class")
        self.assertEqual(cpp_type_utils.payload_mode("FMyNested", nested), "struct")
        self.assertEqual(cpp_type_utils.payload_mode("TArray<int32>"), "direct")
        self.assertEqual(cpp_type_utils.payload_mode("TSet<FName>"), "direct")
        self.assertEqual(cpp_type_utils.payload_mode("FIntPoint"), "direct")

        with self.assertRaises(ValueError):
            cpp_type_utils.payload_mode("TArray<FMyNested>", nested)
        with self.assertRaises(ValueError):
            cpp_type_utils.payload_mode("TArray<UObject*>")
        with self.assertRaises(ValueError):
            cpp_type_utils.validate_field_type("UObject*", context="DT_Test.BadCol")


class TestTableSchema(unittest.TestCase):
    """验证 table_schema 标量精度、统一布尔真源、字面量归一与双端选表对称性"""

    def test_exact_scalar_conversion_and_zero_values(self) -> None:
        """FIntPoint / FIntVector 保持字符串原样不被当作整型零值或截断"""
        self.assertEqual(table_schema.get_scalar_zero_value("bool"), False)
        self.assertEqual(table_schema.get_scalar_zero_value("double"), 0.0)
        self.assertEqual(table_schema.get_scalar_zero_value("uint32"), 0)
        self.assertEqual(table_schema.get_scalar_zero_value("FIntPoint"), "")
        self.assertEqual(table_schema.get_scalar_zero_value("FIntVector"), "")

        self.assertEqual(table_schema.convert_scalar_value("(X=10,Y=20)", "FIntPoint", strict=True), "(X=10,Y=20)")
        self.assertEqual(table_schema.convert_scalar_value("3.5", "float", strict=True), 3.5)
        self.assertEqual(table_schema.convert_scalar_value("42.0", "int32", strict=True), 42)
        self.assertEqual(table_schema.convert_scalar_value("", "int32"), 0)
        self.assertEqual(table_schema.convert_scalar_value("", "float"), 0.0)

        with self.assertRaises(table_schema.SchemaValidationError):
            table_schema.convert_scalar_value("not_a_float", "float", strict=True)
        with self.assertRaises(table_schema.SchemaValidationError):
            table_schema.convert_scalar_value("not_an_int", "int32", strict=True)
        with self.assertRaises(table_schema.SchemaValidationError):
            table_schema.convert_scalar_value("3.7", "int32", strict=True)
        with self.assertRaises(table_schema.SchemaValidationError):
            table_schema.convert_scalar_value(3.7, "int32", strict=True)
        self.assertFalse(table_schema.scalar_values_equal("3.7", 4, "int32"))
        with self.assertRaises(table_schema.SchemaValidationError):
            table_schema.convert_scalar_value(["a", "b"], "FString", strict=True)
        with self.assertRaises(table_schema.SchemaValidationError):
            table_schema.convert_scalar_value("maybe", "bool", strict=True)
        with self.assertRaises(table_schema.SchemaValidationError):
            table_schema.parse_bool_value(2, strict=True)

    def test_unified_bool_equality_including_lowercase_true(self) -> None:
        """小写 'true' 与 True 语义等价，消除旧版 scalar_values_equal 漏判缺陷"""
        self.assertTrue(table_schema.scalar_values_equal("true", True, "bool"))
        self.assertTrue(table_schema.scalar_values_equal("false", False, "bool"))
        self.assertTrue(table_schema.scalar_values_equal("yes", 1, "bool"))
        self.assertTrue(table_schema.scalar_values_equal("no", 0, "bool"))
        self.assertFalse(table_schema.scalar_values_equal("true", "false", "bool"))

    def test_array_parsing_and_equality(self) -> None:
        """支持标准 JSON 数组、未加引号中括号、元组与中英文分隔符"""
        self.assertEqual(table_schema.parse_raw_array_items('["A", "B"]'), ["A", "B"])
        self.assertEqual(table_schema.parse_raw_array_items("[A, B]"), ["A", "B"])
        self.assertEqual(table_schema.parse_raw_array_items("(A, B)"), ["A", "B"])
        self.assertEqual(table_schema.parse_raw_array_items("A；B|C"), ["A", "B", "C"])
        self.assertEqual(table_schema.parse_raw_array_items(""), [])
        self.assertTrue(table_schema.values_are_equal("1, 2, 3", "[1, 2, 3]", "TArray<int32>"))
        self.assertFalse(table_schema.values_are_equal("1, 2", "[1, 2, 3]", "TArray<int32>"))

    def test_ue_literal_unwrapping(self) -> None:
        """NSLOCTEXT / LOCTEXT / (TagName=...) / None 软对象字面量正确归一"""
        self.assertEqual(
            table_schema.unwrap_text_literal('NSLOCTEXT("NS", "Key", "Hello, \\"World\\"\\nLine2")'),
            'Hello, "World"\nLine2',
        )
        self.assertEqual(table_schema.unwrap_text_literal('LOCTEXT("K", "Simple")'), "Simple")
        self.assertEqual(table_schema.unwrap_text_literal("Plain text"), "Plain text")
        self.assertEqual(table_schema.unwrap_tag_literal('(TagName="Item.Type.Weapon")'), "Item.Type.Weapon")
        self.assertEqual(table_schema.unwrap_tag_literal({"TagName": "Item.Type.Armor"}), "Item.Type.Armor")
        self.assertEqual(table_schema.unwrap_soft_object_value("None"), "")
        self.assertEqual(table_schema.unwrap_soft_object_value("/Game/Tex.Tex"), "/Game/Tex.Tex")

    def test_convert_for_json_and_excel_roundtrip(self) -> None:
        """
        验证正向导出与反向回写支持嵌套结构、空值保护、数组与标量列对象拦截
        """
        nested = {"FNested": "Nested.h"}
        self.assertEqual(table_schema.convert_for_json('{"A":1}', "FNested", nested_struct_types=nested), {"A": 1})
        self.assertEqual(table_schema.convert_for_json("", "FNested", strict=True, nested_struct_types=nested), {})
        with self.assertRaises(table_schema.SchemaValidationError):
            table_schema.convert_for_json("{bad", "FNested", strict=True, nested_struct_types=nested)
        with self.assertRaises(table_schema.SchemaValidationError):
            table_schema.convert_for_json("[1, 2]", "FNested", strict=True, nested_struct_types=nested)
        with self.assertRaises(table_schema.SchemaValidationError):
            table_schema.convert_for_json("garbage", "FNested", strict=True, nested_struct_types=nested)
        with self.assertRaises(table_schema.SchemaValidationError):
            table_schema.convert_for_excel({"bad": 1}, "int32", strict=True)
        self.assertIsNone(table_schema.convert_for_excel({"bad": 1}, "int32", fill_defaults=False, strict=False))
        self.assertIsNone(table_schema.convert_for_excel("", "int32", fill_defaults=False))
        self.assertEqual(table_schema.convert_for_excel("", "int32", fill_defaults=True), 0)
        self.assertEqual(table_schema.convert_for_excel(["a", "b"], "TArray<FString>"), '["a", "b"]')

    def test_reserved_field_names_and_header_columns(self) -> None:
        """精准拦截小写 name，放行 Name 与 RowName"""
        self.assertEqual(table_schema.find_reserved_field_names(["RowName", "name", "Name"]), [(2, "name")])
        cols = table_schema.parse_header_columns(["键", "标签", "数量"], ["RowName", "tags", "count"], ["FName", "TArray<FGameplayTag>", "int32"])
        self.assertEqual(len(cols), 3)
        self.assertTrue(cols[0].is_key)
        self.assertTrue(cols[1].is_array)
        self.assertEqual(cols[1].inner_type, "FGameplayTag")

    def test_openpyxl_and_zip_sheet_resolution_symmetry(self) -> None:
        """多工作表（首页说明页 + 次页 RowName 数据页）下 openpyxl 与 zipfile 选表完全一致"""
        with tempfile.TemporaryDirectory() as tmp:
            xlsx_path = create_xlsx(
                os.path.join(tmp, "DT_CustomFile.xlsx"),
                sheet_name="DataSheetReal",
                headers=(["主键", "数值"], ["RowName", "val"], ["FName", "int32"]),
                rows=[["Row_1", 10]],
                extra_sheets_before=[("ReadmeNotes", [["说明文档"], ["请勿改表头"]])],
            )
            wb = openpyxl.load_workbook(xlsx_path, data_only=False)
            _, openpyxl_title = table_schema.resolve_openpyxl_sheet(wb, "DT_CustomFile")
            with zipfile.ZipFile(xlsx_path, "r") as z:
                zip_title, zip_xml = table_schema.resolve_zip_worksheet(z, "DT_CustomFile")
            self.assertEqual(openpyxl_title, "DataSheetReal")
            self.assertEqual(zip_title, "DataSheetReal")
            self.assertTrue(zip_xml.endswith("sheet2.xml"))

    def test_table_schema_edge_cases_and_zip_shared_strings(self) -> None:
        """覆盖标量非严格转换、引号转义、数组/浮点比较边界及 zipfile 共享字符串与容错回退"""
        self.assertEqual(naming_utils.to_pascal_case(""), "")
        self.assertEqual(naming_utils.to_property_name("Name", "FName"), "Name")
        self.assertEqual(table_schema.extract_inner_type("FString"), "FString")
        self.assertEqual(cpp_type_utils.clean_cpp_type(""), "FString")
        self.assertEqual(cpp_type_utils.clean_cpp_type("   "), "")
        self.assertFalse(table_schema.parse_bool_value(None, strict=False))
        self.assertTrue(table_schema.parse_bool_value(2, strict=False))
        self.assertFalse(table_schema.parse_bool_value("unknown_bool", strict=False))
        self.assertEqual(table_schema.parse_raw_array_items(None), [])
        self.assertEqual(table_schema.parse_raw_array_items("   "), [])
        self.assertEqual(table_schema.parse_raw_array_items('["a", ["nested"]]'), ["a", ["nested"]])
        self.assertEqual(table_schema.unwrap_text_literal('LOCTEXT("K", "a\\\\b")'), "a\\b")
        self.assertEqual(table_schema.unwrap_text_literal('NSLOCTEXT("NS", "K")'), 'NSLOCTEXT("NS", "K")')
        self.assertIsNone(table_schema.unwrap_tag_literal(None))
        self.assertIsNone(table_schema.unwrap_soft_object_value(None))
        self.assertEqual(table_schema.convert_scalar_value("not_float", "float", strict=False), 0.0)
        self.assertEqual(table_schema.convert_scalar_value("not_int", "int32", strict=False), 0)
        self.assertEqual(table_schema.convert_scalar_value(None, "FString"), "")
        self.assertEqual(table_schema.convert_for_json({"A": 1}, "FNested", nested_struct_types={"FNested": "N.h"}), {"A": 1})
        self.assertEqual(table_schema.convert_for_json(123, "FNested", nested_struct_types={"FNested": "N.h"}), 123)
        self.assertEqual(table_schema.convert_for_json(None, "TArray<int32>"), [])
        self.assertEqual(table_schema.convert_for_json("{bad", "FNested", strict=False, nested_struct_types={"FNested": "N.h"}), "{bad")
        self.assertEqual(table_schema.convert_for_excel('(TagName="Item.A")', "FGameplayTag"), "Item.A")
        self.assertIsNone(table_schema.convert_for_excel("None", "TSoftObjectPtr<UTexture2D>"))
        self.assertEqual(table_schema.convert_for_excel("None", "TSoftObjectPtr<UTexture2D>", fill_defaults=True), "")
        self.assertEqual(table_schema.convert_for_excel([], "TArray<int32>", fill_defaults=False), "[]")
        self.assertEqual(table_schema.convert_for_excel(None, "TArray<int32>", fill_defaults=False), "")
        self.assertEqual(table_schema.convert_for_excel(["a"], "TArray<FString>"), '["a"]')
        self.assertEqual(table_schema.convert_for_excel("plain_tag", "FGameplayTag"), "plain_tag")

        self.assertTrue(table_schema.scalar_values_equal(None, "", "FString"))
        self.assertTrue(table_schema.scalar_values_equal("abc", "abc", "FString"))
        self.assertTrue(table_schema.scalar_values_equal("bad_int", "bad_int", "int32"))
        self.assertTrue(table_schema.scalar_values_equal("bad_flt", "bad_flt", "float"))
        self.assertTrue(table_schema.values_are_equal(None, "", "FString"))
        self.assertTrue(table_schema.values_are_equal("", [], "TArray<int32>"))
        self.assertTrue(table_schema.values_are_equal("Item.A", {"TagName": "Item.A"}, "FGameplayTag"))
        self.assertTrue(table_schema.values_are_equal("", "None", "TSoftObjectPtr<UTexture2D>"))

        cols = table_schema.parse_header_columns(["", "数量"], ["", "qty"], ["FName", "int32"])
        self.assertEqual(len(cols), 1)

        with tempfile.TemporaryDirectory() as tmp:
            fallback_xlsx = os.path.join(tmp, "NoHeader.xlsx")
            wb = openpyxl.Workbook()
            ws = wb.active
            ws.title = "Notes"
            ws.append(["Just notes"])
            wb.save(fallback_xlsx)
            wb2 = openpyxl.load_workbook(fallback_xlsx)
            _, title = table_schema.resolve_openpyxl_sheet(wb2, "DT_Missing")
            self.assertEqual(title, "Notes")

            shared_zip = os.path.join(tmp, "SharedStrings.xlsx")
            with zipfile.ZipFile(shared_zip, "w") as z:
                z.writestr(
                    "xl/sharedStrings.xml",
                    '<?xml version="1.0"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
                    "<si><t>RowName</t></si><si><t>Other</t></si></sst>",
                )
                z.writestr(
                    "xl/workbook.xml",
                    '<?xml version="1.0"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
                    'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">'
                    '<sheets><sheet name="Cover" sheetId="1" r:id="rId1"/><sheet name="RealData" sheetId="2" r:id="rId2"/></sheets></workbook>',
                )
                z.writestr(
                    "xl/_rels/workbook.xml.rels",
                    '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
                    '<Relationship Id="rId1" Target="worksheets/sheet1.xml"/>'
                    '<Relationship Id="rId2" Target="worksheets/sheet2.xml"/></Relationships>',
                )
                z.writestr(
                    "xl/worksheets/sheet1.xml",
                    '<?xml version="1.0"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
                    '<sheetData>'
                    '<row r="1"><c r="A1" t="s"><v>1</v></c></row>'
                    '<row r="2"><c r="A2" t="s"><v>1</v></c><c r="B2"><v>42</v></c></row>'
                    '<row r="3"><c r="A3" t="s"><v>1</v></c></row>'
                    "</sheetData></worksheet>",
                )
                z.writestr(
                    "xl/worksheets/sheet2.xml",
                    '<?xml version="1.0"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
                    '<sheetData>'
                    '<row r="1"><c r="A1" t="s"><v>1</v></c></row>'
                    '<row r="2"><c r="A2" t="s"><v>0</v></c></row>'
                    '<row r="3"><c r="A3" t="s"><v>1</v></c></row>'
                    "</sheetData></worksheet>",
                )
            with zipfile.ZipFile(shared_zip, "r") as z:
                self.assertEqual(table_schema.resolve_zip_worksheet(z, "DT_Unrelated"), ("RealData", "xl/worksheets/sheet2.xml"))
                self.assertEqual(table_schema.resolve_zip_worksheet_path(z, "DT_Unrelated"), "xl/worksheets/sheet2.xml")

            bad_zip = os.path.join(tmp, "BrokenXml.xlsx")
            with zipfile.ZipFile(bad_zip, "w") as z:
                z.writestr("xl/sharedStrings.xml", "<broken")
                z.writestr("xl/workbook.xml", "<broken")
                z.writestr("xl/_rels/workbook.xml.rels", "<broken")
            with zipfile.ZipFile(bad_zip, "r") as z:
                self.assertEqual(table_schema.resolve_zip_worksheet(z, "DT_Any"), ("", "xl/worksheets/sheet1.xml"))


if __name__ == "__main__":
    unittest.main()
