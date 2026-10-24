#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
统一工具配置加载器 (tool_config.py)

- 全部工具脚本（validate_tables / generate_cpp_structs / excel_to_json）
  只从配置文件读取路径与参数，脚本内不内置任何默认路径与工程假设。
- 配置来源：--config <JSON 文件>（强制）；命令行参数仅作为对配置字段的覆盖。
- 必需键缺失时打印缺失清单并以非零退出码终止。
- 相对路径展开：所有路径类配置值相对配置文件所在目录展开为绝对路径，
  工程可整目录搬迁而无需改配置（去个人绝对路径）。
"""

import glob
import json
import os
import subprocess
import sys
from typing import Any, Dict, List, Optional, Sequence, Tuple

DEFAULT_CONFIG_FILENAME = "generator_config.json"

# 本模块自身目录与 Tools/Excel 目录（配置发现的常规落点，全工具唯一来源）
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TOOLS_DIR = os.path.dirname(SCRIPT_DIR)
TOOLS_EXCEL_DIR = os.path.join(TOOLS_DIR, "Excel")

# 配置发现的环境变量（按优先级；不含任何工程专有名）
CONFIG_ENV_KEYS = ("UE_TOOL_CONFIG", "UE_STATIC_DATA_CONFIG")

# 常规配置文件位置（各工具脚本 import 本常量复用，不再各自拼路径）
CONVENTIONAL_CONFIG_PATH = os.path.normpath(os.path.join(TOOLS_EXCEL_DIR, DEFAULT_CONFIG_FILENAME))

KEY_DATA_TABLES_SOURCE_DIR = "data_tables_source_dir"
KEY_ENGINE_SOURCE_DIR = "engine_source_dir"

KEYS_STRUCT = (
    KEY_DATA_TABLES_SOURCE_DIR,
    "output_module_dir",
    "api_macro",
    "subsystem_header_include",
    "json_root_dir_name",
    "static_data_bin_content_path",
)

KEYS_VALIDATE = (KEY_DATA_TABLES_SOURCE_DIR,)

KEYS_JSON = (
    KEY_DATA_TABLES_SOURCE_DIR,
    "json_output_dir",
)

KEYS_TAGS = (KEY_DATA_TABLES_SOURCE_DIR,)

KEYS_BUILD = (KEY_ENGINE_SOURCE_DIR,)

PATH_KEYS = (
    KEY_DATA_TABLES_SOURCE_DIR,
    "output_module_dir",
    "json_output_dir",
    "gameplay_tags_ini_path",
    "foreign_keys_file",
    "project_file",
    KEY_ENGINE_SOURCE_DIR,
)

SECTION_PATH_SPECS = (
    ("registration", "output_header", "single"),
    ("registration", "source_roots", "list"),
    ("back_write", "backup_dir", "single"),
)


def _find_project_root_and_file(start_dir: str) -> Tuple[Optional[str], Optional[str], Optional[str]]:
    """递归向上查找 *.uproject 并解析主模块名"""
    curr = os.path.abspath(start_dir)
    while True:
        try:
            candidates = [f for f in os.listdir(curr) if f.endswith(".uproject") and os.path.isfile(os.path.join(curr, f))]
        except OSError:
            candidates = []
        if candidates:
            uproject_path = os.path.join(curr, candidates[0])
            project_root = curr
            primary_module = None
            try:
                with open(uproject_path, "r", encoding="utf-8") as f:
                    uproject_data = json.load(f)
                modules = uproject_data.get("Modules", [])
                if modules and isinstance(modules, list) and isinstance(modules[0], dict):
                    primary_module = modules[0].get("Name")
            except Exception:
                pass
            if not primary_module:
                primary_module = os.path.splitext(candidates[0])[0]
            return project_root, uproject_path, primary_module
        parent = os.path.dirname(curr)
        if parent == curr:
            break
        curr = parent
    return None, None, None


def _resolve_path(config_dir: str, project_root: Optional[str], value: Any) -> Any:
    """相对路径展开为绝对路径"""
    if not value:
        return value
    expanded = os.path.expandvars(str(value))
    if os.path.isabs(expanded):
        return expanded
    if project_root:
        if expanded.startswith("."):
            return os.path.normpath(os.path.join(config_dir, expanded))
        return os.path.normpath(os.path.join(project_root, expanded))
    return os.path.normpath(os.path.join(config_dir, expanded))


def _expand_relative_paths(raw: Dict[str, Any], config_dir: str, project_root: Optional[str]) -> Dict[str, Any]:
    """递归展开配置中的相对路径"""
    for key in PATH_KEYS:
        if raw.get(key):
            raw[key] = _resolve_path(config_dir, project_root, raw[key])
    for section, key, kind in SECTION_PATH_SPECS:
        if not isinstance(raw.get(section), dict):
            continue
        value = raw[section].get(key)
        if not value:
            continue
        if kind == "list":
            raw[section][key] = [_resolve_path(config_dir, project_root, item) for item in value]
        else:
            raw[section][key] = _resolve_path(config_dir, project_root, value)
    return raw


def _infer_project_prefix(raw: Dict[str, Any]) -> str:
    """推导项目前缀：显式 project_prefix 优先，缺省默认 Game"""
    explicit = raw.get("project_prefix")
    if explicit:
        return str(explicit).strip()
    return "Game"


def _detect_unreal_editor_cmd(uproject_path: Optional[str]) -> str:
    """根据 UE_ENGINE_DIR、当前工程 .uproject 中的 EngineAssociation 或已安装 UE 5.8~5.1 智能探测虚幻编辑器"""
    is_mac = sys.platform == "darwin"
    is_win = sys.platform == "win32"

    env_dir = os.environ.get("UE_ENGINE_DIR")
    if env_dir:
        expanded = os.path.expandvars(env_dir)
        env_candidates = (
            [
                os.path.join(expanded, "Engine", "Binaries", "Mac", "UnrealEditor.app", "Contents", "MacOS", "UnrealEditor"),
                os.path.join(expanded, "Engine", "Binaries", "Mac", "UnrealEditor-Cmd"),
            ]
            if is_mac
            else (
                [os.path.join(expanded, "Engine", "Binaries", "Win64", "UnrealEditor-Cmd.exe")]
                if is_win
                else [os.path.join(expanded, "Engine", "Binaries", "Linux", "UnrealEditor")]
            )
        )
        for path in env_candidates:
            if os.path.isfile(path):
                return os.path.normpath(path)

    engine_assoc = None
    if uproject_path and os.path.isfile(uproject_path):
        try:
            with open(uproject_path, "r", encoding="utf-8") as f:
                data = json.load(f)
                raw_assoc = data.get("EngineAssociation")
                if raw_assoc:
                    engine_assoc = str(raw_assoc).strip()
        except Exception:
            pass

    version_tags = ([engine_assoc] if engine_assoc else []) + [f"5.{m}" for m in range(8, 0, -1)]
    candidates = []
    for tag in version_tags:
        if is_mac:
            candidates.append(f"/Users/Shared/Epic Games/UE_{tag}/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor")
            candidates.append(f"/Applications/Epic Games/UE_{tag}/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor")
        elif is_win:
            candidates.append(f"C:/Program Files/Epic Games/UE_{tag}/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
            candidates.append(f"D:/Program Files/Epic Games/UE_{tag}/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
        else:
            candidates.append(f"/opt/UnrealEngine/UE_{tag}/Engine/Binaries/Linux/UnrealEditor")
            candidates.append(os.path.expanduser(f"~/UnrealEngine/UE_{tag}/Engine/Binaries/Linux/UnrealEditor"))

    for path in candidates:
        if os.path.isfile(path):
            return os.path.normpath(path)

    if is_win and engine_assoc:
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"SOFTWARE\Epic Games\Unreal Engine\Builds") as key:
                val, _ = winreg.QueryValueEx(key, engine_assoc)
                if val:
                    candidate = os.path.join(val, "Engine", "Binaries", "Win64", "UnrealEditor-Cmd.exe")
                    if os.path.isfile(candidate):
                        return os.path.normpath(candidate)
        except Exception:
            pass

    return ""


def _detect_engine_source_dir(uproject_path: Optional[str], unreal_editor_cmd: Optional[str] = None) -> str:
    """智能推导虚幻引擎源码目录 (<Engine>/Source)"""
    env_dir = os.environ.get("UE_ENGINE_DIR")
    if env_dir:
        expanded = os.path.expandvars(env_dir)
        for sub in ("Source", os.path.join("Engine", "Source")):
            target = os.path.join(expanded, sub)
            if os.path.isdir(target):
                return os.path.normpath(target)
        if os.path.isdir(expanded):
            return os.path.normpath(expanded)

    engine_assoc = None
    if uproject_path and os.path.isfile(uproject_path):
        try:
            with open(uproject_path, "r", encoding="utf-8") as f:
                data = json.load(f)
                raw_assoc = data.get("EngineAssociation")
                if raw_assoc:
                    engine_assoc = str(raw_assoc).strip()
        except Exception:
            pass

    is_mac = sys.platform == "darwin"
    is_win = sys.platform == "win32"

    candidates: List[str] = []
    if engine_assoc:
        if is_mac:
            candidates.append(f"/Users/Shared/Epic Games/UE_{engine_assoc}/Engine/Source")
            candidates.append(f"/Applications/Epic Games/UE_{engine_assoc}/Engine/Source")
        elif is_win:
            candidates.append(f"C:/Program Files/Epic Games/UE_{engine_assoc}/Engine/Source")
            candidates.append(f"D:/Program Files/Epic Games/UE_{engine_assoc}/Engine/Source")
            candidates.append(f"E:/Program Files/Epic Games/UE_{engine_assoc}/Engine/Source")
        else:
            candidates.append(f"/opt/UnrealEngine/UE_{engine_assoc}/Engine/Source")
            candidates.append(os.path.expanduser(f"~/UnrealEngine/UE_{engine_assoc}/Engine/Source"))

    for path in candidates:
        if os.path.isdir(path):
            return os.path.normpath(path)

    if is_win and engine_assoc:
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"SOFTWARE\Epic Games\Unreal Engine\Builds") as key:
                val, _ = winreg.QueryValueEx(key, engine_assoc)
                if val:
                    for sub in (os.path.join("Engine", "Source"), "Source"):
                        cand = os.path.join(val, sub)
                        if os.path.isdir(cand):
                            return os.path.normpath(cand)
        except Exception:
            pass

    if unreal_editor_cmd and os.path.isfile(unreal_editor_cmd):
        norm_cmd = os.path.normpath(unreal_editor_cmd)
        idx = norm_cmd.find(os.path.join("Engine", "Binaries"))
        if idx != -1:
            candidate = os.path.join(norm_cmd[:idx], "Engine", "Source")
            if os.path.isdir(candidate):
                return os.path.normpath(candidate)

    if is_mac:
        fallback_dirs = sorted(glob.glob("/Users/Shared/Epic Games/UE_*/Engine/Source"), reverse=True)
        if fallback_dirs:
            return os.path.normpath(fallback_dirs[0])
    elif is_win:
        for drive in ("C", "D", "E"):
            fallback_dirs = sorted(glob.glob(f"{drive}:/Program Files/Epic Games/UE_*/Engine/Source"), reverse=True)
            if fallback_dirs:
                return os.path.normpath(fallback_dirs[0])

    return ""


def _detect_clang_compiler() -> str:
    """智能探测 clang++ 编译器路径"""
    env_path = os.environ.get("UE_CLANG_COMPILER")
    if env_path and os.path.isfile(env_path):
        return os.path.normpath(os.path.abspath(env_path))

    if sys.platform == "darwin":
        try:
            detected = subprocess.check_output(["xcrun", "--find", "clang++"], text=True).strip()
            if detected and os.path.isfile(detected):
                return os.path.normpath(detected)
        except (OSError, subprocess.SubprocessError):
            pass

    import shutil
    detected = shutil.which("clang++")
    if detected:
        return os.path.normpath(detected)

    if sys.platform == "win32":
        for cand in (
            r"C:\Program Files\LLVM\bin\clang++.exe",
            r"C:\Program Files (x86)\LLVM\bin\clang++.exe",
        ):
            if os.path.isfile(cand):
                return os.path.normpath(cand)

    return ""


def derive_console_names(prefix: str) -> Dict[str, str]:
    """按项目前缀推导控制台命令 / 命令行开关的约定名（生成器与 Cook 工具共用同一真源）
    @param prefix 项目前缀（如 Game）
    @return 字典：export_bin / report_integrity / channel_cmd_arg / verify_switch / export_commandlet
    """
    return {
        "export_bin": f"{prefix}.ExportStaticDataBin",
        "report_integrity": f"{prefix}.ReportStaticDataIntegrity",
        "channel_cmd_arg": f"{prefix}StaticDataChannel",
        "verify_switch": f"{prefix}StaticDataVerify",
        "export_commandlet": f"{prefix}StaticDataExport",
    }


def _normalize_and_derive_config(
    raw: Dict[str, Any],
    config_dir: str,
    project_root: Optional[str],
    uproject_path: Optional[str],
    primary_module: Optional[str],
) -> Dict[str, Any]:
    """统一推导并扁平化派生配置键"""
    raw["project_root"] = project_root
    if not raw.get("project_name"):
        raw["project_name"] = os.path.splitext(os.path.basename(uproject_path))[0] if uproject_path else (primary_module or "Game")
    if not raw.get("project_file") and uproject_path:
        raw["project_file"] = uproject_path

    if not raw.get("unreal_editor_cmd"):
        raw["unreal_editor_cmd"] = os.environ.get("UE_EDITOR_CMD") or _detect_unreal_editor_cmd(uproject_path)

    if not raw.get("engine_source_dir"):
        raw["engine_source_dir"] = _detect_engine_source_dir(uproject_path, raw.get("unreal_editor_cmd"))

    if not raw.get("clang_compiler"):
        raw["clang_compiler"] = _detect_clang_compiler()

    if not raw.get("data_tables_source_dir") and project_root:
        raw["data_tables_source_dir"] = os.path.join(project_root, "DataTables")

    if not raw.get("json_output_dir") and project_root:
        raw["json_output_dir"] = os.path.join(project_root, "DataTables_Json")

    if not raw.get("gameplay_tags_ini_path") and project_root:
        raw["gameplay_tags_ini_path"] = os.path.join(project_root, "Config", "DefaultGameplayTags.ini")

    if not raw.get("output_module_dir") and project_root:
        game_data_plugin_dir = os.path.join(project_root, "Plugins", "GameData", "Source", "GameData")
        if os.path.isdir(game_data_plugin_dir):
            raw["output_module_dir"] = game_data_plugin_dir
            if not raw.get("api_macro"):
                raw["api_macro"] = "GAMEDATA_API"
        else:
            mod_name = primary_module or "Game"
            raw["output_module_dir"] = os.path.join(project_root, "Source", mod_name)
            if not raw.get("api_macro"):
                raw["api_macro"] = f"{mod_name.upper()}_API"
    elif not raw.get("api_macro"):
        out_dir = str(raw.get("output_module_dir") or "")
        if "GameData" in out_dir:
            raw["api_macro"] = "GAMEDATA_API"
        elif primary_module:
            raw["api_macro"] = f"{primary_module.upper()}_API"
        else:
            raw["api_macro"] = "GAME_API"

    if not raw.get("subsystem_header_include"):
        raw["subsystem_header_include"] = "Generated/Data/StaticData/UStaticDataSubsystem.h"

    if not raw.get("json_root_dir_name"):
        json_out = raw.get("json_output_dir")
        raw["json_root_dir_name"] = os.path.basename(json_out) if json_out else "DataTables_Json"

    if not raw.get("static_data_bin_content_path"):
        raw["static_data_bin_content_path"] = "DataTables_Bin/StaticDataCache.bin"

    prefix = _infer_project_prefix(raw)
    raw["project_prefix"] = prefix

    if not raw.get("blueprint_category"):
        raw["blueprint_category"] = f"{prefix}|StaticData"

    log_cfg = raw.get("log_config", {}) if isinstance(raw.get("log_config"), dict) else {}
    raw["log_header"] = log_cfg.get("header") or f"Logging/{prefix}LogCategories.h"
    raw["log_macro"] = log_cfg.get("macro") or f"{prefix.upper()}_LOG"
    raw["log_category_name"] = log_cfg.get("category_name") or f"Log{prefix}"

    cmd_cfg = raw.get("console_commands", {}) if isinstance(raw.get("console_commands"), dict) else {}
    console_names = derive_console_names(prefix)
    raw["export_bin_command"] = cmd_cfg.get("export_bin") or console_names["export_bin"]
    raw["report_integrity_command"] = cmd_cfg.get("report_integrity") or console_names["report_integrity"]
    raw["static_data_channel_cmd_arg"] = cmd_cfg.get("channel_cmd_arg") or console_names["channel_cmd_arg"]
    raw["verify_launch_switch"] = cmd_cfg.get("verify_switch") or console_names["verify_switch"]
    raw["export_commandlet"] = cmd_cfg.get("export_commandlet") or console_names["export_commandlet"]

    row_key_mode = str(raw.get("row_key_mode") or "fname").strip().lower()
    if row_key_mode not in ("fname", "enum"):
        print(f"[配置错误] row_key_mode 仅支持 'fname' 或 'enum'，当前为: {row_key_mode}", file=sys.stderr)
        sys.exit(1)
    raw["row_key_mode"] = row_key_mode
    if not isinstance(raw.get("nested_struct_types"), dict):
        raw["nested_struct_types"] = {}

    reg_cfg = raw.get("registration", {}) if isinstance(raw.get("registration"), dict) else {}
    raw["registration_class_name"] = reg_cfg.get("class_name") or f"F{prefix}Registration"
    raw["game_instance_class_name"] = reg_cfg.get("game_instance_class") or f"U{prefix}GameInstance"

    default_reg_header = None
    default_source_roots = None
    if project_root:
        main_mod = primary_module or "Game"
        default_reg_header = os.path.join(project_root, "Source", main_mod, "Public", "Registration", f"{prefix}Registration.h")
        default_source_roots = [
            os.path.join(project_root, "Source"),
            os.path.join(project_root, "Plugins"),
        ]
    elif raw.get("output_module_dir"):
        default_reg_header = os.path.join(raw["output_module_dir"], "Public", "Registration", f"{prefix}Registration.h")
        default_source_roots = [raw["output_module_dir"]]

    if not isinstance(raw.get("registration"), dict):
        raw["registration"] = {}
    if not raw["registration"].get("output_header") and default_reg_header:
        raw["registration"]["output_header"] = default_reg_header
    if not raw["registration"].get("source_roots") and default_source_roots:
        raw["registration"]["source_roots"] = default_source_roots
    if not raw["registration"].get("class_name"):
        raw["registration"]["class_name"] = raw["registration_class_name"]
    if not raw["registration"].get("game_instance_class"):
        raw["registration"]["game_instance_class"] = raw["game_instance_class_name"]

    game_reg_cfg = raw.get("game_registration", {}) if isinstance(raw.get("game_registration"), dict) else {}
    raw["game_config_defaults"] = game_reg_cfg.get("config_defaults") or {}

    if not raw.get("row_serializer_register_func"):
        raw["row_serializer_register_func"] = f"Register{prefix}StaticDataRowSerializers"

    bw_cfg = raw.get("back_write", {}) if isinstance(raw.get("back_write"), dict) else {}
    raw["backup_dir"] = bw_cfg.get("backup_dir") or (
        os.path.normpath(os.path.join(project_root, "DataTables_Backup"))
        if project_root else os.path.normpath(os.path.join(config_dir, "../../DataTables_Backup"))
    )
    raw["max_backups"] = bw_cfg.get("max_backups", 10)
    raw["protect_formulas"] = bw_cfg.get("protect_formulas", True)

    return raw


def config_candidates(extra_dirs: Sequence[str] = ()) -> List[str]:
    """按优先级列出候选配置文件路径（环境变量 > extra_dirs > 当前目录 > Tools/Excel 目录）"""
    candidates: List[str] = [os.environ[key] for key in CONFIG_ENV_KEYS if os.environ.get(key)]
    candidates.extend(os.path.join(d, DEFAULT_CONFIG_FILENAME) for d in (*extra_dirs, os.getcwd(), TOOLS_EXCEL_DIR))
    return candidates


def resolve_config_path(explicit: Optional[str] = None, extra_dirs: Sequence[str] = ()) -> Optional[str]:
    """解析工具配置文件路径：显式参数优先（须存在），否则按 config_candidates 顺序取首个存在者；全部未命中返回 None"""
    if explicit:
        return explicit if os.path.isfile(explicit) else None
    for candidate in config_candidates(extra_dirs):
        if os.path.isfile(candidate):
            return candidate
    return None


def load_config(config_path: Optional[str], required_keys: Sequence[str]) -> Dict[str, Any]:
    """加载并校验工具配置"""
    resolved_path = resolve_config_path(config_path)
    if resolved_path is None:
        if config_path:
            print(f"[配置错误] 配置文件不存在: {config_path}", file=sys.stderr)
        else:
            tried = ", ".join(config_candidates())
            print(f"[配置错误] 未提供 --config，且自动发现失败（已尝试: {tried}）。"
                  f"请使用 --config <path> 或环境变量 {CONFIG_ENV_KEYS[0]} 指定配置文件。", file=sys.stderr)
        sys.exit(1)

    with open(resolved_path, "r", encoding="utf-8") as f:
        raw = json.load(f)
    if not isinstance(raw, dict):
        print("[配置错误] 配置文件必须为 JSON 对象。", file=sys.stderr)
        sys.exit(1)

    config_dir = os.path.dirname(os.path.abspath(resolved_path))
    project_root, uproject_path, primary_module = _find_project_root_and_file(config_dir)
    if not project_root:
        project_root, uproject_path, primary_module = _find_project_root_and_file(os.getcwd())

    _expand_relative_paths(raw, config_dir, project_root)
    _normalize_and_derive_config(raw, config_dir, project_root, uproject_path, primary_module)

    missing = [key for key in required_keys if not raw.get(key)]
    if missing:
        print(f"[配置错误] {resolved_path} 缺少必需配置键: {', '.join(missing)}"
              f"（参考字段说明见 Tools/Excel/generator_config.example.json）。", file=sys.stderr)
        sys.exit(1)
    return raw