#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
端到端管线脚本 (excel_to_json / gameplay_tags_to_ini / json_to_excel / roundtrip_check / main / run_pipeline) 单元测试
"""

import argparse
import json
import os
import sys
import tempfile
import unittest
from typing import Any
from unittest import mock

import openpyxl

from test_helpers import create_mini_project, create_xlsx
import excel_to_json
import gameplay_tags_to_ini
import json_to_excel
import main as codegen_main
import roundtrip_check
import run_pipeline


class TestExcelToJsonAndTags(unittest.TestCase):
    """验证 JSON 导出空表保留、异常保旧、孤儿清理以及 GameplayTags 数组/字面量解析"""

    def test_excel_to_json_exports_valid_empty_table_and_protects_on_error(self) -> None:
        """合法空表（仅 3 行表头）导出 []；解析异常返回 False 并保护既有 JSON 不被孤儿清理误删"""
        with tempfile.TemporaryDirectory() as tmp:
            in_dir = os.path.join(tmp, "DT")
            out_dir = os.path.join(tmp, "Json")
            create_xlsx(
                os.path.join(in_dir, "Item", "DT_EmptyValid.xlsx"),
                "DT_EmptyValid",
                (["主键", "数量"], ["RowName", "count"], ["FName", "int32"]),
                rows=[],
            )
            create_xlsx(
                os.path.join(in_dir, "Item", "DT_Normal.xlsx"),
                "DT_Normal",
                (["主键", "数量"], ["RowName", "count"], ["FName", "int32"]),
                rows=[["Item_1", 7]],
            )

            self.assertTrue(excel_to_json.export_all(in_dir, out_dir))
            empty_json = os.path.join(out_dir, "Item", "DT_EmptyValid.json")
            normal_json = os.path.join(out_dir, "Item", "DT_Normal.json")
            self.assertTrue(os.path.isfile(empty_json))
            with open(empty_json, "r", encoding="utf-8") as handle:
                self.assertEqual(json.load(handle), [])

            with open(os.path.join(in_dir, "Item", "DT_Normal.xlsx"), "wb") as handle:
                handle.write(b"CORRUPTED_ZIP")
            self.assertFalse(excel_to_json.export_all(in_dir, out_dir))
            self.assertTrue(os.path.isfile(normal_json))

            os.remove(os.path.join(in_dir, "Item", "DT_Normal.xlsx"))
            self.assertTrue(excel_to_json.export_all(in_dir, out_dir))
            self.assertFalse(os.path.isfile(normal_json))

    def test_gameplay_tags_parses_json_arrays_and_ue_literals(self) -> None:
        """正确解析 TArray<FGameplayTag> JSON 数组与 (TagName=...) 字面量，不产出带括号/引号脏标签"""
        with tempfile.TemporaryDirectory() as tmp:
            in_dir = os.path.join(tmp, "DT")
            ini_path = os.path.join(tmp, "Config", "DefaultGameplayTags.ini")
            create_xlsx(
                os.path.join(in_dir, "DT_Tags.xlsx"),
                "DT_Tags",
                (["主键", "单标签", "多标签"], ["RowName", "single_tag", "multi_tags"], ["FName", "FGameplayTag", "TArray<FGameplayTag>"]),
                rows=[
                    ["R1", '(TagName="Item.Type.Weapon")', '["Item.Quality.Rare", "Item.Bind.Pickup"]'],
                    ["R2", "Item.Type.Armor", "Item.Slot.Head, Item.Slot.Body"],
                ],
            )
            cfg = {"data_tables_source_dir": in_dir, "gameplay_tags_ini_path": ini_path}
            self.assertTrue(gameplay_tags_to_ini.export_tags_ini(cfg))
            with open(ini_path, "r", encoding="utf-8") as handle:
                content = handle.read()
            for expected in ("Item.Type.Weapon", "Item.Quality.Rare", "Item.Bind.Pickup", "Item.Type.Armor", "Item.Slot.Head", "Item.Slot.Body"):
                self.assertIn(f'+GameplayTagList=(Tag="{expected}")', content)
            self.assertNotIn("[", content.split("GameplayTagsSettings]", 1)[1])

            with open(os.path.join(in_dir, "DT_Broken.xlsx"), "wb") as handle:
                handle.write(b"BAD_ZIP")
            self.assertFalse(gameplay_tags_to_ini.export_tags_ini(cfg))

    def test_excel_to_json_and_tags_cli_and_empty_cleanup(self) -> None:
        """覆盖 excel_to_json 与 gameplay_tags_to_ini 的 CLI 入口、空目录清理与短表跳过"""
        with tempfile.TemporaryDirectory() as tmp:
            proj = create_mini_project(tmp)
            dt_dir = proj["data_tables"]
            create_xlsx(
                os.path.join(dt_dir, "Item", "DT_Short.xlsx"),
                "DT_Short",
                (["仅标题"], [], []),
                rows=[],
            )
            create_xlsx(
                os.path.join(dt_dir, "Item", "DT_Ok.xlsx"),
                "DT_Ok",
                (["主键", "标签", "留空列"], ["RowName", "tag", ""], ["FName", "FGameplayTag", "FString"]),
                rows=[["R1", "Item.Tag.A", "x"], ["", "Item.Ignored", "y"]],
            )
            with mock.patch.object(sys, "argv", ["excel_to_json.py", "--config", proj["config"]]):
                with self.assertRaises(SystemExit) as cm:
                    excel_to_json.main()
                self.assertEqual(cm.exception.code, 0)

            collected_tags = gameplay_tags_to_ini.collect_tags(dt_dir)
            self.assertIn("Item.Tag.A", collected_tags)
            self.assertNotIn("Item.Ignored", collected_tags)
            with mock.patch.object(sys, "argv", ["gameplay_tags_to_ini.py", "--config", proj["config"]]):
                with self.assertRaises(SystemExit) as cm2:
                    gameplay_tags_to_ini.main()
                self.assertEqual(cm2.exception.code, 0)

            self.assertTrue(gameplay_tags_to_ini.export_tags_ini({"data_tables_source_dir": dt_dir, "gameplay_tags_ini_path": ""}))

            empty_dt = os.path.join(tmp, "EmptyDT")
            os.makedirs(empty_dt, exist_ok=True)
            stale_ini = os.path.join(tmp, "Config", "DefaultGameplayTags.ini")
            self.assertTrue(os.path.isfile(stale_ini))
            self.assertTrue(gameplay_tags_to_ini.export_tags_ini({"data_tables_source_dir": empty_dt, "gameplay_tags_ini_path": stale_ini}))
            self.assertFalse(os.path.isfile(stale_ini))
            self.assertTrue(gameplay_tags_to_ini.export_tags_ini({"data_tables_source_dir": empty_dt, "gameplay_tags_ini_path": stale_ini}))
            self.assertEqual(excel_to_json._cleanup_orphan_json(os.path.join(tmp, "MissingOut"), set()), 0)

            with mock.patch.object(gameplay_tags_to_ini, "_write_text_atomic", side_effect=OSError("ro")):
                self.assertFalse(gameplay_tags_to_ini.export_tags_ini({"data_tables_source_dir": dt_dir, "gameplay_tags_ini_path": stale_ini}))


class TestJsonToExcelAndRoundtrip(unittest.TestCase):
    """验证反向回写、假值键 0、防秒冲突快照、SHA-1 校验回滚、公式保护与往返自检"""

    def test_sync_falsy_zero_key_formula_shield_backup_and_sha1_rollback(self) -> None:
        """覆盖键 0 兼容、公式盾牌、空表零误报漂移、同秒双快照防覆盖与 SHA-1 篡改拦截"""
        with tempfile.TemporaryDirectory() as tmp:
            excel_dir = os.path.join(tmp, "DT")
            json_dir = os.path.join(tmp, "Json")
            backup_dir = os.path.join(tmp, "Backup")
            os.makedirs(json_dir, exist_ok=True)

            xlsx_path = create_xlsx(
                os.path.join(excel_dir, "DT_Test.xlsx"),
                "DT_Test",
                (["主键", "数量", "公式列", "名称"], ["RowName", "qty", "calc", "title"], ["FName", "int32", "int32", "FText"]),
                rows=[["0", 10, "=B4*2", "旧名"], ["Item_Keep", 5, 10, "保留"]],
            )
            json_path = os.path.join(json_dir, "DT_Test.json")
            with open(json_path, "w", encoding="utf-8") as handle:
                json.dump([
                    {"RowName": 0, "Qty": 99, "Calc": 777, "Title": 'NSLOCTEXT("NS", "K", "新名")'},
                    {"RowName": "Item_Keep", "Qty": 5, "Calc": 10, "Title": "保留"},
                    {"RowName": "Item_Added", "Qty": 12, "Calc": 24, "Title": "新增"},
                ], handle, ensure_ascii=False)

            options = argparse.Namespace(
                dry_run=False, table=None, allow_delete=False,
                force_formula=False, fill_defaults=False,
                float_epsilon=1e-6, no_backup=False, strict=False,
                protect_formulas_config=True,
            )
            session1 = json_to_excel.BackupSession(backup_dir, excel_dir, max_backups=5)
            res = json_to_excel.perform_sync_for_table(xlsx_path, json_path, options, session1)
            snap1 = session1.finalize()
            self.assertIsNone(res.error)
            self.assertTrue(res.has_changes)
            self.assertIsNotNone(snap1)
            self.assertGreaterEqual(res.text_literal_count, 1)

            wb = openpyxl.load_workbook(xlsx_path, data_only=False)
            ws = wb["DT_Test"]
            self.assertEqual(ws.cell(row=4, column=2).value, 99)
            self.assertEqual(ws.cell(row=4, column=3).value, "=B4*2")
            self.assertEqual(ws.cell(row=4, column=4).value, "新名")
            self.assertEqual(ws.cell(row=6, column=1).value, "Item_Added")

            session2 = json_to_excel.BackupSession(backup_dir, excel_dir, max_backups=5)
            session2.backup(xlsx_path)
            snap2 = session2.finalize()
            self.assertNotEqual(snap1, snap2)

            ok, _msg = json_to_excel.perform_rollback(backup_dir, snapshot=snap1, expected_excel_dir=excel_dir)
            self.assertTrue(ok)
            wb_restored = openpyxl.load_workbook(xlsx_path, data_only=False)
            self.assertEqual(wb_restored["DT_Test"].cell(row=4, column=2).value, 10)

            ok_mismatch_dir, msg_mismatch_dir = json_to_excel.perform_rollback(
                backup_dir,
                snapshot=snap1,
                expected_excel_dir=os.path.join(tmp, "OtherExcelDir"),
            )
            self.assertFalse(ok_mismatch_dir)
            self.assertIn("不一致", msg_mismatch_dir)

            ok_no_table, msg_no_table = json_to_excel.perform_rollback(backup_dir, snapshot=snap1, table="DT_NoSuchTable")
            self.assertFalse(ok_no_table)
            self.assertIn("未找到可还原", msg_no_table)

            empty_session = json_to_excel.BackupSession(backup_dir, excel_dir, max_backups=5)
            empty_session.snapshot_dir = empty_session._allocate_snapshot_dir()
            alloc_dir = empty_session.snapshot_dir
            self.assertIsNone(empty_session.finalize())
            self.assertFalse(os.path.exists(alloc_dir))

            backed_file = os.path.join(snap1, "DT_Test.xlsx")
            with open(backed_file, "ab") as handle:
                handle.write(b"TAMPER")
            ok_tampered, msg_tampered = json_to_excel.perform_rollback(backup_dir, snapshot=snap1)
            self.assertFalse(ok_tampered)
            self.assertIn("SHA-1", msg_tampered)

            traversal_snap = os.path.join(backup_dir, "20260101_120000")
            os.makedirs(traversal_snap, exist_ok=True)
            with open(os.path.join(traversal_snap, json_to_excel.MANIFEST_FILENAME), "w", encoding="utf-8") as handle:
                json.dump({
                    "timestamp": "20260101_120000",
                    "excel_dir": excel_dir,
                    "entries": [{"rel_path": "../Escape.xlsx", "sha1": "deadbeef"}],
                }, handle)
            ok_trav, msg_trav = json_to_excel.perform_rollback(backup_dir, snapshot=traversal_snap)
            self.assertFalse(ok_trav)
            self.assertIn("越界", msg_trav)

            with open(json_path, "w", encoding="utf-8") as handle:
                json.dump([], handle)
            empty_res = json_to_excel.perform_sync_for_table(xlsx_path, json_path, options, backup_session=None)
            self.assertEqual(empty_res.drift_issues, [])

    def test_roundtrip_check_pass_and_cleanup(self) -> None:
        """往返自检端到端通过，空目录失败时亦不泄漏临时目录"""
        with tempfile.TemporaryDirectory() as tmp:
            dt_dir = os.path.join(tmp, "DT")
            create_xlsx(
                os.path.join(dt_dir, "Item", "DT_Item.xlsx"),
                "DT_Item",
                (["主键", "数量", "权重", "启用", "标签"], ["RowName", "qty", "weight", "active", "tags"], ["FName", "int32", "float", "bool", "TArray<FString>"]),
                rows=[["Item_1", 10, 1.5, True, '["A", "B"]']],
            )
            self.assertEqual(roundtrip_check.run_roundtrip(dt_dir, {}, no_write=False), 0)
            self.assertEqual(roundtrip_check.run_roundtrip(dt_dir, {}, no_write=True), 0)

            empty_dir = os.path.join(tmp, "EmptyDT")
            os.makedirs(empty_dir, exist_ok=True)
            self.assertEqual(roundtrip_check.run_roundtrip(empty_dir, {}), 1)

    def test_json_to_excel_sync_all_strict_delete_rotation_and_cli(self) -> None:
        """覆盖 json_to_excel.sync_all、删行公式拦截、结构漂移、类型转换失败、备份轮转、部分回滚与 CLI"""
        with tempfile.TemporaryDirectory() as tmp:
            proj = create_mini_project(tmp)
            excel_dir = proj["data_tables"]
            json_dir = proj["json_dir"]
            os.makedirs(json_dir, exist_ok=True)

            xlsx_item = create_xlsx(
                os.path.join(excel_dir, "Item", "DT_Item.xlsx"),
                "DT_Item",
                (["主键", "数量", "公式"], ["RowName", "qty", "calc"], ["FName", "int32", "int32"]),
                rows=[["I1", 10, "=B4*2"], ["I2", 20, 40]],
                extra_sheets_before=[("Notes", [["说明"]])],
            )
            wb = openpyxl.load_workbook(xlsx_item)
            wb["DT_Item"].cell(row=4, column=2).number_format = "#,##0"
            wb.save(xlsx_item)

            json_item = os.path.join(json_dir, "Item", "DT_Item.json")
            os.makedirs(os.path.dirname(json_item), exist_ok=True)
            with open(json_item, "w", encoding="utf-8") as handle:
                json.dump([
                    {"Name": "I1", "Qty": None, "ExtraField": 1},
                    {"RowName": "I3", "Qty": 30, "Calc": 60},
                ], handle, ensure_ascii=False)

            with mock.patch.object(sys, "argv", ["json_to_excel.py", "--config", proj["config"], "--dry-run"]):
                with self.assertRaises(SystemExit) as cm_dry:
                    json_to_excel.main()
                self.assertEqual(cm_dry.exception.code, 0)

            with mock.patch.object(
                sys,
                "argv",
                ["json_to_excel.py", "--config", proj["config"], "--allow-delete", "--strict", "--table", "DT_Item"],
            ):
                with self.assertRaises(SystemExit) as cm_strict:
                    json_to_excel.main()
                self.assertEqual(cm_strict.exception.code, 1)

            with open(json_item, "w", encoding="utf-8") as handle:
                json.dump([
                    {"RowName": "I1", "Qty": 15, "Calc": 30},
                    {"RowName": "I3", "Qty": 30, "Calc": 60},
                ], handle, ensure_ascii=False)
            with mock.patch.object(
                sys,
                "argv",
                ["json_to_excel.py", "--config", proj["config"], "--allow-delete", "--strict", "--table", "DT_Item"],
            ):
                with self.assertRaises(SystemExit) as cm_formula_strict:
                    json_to_excel.main()
                self.assertEqual(cm_formula_strict.exception.code, 1)

            with open(proj["config"], "r", encoding="utf-8") as handle:
                cfg_data = json.load(handle)
            cfg_data["max_backups"] = 1
            cfg_data.setdefault("back_write", {})["max_backups"] = 1
            with open(proj["config"], "w", encoding="utf-8") as handle:
                json.dump(cfg_data, handle)

            with mock.patch.object(
                sys,
                "argv",
                ["json_to_excel.py", "--config", proj["config"], "--allow-delete", "--force-formula-overwrite"],
            ):
                with self.assertRaises(SystemExit) as cm_write:
                    json_to_excel.main()
                self.assertEqual(cm_write.exception.code, 0)

            with open(json_item, "w", encoding="utf-8") as handle:
                json.dump([{"RowName": "I1", "Qty": 16, "Calc": 32}], handle, ensure_ascii=False)
            with mock.patch.object(
                sys,
                "argv",
                ["json_to_excel.py", "--config", proj["config"], "--allow-delete"],
            ):
                with self.assertRaises(SystemExit) as cm_rotate:
                    json_to_excel.main()
                self.assertEqual(cm_rotate.exception.code, 0)

            with mock.patch.object(sys, "argv", ["json_to_excel.py", "--config", proj["config"], "--rollback", "--table", "Item"]):
                with self.assertRaises(SystemExit) as cm_rb:
                    json_to_excel.main()
                self.assertEqual(cm_rb.exception.code, 0)

            with mock.patch.object(sys, "argv", ["json_to_excel.py", "--config", proj["config"], "--table", "DT_NonExistentTable"]):
                with self.assertRaises(SystemExit) as cm_no_match:
                    json_to_excel.main()
                self.assertEqual(cm_no_match.exception.code, 1)

            with mock.patch.object(sys, "argv", ["json_to_excel.py", "--config", proj["config"], "--no-backup"]):
                with self.assertRaises(SystemExit) as cm_nb:
                    json_to_excel.main()
                self.assertEqual(cm_nb.exception.code, 0)

            bad_json = os.path.join(json_dir, "Item", "DT_BadJson.json")
            with open(bad_json, "w", encoding="utf-8") as handle:
                handle.write("{corrupt")
            bad_xlsx = create_xlsx(
                os.path.join(excel_dir, "Item", "DT_BadJson.xlsx"),
                "DT_BadJson",
                (["主键"], ["RowName"], ["FName"]),
                rows=[["K1"]],
            )
            opts = argparse.Namespace(
                dry_run=False, table=None, allow_delete=False, force_formula=False,
                fill_defaults=False, float_epsilon=1e-6, no_backup=True, strict=True,
                protect_formulas_config=True,
            )
            self.assertIsNotNone(json_to_excel.perform_sync_for_table(bad_xlsx, bad_json, opts, None).error)

            with open(bad_json, "w", encoding="utf-8") as handle:
                json.dump({"not": "a_list"}, handle)
            self.assertIsNotNone(json_to_excel.perform_sync_for_table(bad_xlsx, bad_json, opts, None).error)

            with open(bad_json, "w", encoding="utf-8") as handle:
                json.dump([{"RowName": "K1"}], handle)
            with open(bad_xlsx, "wb") as handle:
                handle.write(b"BAD_ZIP")
            self.assertIsNotNone(json_to_excel.perform_sync_for_table(bad_xlsx, bad_json, opts, None).error)

            short_xlsx = create_xlsx(
                os.path.join(excel_dir, "Item", "DT_Short.xlsx"),
                "DT_Short",
                (["仅标题"], [], []),
                rows=[],
            )
            self.assertIsNotNone(json_to_excel.perform_sync_for_table(short_xlsx, bad_json, opts, None).error)

            ok_rb, _ = json_to_excel.perform_rollback(os.path.join(tmp, "NoBackup"))
            self.assertFalse(ok_rb)
            empty_snap = os.path.join(tmp, "EmptySnap")
            os.makedirs(empty_snap, exist_ok=True)
            self.assertFalse(json_to_excel.perform_rollback(tmp, snapshot=empty_snap)[0])
            with open(os.path.join(empty_snap, json_to_excel.MANIFEST_FILENAME), "w", encoding="utf-8") as handle:
                handle.write("{bad")
            self.assertFalse(json_to_excel.perform_rollback(tmp, snapshot=empty_snap)[0])
            with open(os.path.join(empty_snap, json_to_excel.MANIFEST_FILENAME), "w", encoding="utf-8") as handle:
                json.dump({"files": []}, handle)
            self.assertFalse(json_to_excel.perform_rollback(tmp, snapshot=empty_snap)[0])

    def test_roundtrip_diff_and_cli_branches(self) -> None:
        """覆盖 roundtrip_check 差异比对、keep_temp、错误传播与 main CLI"""
        with tempfile.TemporaryDirectory() as tmp:
            dir_a = os.path.join(tmp, "A")
            dir_b = os.path.join(tmp, "B")
            os.makedirs(dir_a, exist_ok=True)
            os.makedirs(dir_b, exist_ok=True)
            with open(os.path.join(dir_a, "only_a.json"), "w", encoding="utf-8") as handle:
                handle.write("[]")
            with open(os.path.join(dir_b, "only_b.json"), "w", encoding="utf-8") as handle:
                handle.write("[]")
            with open(os.path.join(dir_a, "same.json"), "w", encoding="utf-8") as handle:
                handle.write('[{"A": 1}]')
            with open(os.path.join(dir_b, "same.json"), "w", encoding="utf-8") as handle:
                handle.write('[{"A": 2}]')
            same_flag, diffs = roundtrip_check.compare_json_dirs(dir_a, dir_b)
            self.assertFalse(same_flag)
            self.assertTrue(any("only_a.json" in d for d in diffs))
            self.assertTrue(any("only_b.json" in d for d in diffs))
            self.assertTrue(any("same.json" in d for d in diffs))

            proj = create_mini_project(tmp)
            create_xlsx(
                os.path.join(proj["data_tables"], "DT_RT.xlsx"),
                "DT_RT",
                (["主键", "数量"], ["RowName", "qty"], ["FName", "int32"]),
                rows=[["R1", 5]],
            )
            with mock.patch.object(sys, "argv", ["roundtrip_check.py", "--config", proj["config"], "--no-write"]):
                self.assertEqual(roundtrip_check.main(), 0)

            with mock.patch.object(roundtrip_check.excel_to_json, "export_all", return_value=False):
                self.assertEqual(roundtrip_check.run_roundtrip(proj["data_tables"], {}), 1)
            with mock.patch.object(roundtrip_check, "compare_json_dirs", return_value=(False, ["内容不一致: DT_RT.json"])):
                self.assertEqual(roundtrip_check.run_roundtrip(proj["data_tables"], {}, no_write=True), 1)
                self.assertEqual(roundtrip_check.run_roundtrip(proj["data_tables"], {}, no_write=False), 1)
            with mock.patch.object(roundtrip_check, "sync_excel_copy", return_value=(1, 0, 1, ["mock error"])):
                self.assertEqual(roundtrip_check.run_roundtrip(proj["data_tables"], {}, no_write=False), 1)


class TestMainAndPipeline(unittest.TestCase):
    """验证 main.py 首轮洁净生成、分类清理与 run_pipeline 调度行为"""

    def test_main_first_run_generates_and_discovers_registrations(self) -> None:
        """洁净首轮执行 main.py 即把本轮暂存的 FStaticDataRegistration / FGameGameRegistration 纳入统一注册头"""
        with tempfile.TemporaryDirectory() as tmp:
            proj = create_mini_project(tmp, project_name="CleanApp", module_name="CleanData")
            create_xlsx(
                os.path.join(proj["data_tables"], "Hero", "DT_Hero.xlsx"),
                "DT_Hero",
                (["主键", "生命"], ["RowName", "hp"], ["FName", "int32"]),
                rows=[["Hero_1", 100]],
            )

            with mock.patch.object(sys, "argv", ["main.py", "--config", proj["config"]]):
                codegen_main.main()

            self.assertTrue(os.path.isfile(proj["reg_header"]))
            with open(proj["reg_header"], "r", encoding="utf-8") as handle:
                reg_content = handle.read()
            self.assertIn("FStaticDataRegistration::RegisterAll", reg_content)
            self.assertIn("FGameGameRegistration::RegisterAll", reg_content)
            self.assertFalse(os.path.exists(os.path.join(proj["module_dir"], "Intermediate")))

            cfg_override = codegen_main.build_config(proj["config"], {"row_types_include_prefix": "Custom/Prefix"})
            self.assertEqual(cfg_override.row_types_include_prefix, "Custom/Prefix")
            self.assertEqual(len(cfg_override.generated_roots()), 5)
            self.assertIsInstance(codegen_main.load_artifacts(proj["config"]), dict)

            with mock.patch.object(sys, "argv", ["main.py", "--config", proj["config"]]):
                codegen_main.main()

            with mock.patch.object(sys, "argv", ["main.py", "--config", proj["config"], "--print-config"]):
                codegen_main.main()

            os.remove(os.path.join(proj["data_tables"], "Hero", "DT_Hero.xlsx"))
            with mock.patch.object(sys, "argv", ["main.py", "--config", proj["config"]]):
                codegen_main.main()
            self.assertFalse(os.path.exists(proj["reg_header"]))
            with mock.patch.object(sys, "argv", ["main.py", "--config", proj["config"]]):
                codegen_main.main()

            create_xlsx(
                os.path.join(proj["data_tables"], "Hero", "DT_BadName.xlsx"),
                "DT_BadName",
                (["主键", "非法"], ["RowName", "name"], ["FName", "FString"]),
                rows=[["H1", "x"]],
            )
            with mock.patch.object(sys, "argv", ["main.py", "--config", proj["config"]]):
                with self.assertRaises(SystemExit) as cm_bad:
                    codegen_main.main()
                self.assertEqual(cm_bad.exception.code, 1)
            os.remove(os.path.join(proj["data_tables"], "Hero", "DT_BadName.xlsx"))

            with open(os.path.join(proj["data_tables"], "Hero", "DT_Corrupt.xlsx"), "wb") as handle:
                handle.write(b"NOT_ZIP")
            with mock.patch.object(sys, "argv", ["main.py", "--config", proj["config"]]):
                with self.assertRaises(SystemExit) as cm_corrupt:
                    codegen_main.main()
                self.assertEqual(cm_corrupt.exception.code, 1)

            create_xlsx(
                os.path.join(proj["data_tables"], "Hero", "DT_Good.xlsx"),
                "DT_Good",
                (["主键", "数值"], ["RowName", "val"], ["FName", "int32"]),
                rows=[["H1", 1]],
            )
            with mock.patch.object(sys, "argv", ["main.py", "--config", proj["config"]]):
                with self.assertRaises(SystemExit) as cm_partial:
                    codegen_main.main()
                self.assertEqual(cm_partial.exception.code, 1)
            os.remove(os.path.join(proj["data_tables"], "Hero", "DT_Corrupt.xlsx"))

            with mock.patch.object(codegen_main.GenerationStaging, "commit", side_effect=OSError("commit fail")):
                with mock.patch.object(sys, "argv", ["main.py", "--config", proj["config"]]):
                    with self.assertRaises(SystemExit) as cm_commit:
                        codegen_main.main()
                    self.assertEqual(cm_commit.exception.code, 1)

    def test_run_pipeline_mode_resolution_dispatch_and_export_bin(self) -> None:
        """全面验证 run_pipeline 模式分发、逆向后验回滚、.bin 产物检查、用法输出与 CLI 主入口"""
        run_pipeline.print_usage()
        ns = argparse.Namespace(reverse=False, rollback=False, roundtrip=True, validate=False, struct=False, json=False, tags=False, bin=False)
        self.assertEqual(run_pipeline._resolve_mode(ns), "roundtrip")
        ns.roundtrip = False
        self.assertEqual(run_pipeline._resolve_mode(ns), "all")

        conflict_ns = argparse.Namespace(
            all=False, reverse=False, rollback=False, roundtrip=False,
            validate=True, struct=False, json=True, tags=False, bin=False,
        )
        with self.assertRaises(ValueError):
            run_pipeline._resolve_mode(conflict_ns)

        with tempfile.TemporaryDirectory() as tmp:
            proj = create_mini_project(tmp)
            self.assertFalse(
                run_pipeline._dispatch_mode("validate", sys.executable, proj["config"], conflict_ns, ["--unknown-flag"])
            )
            fake_editor = os.path.join(tmp, "FakeEditor")
            with open(fake_editor, "w", encoding="utf-8") as handle:
                handle.write("#!/bin/sh\nexit 0\n")

            self.assertFalse(run_pipeline.run_script(sys.executable, "nonexistent_script.py", proj["config"], []))
            with mock.patch.object(run_pipeline.subprocess, "run", return_value=mock.Mock(returncode=0)):
                self.assertTrue(run_pipeline.run_script(sys.executable, "validate_tables.py", proj["config"], []))

            with open(proj["config"], "r", encoding="utf-8") as handle:
                cfg_data = json.load(handle)
            cfg_data["unreal_editor_cmd"] = fake_editor
            cfg_data["clang_compiler"] = fake_editor
            with open(proj["config"], "w", encoding="utf-8") as handle:
                json.dump(cfg_data, handle)

            with mock.patch.object(run_pipeline.subprocess, "run", return_value=mock.Mock(returncode=1)):
                self.assertFalse(run_pipeline.run_export_bin(proj["config"]))

            with mock.patch.object(run_pipeline.subprocess, "run", return_value=mock.Mock(returncode=0)):
                self.assertFalse(run_pipeline.run_export_bin(proj["config"]))

            bin_path = os.path.join(tmp, "Content", "DataTables_Bin", "StaticDataCache.bin")
            os.makedirs(os.path.dirname(bin_path), exist_ok=True)
            with open(bin_path, "wb") as handle:
                handle.write(b"OLD")
            os.utime(bin_path, (1000000.0, 1000000.0))
            with mock.patch.object(run_pipeline.subprocess, "run", return_value=mock.Mock(returncode=0)):
                self.assertFalse(run_pipeline.run_export_bin(proj["config"]))

            def _touch_bin(*_args: Any, **_kwargs: Any) -> mock.Mock:
                """
                模拟编辑器进程导出并刷新 .bin 文件时间戳
                @return returncode=0 的 Mock 进程结果
                """
                with open(bin_path, "wb") as handle:
                    handle.write(b"FRESH")
                return mock.Mock(returncode=0)

            with mock.patch.object(run_pipeline.subprocess, "run", side_effect=_touch_bin):
                self.assertTrue(run_pipeline.run_export_bin(proj["config"]))

            cfg_no_editor = os.path.join(tmp, "no_editor.json")
            with open(cfg_no_editor, "w", encoding="utf-8") as handle:
                json.dump({"project_file": proj["uproject"], "static_data_bin_content_path": "DataTables_Bin/X.bin", "unreal_editor_cmd": "/no/such/editor", "clang_compiler": fake_editor}, handle)
            with mock.patch.object(run_pipeline.tool_config, "_detect_unreal_editor_cmd", return_value=""):
                self.assertFalse(run_pipeline.run_export_bin(cfg_no_editor))

            cfg_no_bin = os.path.join(tmp, "no_bin.json")
            with open(cfg_no_bin, "w", encoding="utf-8") as handle:
                json.dump({"project_file": "", "static_data_bin_content_path": "", "clang_compiler": fake_editor}, handle)
            with mock.patch.object(run_pipeline.tool_config, "_find_project_root_and_file", return_value=(None, None, None)):
                self.assertFalse(run_pipeline.run_export_bin(cfg_no_bin))

            base_ns = argparse.Namespace(
                table="DT_Item", dry_run=False, allow_delete=True, force_formula=True,
                fill_defaults=True, float_epsilon=1e-5, no_backup=False, strict=True,
                snapshot="20260101_000000",
            )
            with mock.patch.object(run_pipeline, "run_script", return_value=True):
                self.assertTrue(run_pipeline._dispatch_mode("all", sys.executable, proj["config"], base_ns, []))
                self.assertTrue(run_pipeline._dispatch_mode("rollback", sys.executable, proj["config"], base_ns, []))
                self.assertTrue(run_pipeline._dispatch_mode("roundtrip", sys.executable, proj["config"], base_ns, []))
                self.assertTrue(run_pipeline._dispatch_mode("reverse", sys.executable, proj["config"], base_ns, []))

            with mock.patch.object(run_pipeline, "run_export_bin", return_value=True):
                self.assertTrue(run_pipeline._dispatch_mode("bin", sys.executable, proj["config"], base_ns, []))

            with mock.patch.object(run_pipeline, "run_script", return_value=False):
                self.assertFalse(run_pipeline._dispatch_mode("validate", sys.executable, proj["config"], base_ns, []))
                self.assertFalse(run_pipeline._run_reverse_mode(sys.executable, proj["config"], base_ns))

            dry_ns = argparse.Namespace(**{**vars(base_ns), "dry_run": True})
            with mock.patch.object(run_pipeline, "run_script", return_value=True):
                self.assertTrue(run_pipeline._run_reverse_mode(sys.executable, proj["config"], dry_ns))

            nb_ns = argparse.Namespace(**{**vars(base_ns), "no_backup": True})
            with mock.patch.object(run_pipeline, "run_script", side_effect=[True, False]):
                self.assertFalse(run_pipeline._run_reverse_mode(sys.executable, proj["config"], nb_ns))

            with mock.patch.object(run_pipeline, "run_script", side_effect=[True, False]), \
                    mock.patch.object(run_pipeline.json_to_excel, "resolve_snapshot_dir", side_effect=[None, None]):
                self.assertFalse(run_pipeline._run_reverse_mode(sys.executable, proj["config"], base_ns))

            with mock.patch.object(run_pipeline, "run_script", side_effect=[True, False, True]), \
                    mock.patch.object(run_pipeline.json_to_excel, "resolve_snapshot_dir", side_effect=[None, "/tmp/snap_new"]):
                self.assertFalse(run_pipeline._run_reverse_mode(sys.executable, proj["config"], base_ns))

            with mock.patch.object(run_pipeline, "run_script", side_effect=[True, False, False]), \
                    mock.patch.object(run_pipeline.json_to_excel, "resolve_snapshot_dir", side_effect=[None, "/tmp/snap_new"]):
                self.assertFalse(run_pipeline._run_reverse_mode(sys.executable, proj["config"], base_ns))

            with mock.patch.object(sys, "argv", ["run_pipeline.py", "--config", os.path.join(tmp, "missing.json")]):
                with self.assertRaises(SystemExit) as cm_miss:
                    run_pipeline.main()
                self.assertEqual(cm_miss.exception.code, 1)

            with mock.patch.object(sys, "argv", ["run_pipeline.py"]), \
                    mock.patch.object(run_pipeline.tool_config, "resolve_config_path", return_value=None):
                with self.assertRaises(SystemExit) as cm_auto:
                    run_pipeline.main()
                self.assertEqual(cm_auto.exception.code, 1)

            with mock.patch.object(sys, "argv", ["run_pipeline.py", "--config", proj["config"], "--validate"]), \
                    mock.patch.object(run_pipeline, "_dispatch_mode", return_value=True):
                run_pipeline.main()

            with mock.patch.object(sys, "argv", ["run_pipeline.py", "--config", proj["config"], "--validate"]), \
                    mock.patch.object(run_pipeline, "_dispatch_mode", return_value=False):
                with self.assertRaises(SystemExit) as cm_fail:
                    run_pipeline.main()
                self.assertEqual(cm_fail.exception.code, 1)


if __name__ == "__main__":
    unittest.main()
