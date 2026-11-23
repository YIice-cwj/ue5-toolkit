#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
统一工具配置加载器 (tool_config.py) 单元测试
"""

import json
import os
import tempfile
import unittest
from unittest import mock

from test_helpers import create_mini_project
import tool_config


class TestToolConfig(unittest.TestCase):
    """验证路径展开、API 宏推导、日志默认值解耦、环境变量收敛与错误退出契约"""

    def test_env_keys_only_include_unified_key(self) -> None:
        """环境变量发现列表已移除废弃的 UE_STATIC_DATA_CONFIG"""
        self.assertEqual(tool_config.CONFIG_ENV_KEYS, ("UE_TOOL_CONFIG",))

    def test_derive_console_names(self) -> None:
        """控制台命令名按项目前缀一致推导"""
        names = tool_config.derive_console_names("MyGame")
        self.assertEqual(names["export_bin"], "MyGame.ExportStaticDataBin")
        self.assertEqual(names["report_integrity"], "MyGame.ReportStaticDataIntegrity")
        self.assertEqual(names["channel_cmd_arg"], "MyGameStaticDataChannel")
        self.assertEqual(names["verify_switch"], "MyGameStaticDataVerify")
        self.assertEqual(names["export_commandlet"], "MyGameStaticDataExport")

    def test_derive_api_macro_from_module_dir_basename(self) -> None:
        """输出模块目录名含 GameData 前缀时仍按末级目录名准确推导 API 宏"""
        self.assertEqual(
            tool_config._derive_api_macro("/repo/Plugins/GameDataSamples/Source/GameDataSamples", "UEToolkit"),
            "GAMEDATASAMPLES_API",
        )
        self.assertEqual(
            tool_config._derive_api_macro("/repo/Plugins/GameData/Source/GameData", "UEToolkit"),
            "GAMEDATA_API",
        )
        self.assertEqual(tool_config._derive_api_macro("", "CustomPrimary"), "CUSTOMPRIMARY_API")
        self.assertEqual(tool_config._derive_api_macro("", None), "GAME_API")

    def test_load_config_derives_paths_api_macro_and_decoupled_logging(self) -> None:
        """完整加载配置：自动派生 API 宏、日志默认统一为 GameLogging、展开相对路径"""
        with tempfile.TemporaryDirectory() as tmp:
            proj = create_mini_project(tmp, project_name="MyProj", module_name="GameDataSamples")
            cfg_path = proj["config"]
            with open(cfg_path, "r", encoding="utf-8") as handle:
                data = json.load(handle)
            data["project_prefix"] = "CustomPrefix"
            data["back_write"] = {"backup_dir": "./Backups"}
            with open(cfg_path, "w", encoding="utf-8") as handle:
                json.dump(data, handle)

            loaded = tool_config.load_config(cfg_path, tool_config.KEYS_STRUCT)
            self.assertEqual(loaded["api_macro"], "GAMEDATASAMPLES_API")
            self.assertEqual(loaded["project_prefix"], "CustomPrefix")
            self.assertEqual(loaded["log_header"], "Logging/GameLogCategories.h")
            self.assertEqual(loaded["log_macro"], "GAME_LOG")
            self.assertEqual(loaded["log_category_name"], "LogGame")
            self.assertEqual(loaded["blueprint_category"], "CustomPrefix|StaticData")
            self.assertEqual(loaded["backup_dir"], os.path.normpath(os.path.join(tmp, "Backups")))
            self.assertEqual(loaded["data_tables_source_dir"], proj["data_tables"])

    def test_resolve_path_dot_vs_project_root_vs_absolute(self) -> None:
        """点号开头相对配置文件目录，其余相对工程根，绝对路径原样保留"""
        self.assertEqual(tool_config._resolve_path("/cfg", "/proj", "/abs/path"), "/abs/path")
        self.assertEqual(tool_config._resolve_path("/cfg/sub", "/proj", "./local"), "/cfg/sub/local")
        self.assertEqual(tool_config._resolve_path("/cfg/sub", "/proj", "DataTables"), "/proj/DataTables")
        self.assertEqual(tool_config._resolve_path("/cfg/sub", None, "DataTables"), "/cfg/sub/DataTables")
        self.assertEqual(tool_config._resolve_path("/cfg", "/proj", ""), "")

    def test_resolve_config_path_priority_and_env(self) -> None:
        """配置发现优先级：显式参数 > 环境变量 > extra_dirs"""
        with tempfile.TemporaryDirectory() as tmp:
            env_cfg = os.path.join(tmp, "env_cfg.json")
            dir_cfg = os.path.join(tmp, tool_config.DEFAULT_CONFIG_FILENAME)
            with open(env_cfg, "w", encoding="utf-8") as handle:
                handle.write("{}")
            with open(dir_cfg, "w", encoding="utf-8") as handle:
                handle.write("{}")

            self.assertEqual(tool_config.resolve_config_path(dir_cfg), dir_cfg)
            self.assertIsNone(tool_config.resolve_config_path(os.path.join(tmp, "missing.json")))

            with mock.patch.dict(os.environ, {"UE_TOOL_CONFIG": env_cfg}, clear=False):
                self.assertEqual(tool_config.resolve_config_path(None, extra_dirs=(tmp,)), env_cfg)

            with mock.patch.dict(os.environ, {}, clear=True):
                self.assertEqual(tool_config.resolve_config_path(None, extra_dirs=(tmp,)), dir_cfg)

    def test_load_config_error_exits(self) -> None:
        """非法配置文件（缺失、非对象、非法 row_key_mode、缺必需键）均以非零码终止"""
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit) as cm:
                tool_config.load_config(os.path.join(tmp, "none.json"), ())
            self.assertEqual(cm.exception.code, 1)

            bad_json = os.path.join(tmp, "list.json")
            with open(bad_json, "w", encoding="utf-8") as handle:
                handle.write("[]")
            with self.assertRaises(SystemExit) as cm2:
                tool_config.load_config(bad_json, ())
            self.assertEqual(cm2.exception.code, 1)

            bad_mode = os.path.join(tmp, "bad_mode.json")
            with open(bad_mode, "w", encoding="utf-8") as handle:
                json.dump({"row_key_mode": "invalid_mode"}, handle)
            with self.assertRaises(SystemExit) as cm3:
                tool_config.load_config(bad_mode, ())
            self.assertEqual(cm3.exception.code, 1)

            missing_key = os.path.join(tmp, "missing_key.json")
            with open(missing_key, "w", encoding="utf-8") as handle:
                json.dump({}, handle)
            with self.assertRaises(SystemExit) as cm4:
                tool_config.load_config(missing_key, ("nonexistent_required_key",))
            self.assertEqual(cm4.exception.code, 1)

    def test_uproject_association_and_engine_helpers(self) -> None:
        """读取 .uproject EngineAssociation 与候选引擎目录函数行为确定"""
        with tempfile.TemporaryDirectory() as tmp:
            proj = create_mini_project(tmp)
            self.assertEqual(tool_config._read_uproject_engine_association(proj["uproject"]), "5.8")
            self.assertIsNone(tool_config._read_uproject_engine_association(os.path.join(tmp, "missing.uproject")))

            broken = os.path.join(tmp, "broken.uproject")
            with open(broken, "w", encoding="utf-8") as handle:
                handle.write("{not json")
            self.assertIsNone(tool_config._read_uproject_engine_association(broken))

            roots = tool_config._platform_engine_roots(["5.8"])
            self.assertTrue(len(roots) >= 2)
            bins = tool_config._editor_binary_candidates("/FakeEngine")
            self.assertTrue(len(bins) >= 1)

    def test_cross_platform_engine_and_compiler_detection_branches(self) -> None:
        """覆盖 Win32/Linux/macOS 引擎目录、编辑器二进制、源码路径与 Clang 检测分支"""
        with tempfile.TemporaryDirectory() as tmp:
            fake_engine = os.path.join(tmp, "UE_5.8")
            fake_src = os.path.join(fake_engine, "Engine", "Source")
            os.makedirs(fake_src, exist_ok=True)
            fake_mac_bin = os.path.join(fake_engine, "Engine", "Binaries", "Mac", "UnrealEditor-Cmd")
            os.makedirs(os.path.dirname(fake_mac_bin), exist_ok=True)
            with open(fake_mac_bin, "w", encoding="utf-8") as handle:
                handle.write("")
            fake_win_bin = os.path.join(fake_engine, "Engine", "Binaries", "Win64", "UnrealEditor-Cmd.exe")
            os.makedirs(os.path.dirname(fake_win_bin), exist_ok=True)
            with open(fake_win_bin, "w", encoding="utf-8") as handle:
                handle.write("")

            proj_dir = os.path.join(tmp, "Proj")
            os.makedirs(proj_dir, exist_ok=True)
            proj_uproject = create_mini_project(proj_dir)["uproject"]
            with mock.patch.object(tool_config.sys, "platform", "win32"):
                self.assertTrue(any("C:/Program Files" in r for r in tool_config._platform_engine_roots(["", "5.8"])))
                self.assertTrue(tool_config._editor_binary_candidates(fake_engine)[0].endswith("UnrealEditor-Cmd.exe"))
                self.assertIsNone(tool_config._query_windows_engine_root("5.8"))
                with mock.patch.dict(os.environ, {"UE_ENGINE_DIR": fake_engine}, clear=False):
                    self.assertEqual(tool_config._detect_unreal_editor_cmd(None), os.path.normpath(fake_win_bin))
                    self.assertEqual(tool_config._detect_engine_source_dir(None, ""), os.path.normpath(fake_src))
                with mock.patch.dict(os.environ, {}, clear=True), \
                        mock.patch.object(tool_config, "_query_windows_engine_root", return_value=fake_engine):
                    self.assertEqual(tool_config._detect_unreal_editor_cmd(proj_uproject), os.path.normpath(fake_win_bin))
                    self.assertEqual(tool_config._detect_engine_source_dir(proj_uproject, ""), os.path.normpath(fake_src))
                with mock.patch.dict(os.environ, {}, clear=True), \
                        mock.patch.object(tool_config, "_query_windows_engine_root", return_value=None), \
                        mock.patch.object(tool_config.glob, "glob", return_value=[fake_src]):
                    self.assertEqual(tool_config._detect_engine_source_dir(None, ""), os.path.normpath(fake_src))
                with mock.patch.dict(os.environ, {}, clear=True), \
                        mock.patch.object(tool_config.shutil, "which", return_value=None), \
                        mock.patch.object(tool_config.os.path, "isfile", return_value=True):
                    self.assertIn("clang++.exe", tool_config._detect_clang_compiler())

            with mock.patch.object(tool_config.sys, "platform", "linux"):
                self.assertTrue(any("/opt/UnrealEngine" in r for r in tool_config._platform_engine_roots(["5.8"])))
                self.assertTrue(tool_config._editor_binary_candidates(fake_engine)[0].endswith("UnrealEditor"))
                self.assertIsNone(tool_config._query_windows_engine_root("5.8"))
                with mock.patch.dict(os.environ, {}, clear=True), \
                        mock.patch.object(tool_config, "_platform_engine_roots", return_value=[]):
                    self.assertEqual(tool_config._detect_unreal_editor_cmd(None), "")
                    self.assertEqual(tool_config._detect_engine_source_dir(None, fake_mac_bin), os.path.normpath(fake_src))
                    self.assertEqual(tool_config._detect_engine_source_dir(None, ""), "")

            with mock.patch.dict(os.environ, {"UE_ENGINE_DIR": fake_src}, clear=False):
                self.assertEqual(tool_config._detect_engine_source_dir(None, ""), os.path.normpath(fake_src))

            with mock.patch.object(tool_config.sys, "platform", "darwin"), \
                    mock.patch.dict(os.environ, {}, clear=True), \
                    mock.patch.object(tool_config, "_platform_engine_roots", return_value=[]), \
                    mock.patch.object(tool_config.glob, "glob", return_value=[fake_src]):
                self.assertEqual(tool_config._detect_engine_source_dir(None, ""), os.path.normpath(fake_src))

            with mock.patch.dict(os.environ, {"UE_CLANG_COMPILER": fake_mac_bin}, clear=False):
                self.assertEqual(tool_config._detect_clang_compiler(), os.path.normpath(fake_mac_bin))

            with mock.patch.dict(os.environ, {}, clear=True), \
                    mock.patch.object(tool_config.subprocess, "check_output", side_effect=OSError("no xcrun")), \
                    mock.patch.object(tool_config.shutil, "which", return_value="/usr/bin/clang++"):
                self.assertEqual(tool_config._detect_clang_compiler(), "/usr/bin/clang++")

            with mock.patch.object(tool_config.sys, "platform", "linux"), \
                    mock.patch.dict(os.environ, {}, clear=True), \
                    mock.patch.object(tool_config.shutil, "which", return_value=None):
                self.assertEqual(tool_config._detect_clang_compiler(), "")

    def test_load_config_fallbacks_and_auto_discovery_failure(self) -> None:
        """覆盖无 output_module_dir 推导、无 project_root 注册推导、损坏 .uproject 回退与自动发现失败"""
        with tempfile.TemporaryDirectory() as tmp:
            broken_uproject = os.path.join(tmp, "FallbackApp.uproject")
            with open(broken_uproject, "w", encoding="utf-8") as handle:
                handle.write("{corrupt")
            cfg_path = os.path.join(tmp, "cfg.json")
            with open(cfg_path, "w", encoding="utf-8") as handle:
                json.dump({"project_file": "./FallbackApp.uproject", "clang_compiler": broken_uproject}, handle)
            loaded = tool_config.load_config(cfg_path, ())
            self.assertEqual(loaded["output_module_dir"], os.path.normpath(os.path.join(tmp, "Source", "FallbackApp")))

            standalone_dir = os.path.join(tmp, "Standalone")
            os.makedirs(standalone_dir, exist_ok=True)
            standalone_cfg = os.path.join(standalone_dir, "cfg.json")
            mod_only = os.path.join(standalone_dir, "ModOnly")
            with open(standalone_cfg, "w", encoding="utf-8") as handle:
                json.dump({"output_module_dir": mod_only, "clang_compiler": broken_uproject}, handle)
            with mock.patch.object(tool_config, "_find_project_root_and_file", return_value=(None, None, None)):
                loaded_mod = tool_config.load_config(standalone_cfg, ())
            self.assertIn("ModOnly", loaded_mod["registration"]["output_header"])

            with mock.patch.object(tool_config, "resolve_config_path", return_value=None):
                with self.assertRaises(SystemExit) as cm:
                    tool_config.load_config(None, ())
                self.assertEqual(cm.exception.code, 1)

            with mock.patch.dict(os.environ, {}, clear=True), \
                    mock.patch.object(tool_config, "config_candidates", return_value=[os.path.join(tmp, "nope.json")]):
                self.assertIsNone(tool_config.resolve_config_path(None))


if __name__ == "__main__":
    unittest.main()
