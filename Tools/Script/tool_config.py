#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
统一工具配置加载器 (tool_config.py)

- 全部工具脚本（validate_tables / main / excel_to_json / gameplay_tags_to_ini / json_to_excel）
  只从配置文件读取路径与参数，脚本内不内置任何硬编码工程路径。
- 配置来源：--config <JSON 文件> > 环境变量 UE_TOOL_CONFIG > 当前目录 > Tools/Excel 目录。
- 必需键缺失时打印缺失清单并以非零退出码终止。
- 相对路径展开：以 `.` 开头的相对路径相对配置文件所在目录展开，
  其余相对路径在自动发现工程根（*.uproject）时相对工程根展开，否则相对配置文件所在目录展开。
"""

import glob
import json
import os
import shutil
import subprocess
import sys
from typing import Any, Dict, List, Optional, Sequence, Tuple

DEFAULT_CONFIG_FILENAME = "generator_config.json"

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TOOLS_DIR = os.path.dirname(SCRIPT_DIR)
TOOLS_EXCEL_DIR = os.path.join(TOOLS_DIR, "Excel")

CONFIG_ENV_KEYS = ("UE_TOOL_CONFIG",)

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
    "backup_dir",
    "unreal_editor_cmd",
    "clang_compiler",
)

SECTION_PATH_SPECS = (
    ("registration", "output_header", "single"),
    ("registration", "source_roots", "list"),
    ("back_write", "backup_dir", "single"),
)


def _find_project_root_and_file(start_dir: str) -> Tuple[Optional[str], Optional[str], Optional[str]]:
    """
    递归向上查找 *.uproject 并解析主模块名
    @param start_dir 起始查找目录
    @return (project_root, uproject_path, primary_module)
    """
    curr = os.path.abspath(start_dir)
    while True:
        try:
            candidates = sorted(
                f for f in os.listdir(curr)
                if f.endswith(".uproject") and os.path.isfile(os.path.join(curr, f))
            )
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
            except (OSError, json.JSONDecodeError, TypeError, ValueError):
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
    """
    相对路径展开为绝对路径
    @param config_dir 配置文件所在目录
    @param project_root 工程根目录（可选）
    @param value 待展开的路径配置值
    @return 归一化绝对路径
    """
    if not value:
        return value
    expanded = os.path.expanduser(os.path.expandvars(str(value)))
    if os.path.isabs(expanded):
        return os.path.normpath(expanded)
    if project_root and not expanded.startswith("."):
        return os.path.normpath(os.path.join(project_root, expanded))
    return os.path.normpath(os.path.join(config_dir, expanded))


def _expand_relative_paths(raw: Dict[str, Any], config_dir: str, project_root: Optional[str]) -> Dict[str, Any]:
    """
    展开配置中的顶层及分段相对路径
    @param raw 原始配置字典（原地更新）
    @param config_dir 配置文件目录
    @param project_root 工程根目录
    @return 更新后的配置字典
    """
    for key in PATH_KEYS:
        if raw.get(key):
            raw[key] = _resolve_path(config_dir, project_root, raw[key])
    for section, key, kind in SECTION_PATH_SPECS:
        section_obj = raw.get(section)
        if not isinstance(section_obj, dict):
            continue
        value = section_obj.get(key)
        if not value:
            continue
        if kind == "list" and isinstance(value, list):
            section_obj[key] = [_resolve_path(config_dir, project_root, item) for item in value]
        elif kind != "list":
            section_obj[key] = _resolve_path(config_dir, project_root, value)
    return raw


def _infer_project_prefix(raw: Dict[str, Any]) -> str:
    """
    推导项目前缀：显式 project_prefix 优先，缺省默认 Game
    @param raw 配置字典
    @return 项目前缀字符串
    """
    explicit = raw.get("project_prefix")
    if explicit:
        return str(explicit).strip()
    return "Game"


def _read_uproject_engine_association(uproject_path: Optional[str]) -> Optional[str]:
    """
    从 .uproject 读取 EngineAssociation 字段
    @param uproject_path .uproject 文件路径
    @return 引擎版本标签（未找到或解析失败返回 None）
    """
    if not uproject_path or not os.path.isfile(uproject_path):
        return None
    try:
        with open(uproject_path, "r", encoding="utf-8") as f:
            data = json.load(f)
        raw_assoc = data.get("EngineAssociation")
        return str(raw_assoc).strip() if raw_assoc else None
    except (OSError, json.JSONDecodeError, TypeError, ValueError):
        return None


def _query_windows_engine_root(engine_assoc: Optional[str]) -> Optional[str]:
    """
    在 Windows 注册表中查询源码版 UE 引擎根目录
    @param engine_assoc .uproject 的 EngineAssociation 值
    @return 引擎根目录路径，未命中返回 None
    """
    if sys.platform != "win32" or not engine_assoc:
        return None
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"SOFTWARE\Epic Games\Unreal Engine\Builds") as key:
            val, _ = winreg.QueryValueEx(key, engine_assoc)
            return str(val) if val else None
    except (ImportError, OSError):
        return None


def _platform_engine_roots(version_tags: Sequence[str]) -> List[str]:
    """
    按当前操作系统列出标准安装的 UE 引擎根目录候选列表
    @param version_tags 版本标签序列（如 ["5.8", "5.7", ...]）
    @return 候选引擎根目录列表
    """
    roots: List[str] = []
    for tag in version_tags:
        if not tag:
            continue
        if sys.platform == "darwin":
            roots.append(f"/Users/Shared/Epic Games/UE_{tag}")
            roots.append(f"/Applications/Epic Games/UE_{tag}")
        elif sys.platform == "win32":
            roots.append(f"C:/Program Files/Epic Games/UE_{tag}")
            roots.append(f"D:/Program Files/Epic Games/UE_{tag}")
            roots.append(f"E:/Program Files/Epic Games/UE_{tag}")
        else:
            roots.append(f"/opt/UnrealEngine/UE_{tag}")
            roots.append(os.path.expanduser(f"~/UnrealEngine/UE_{tag}"))
    return roots


def _editor_binary_candidates(engine_root: str) -> List[str]:
    """
    由引擎根目录（或 Engine 子目录）展开平台对应的编辑器可执行文件候选路径
    @param engine_root 引擎根目录
    @return 可执行文件候选路径列表
    """
    norm_root = os.path.normpath(engine_root)
    roots = [norm_root]
    if os.path.basename(norm_root) == "Engine":
        roots.insert(0, os.path.dirname(norm_root))
    candidates: List[str] = []
    for root in roots:
        if sys.platform == "darwin":
            candidates.extend([
                os.path.join(root, "Engine", "Binaries", "Mac", "UnrealEditor.app", "Contents", "MacOS", "UnrealEditor"),
                os.path.join(root, "Engine", "Binaries", "Mac", "UnrealEditor-Cmd"),
            ])
        elif sys.platform == "win32":
            candidates.append(os.path.join(root, "Engine", "Binaries", "Win64", "UnrealEditor-Cmd.exe"))
        else:
            candidates.append(os.path.join(root, "Engine", "Binaries", "Linux", "UnrealEditor"))
    return candidates


def _detect_unreal_editor_cmd(uproject_path: Optional[str]) -> str:
    """
    根据 UE_ENGINE_DIR、当前工程 .uproject 中的 EngineAssociation 或已安装 UE 5.8~5.1 探测虚幻编辑器
    @param uproject_path 工程 .uproject 路径
    @return 编辑器可执行文件绝对路径，未找到返回空串
    """
    env_dir = os.environ.get("UE_ENGINE_DIR")
    if env_dir:
        for path in _editor_binary_candidates(os.path.expanduser(os.path.expandvars(env_dir))):
            if os.path.isfile(path):
                return os.path.normpath(path)

    engine_assoc = _read_uproject_engine_association(uproject_path)
    if engine_assoc:
        for root in _platform_engine_roots([engine_assoc]):
            for path in _editor_binary_candidates(root):
                if os.path.isfile(path):
                    return os.path.normpath(path)
        win_root = _query_windows_engine_root(engine_assoc)
        if win_root:
            for path in _editor_binary_candidates(win_root):
                if os.path.isfile(path):
                    return os.path.normpath(path)

    for root in _platform_engine_roots([f"5.{m}" for m in range(8, 0, -1)]):
        for path in _editor_binary_candidates(root):
            if os.path.isfile(path):
                return os.path.normpath(path)

    return ""


def _detect_engine_source_dir(uproject_path: Optional[str], unreal_editor_cmd: Optional[str] = None) -> str:
    """
    智能推导虚幻引擎源码目录 (<Engine>/Source)
    @param uproject_path 工程 .uproject 路径
    @param unreal_editor_cmd 已探测的编辑器可执行路径（可选）
    @return Engine/Source 绝对路径，未找到返回空串
    """
    env_dir = os.environ.get("UE_ENGINE_DIR")
    if env_dir:
        expanded = os.path.expanduser(os.path.expandvars(env_dir))
        for sub in ("Source", os.path.join("Engine", "Source")):
            target = os.path.join(expanded, sub)
            if os.path.isdir(target):
                return os.path.normpath(target)
        if os.path.isdir(expanded):
            return os.path.normpath(expanded)

    engine_assoc = _read_uproject_engine_association(uproject_path)
    if engine_assoc:
        for root in _platform_engine_roots([engine_assoc]):
            candidate = os.path.join(root, "Engine", "Source")
            if os.path.isdir(candidate):
                return os.path.normpath(candidate)

        win_root = _query_windows_engine_root(engine_assoc)
        if win_root:
            for sub in (os.path.join("Engine", "Source"), "Source"):
                cand = os.path.join(win_root, sub)
                if os.path.isdir(cand):
                    return os.path.normpath(cand)

    if unreal_editor_cmd and os.path.isfile(unreal_editor_cmd):
        norm_cmd = os.path.normpath(unreal_editor_cmd)
        idx = norm_cmd.find(os.path.join("Engine", "Binaries"))
        if idx != -1:
            candidate = os.path.join(norm_cmd[:idx], "Engine", "Source")
            if os.path.isdir(candidate):
                return os.path.normpath(candidate)

    if sys.platform == "darwin":
        fallback_dirs = sorted(glob.glob("/Users/Shared/Epic Games/UE_*/Engine/Source"), reverse=True)
        if fallback_dirs:
            return os.path.normpath(fallback_dirs[0])
    elif sys.platform == "win32":
        for drive in ("C", "D", "E"):
            fallback_dirs = sorted(glob.glob(f"{drive}:/Program Files/Epic Games/UE_*/Engine/Source"), reverse=True)
            if fallback_dirs:
                return os.path.normpath(fallback_dirs[0])

    return ""


def _detect_clang_compiler() -> str:
    """
    智能探测 clang++ 编译器路径
    @return clang++ 绝对路径，未找到返回空串
    """
    env_path = os.environ.get("UE_CLANG_COMPILER")
    if env_path:
        expanded_env = os.path.expanduser(os.path.expandvars(env_path))
        if os.path.isfile(expanded_env):
            return os.path.normpath(os.path.abspath(expanded_env))

    if sys.platform == "darwin":
        try:
            detected = subprocess.check_output(["xcrun", "--find", "clang++"], text=True).strip()
            if detected and os.path.isfile(detected):
                return os.path.normpath(detected)
        except (OSError, subprocess.SubprocessError):
            pass

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
    """
    按项目前缀推导控制台命令 / 命令行开关的约定名（生成器与 Cook 工具共用同一真源）
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


def _derive_api_macro(output_module_dir: str, primary_module: Optional[str]) -> str:
    """
    根据输出模块目录末级目录名推导 UE 模块导出宏（如 .../Source/GameDataSamples -> GAMEDATASAMPLES_API）
    @param output_module_dir 输出模块目录
    @param primary_module 工程主模块名（回退用）
    @return UE 模块导出宏名
    """
    if output_module_dir:
        module_name = os.path.basename(os.path.normpath(output_module_dir))
        if module_name:
            return f"{module_name.upper()}_API"
    if primary_module:
        return f"{primary_module.upper()}_API"
    return "GAME_API"


def _normalize_and_derive_config(
    raw: Dict[str, Any],
    config_dir: str,
    project_root: Optional[str],
    uproject_path: Optional[str],
    primary_module: Optional[str],
) -> Dict[str, Any]:
    """
    统一推导并扁平化派生配置键
    @param raw 已展开路径的原始配置字典
    @param config_dir 配置文件所在目录
    @param project_root 工程根目录
    @param uproject_path .uproject 文件路径
    @param primary_module 工程主模块名
    @return 补全派生键后的配置字典
    """
    raw["project_root"] = project_root
    if not raw.get("project_name"):
        raw["project_name"] = os.path.splitext(os.path.basename(uproject_path))[0] if uproject_path else (primary_module or "Game")
    if not raw.get("project_file") and uproject_path:
        raw["project_file"] = uproject_path

    if not raw.get("unreal_editor_cmd"):
        env_editor = os.environ.get("UE_EDITOR_CMD")
        raw["unreal_editor_cmd"] = (
            os.path.normpath(os.path.expanduser(os.path.expandvars(env_editor)))
            if env_editor else _detect_unreal_editor_cmd(uproject_path)
        )

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
        else:
            mod_name = primary_module or "Game"
            raw["output_module_dir"] = os.path.join(project_root, "Source", mod_name)

    if not raw.get("api_macro"):
        raw["api_macro"] = _derive_api_macro(str(raw.get("output_module_dir") or ""), primary_module)

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
    raw["log_header"] = log_cfg.get("header") or raw.get("log_header") or "Logging/GameLogCategories.h"
    raw["log_macro"] = log_cfg.get("macro") or raw.get("log_macro") or "GAME_LOG"
    raw["log_category_name"] = log_cfg.get("category_name") or raw.get("log_category_name") or "LogGame"

    cmd_cfg = raw.get("console_commands", {}) if isinstance(raw.get("console_commands"), dict) else {}
    console_names = derive_console_names(prefix)
    raw["export_bin_command"] = cmd_cfg.get("export_bin") or raw.get("export_bin_command") or console_names["export_bin"]
    raw["report_integrity_command"] = cmd_cfg.get("report_integrity") or raw.get("report_integrity_command") or console_names["report_integrity"]
    raw["static_data_channel_cmd_arg"] = cmd_cfg.get("channel_cmd_arg") or raw.get("static_data_channel_cmd_arg") or console_names["channel_cmd_arg"]
    raw["verify_launch_switch"] = cmd_cfg.get("verify_switch") or raw.get("verify_launch_switch") or console_names["verify_switch"]
    raw["export_commandlet"] = cmd_cfg.get("export_commandlet") or raw.get("export_commandlet") or console_names["export_commandlet"]

    row_key_mode = str(raw.get("row_key_mode") or "fname").strip().lower()
    if row_key_mode not in ("fname", "enum"):
        print(f"[配置错误] row_key_mode 仅支持 'fname' 或 'enum'，当前为: {row_key_mode}", file=sys.stderr)
        sys.exit(1)
    raw["row_key_mode"] = row_key_mode
    if not isinstance(raw.get("nested_struct_types"), dict):
        raw["nested_struct_types"] = {}

    reg_cfg = raw.get("registration", {}) if isinstance(raw.get("registration"), dict) else {}
    raw["registration_class_name"] = reg_cfg.get("class_name") or raw.get("registration_class_name") or f"F{prefix}Registration"
    raw["game_instance_class_name"] = reg_cfg.get("game_instance_class") or raw.get("game_instance_class_name") or f"U{prefix}GameInstance"

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
    raw["game_config_defaults"] = game_reg_cfg.get("config_defaults") or raw.get("game_config_defaults") or {}

    if not raw.get("row_serializer_register_func"):
        raw["row_serializer_register_func"] = f"Register{prefix}StaticDataRowSerializers"

    bw_cfg = raw.get("back_write", {}) if isinstance(raw.get("back_write"), dict) else {}
    raw["backup_dir"] = bw_cfg.get("backup_dir") or raw.get("backup_dir") or (
        os.path.normpath(os.path.join(project_root, "DataTables_Backup"))
        if project_root else os.path.normpath(os.path.join(config_dir, "../../DataTables_Backup"))
    )
    raw["max_backups"] = bw_cfg.get("max_backups", raw.get("max_backups", 10))
    raw["protect_formulas"] = bw_cfg.get("protect_formulas", raw.get("protect_formulas", True))

    return raw


def config_candidates(extra_dirs: Sequence[str] = ()) -> List[str]:
    """
    按优先级列出候选配置文件路径（环境变量 > extra_dirs > 当前目录 > Tools/Excel 目录）
    @param extra_dirs 额外搜索目录序列
    @return 候选路径列表
    """
    candidates: List[str] = [os.environ[key] for key in CONFIG_ENV_KEYS if os.environ.get(key)]
    candidates.extend(os.path.join(d, DEFAULT_CONFIG_FILENAME) for d in (*extra_dirs, os.getcwd(), TOOLS_EXCEL_DIR))
    return candidates


def resolve_config_path(explicit: Optional[str] = None, extra_dirs: Sequence[str] = ()) -> Optional[str]:
    """
    解析工具配置文件路径：显式参数优先（须存在），否则按 config_candidates 顺序取首个存在者
    @param explicit 显式指定的配置文件路径
    @param extra_dirs 额外搜索目录序列
    @return 命中的配置文件路径，全部未命中返回 None
    """
    if explicit:
        return explicit if os.path.isfile(explicit) else None
    for candidate in config_candidates(extra_dirs):
        if os.path.isfile(candidate):
            return candidate
    return None


def load_config(config_path: Optional[str], required_keys: Sequence[str]) -> Dict[str, Any]:
    """
    加载并校验工具配置
    @param config_path 显式配置文件路径（可为 None，自动按优先级发现）
    @param required_keys 必需存在的配置键序列
    @return 补全派生键并校验通过的配置字典
    """
    resolved_path = resolve_config_path(config_path)
    if resolved_path is None:
        if config_path:
            print(f"[配置错误] 配置文件不存在: {config_path}", file=sys.stderr)
        else:
            tried = ", ".join(config_candidates())
            print(f"[配置错误] 未提供 --config，且自动发现失败（已尝试: {tried}）。"
                  f"请使用 --config <path> 或环境变量 {CONFIG_ENV_KEYS[0]} 指定配置文件。", file=sys.stderr)
        sys.exit(1)

    try:
        with open(resolved_path, "r", encoding="utf-8") as f:
            raw = json.load(f)
    except (OSError, json.JSONDecodeError, UnicodeDecodeError) as e:
        print(f"[配置错误] 读取或解析配置文件失败 {resolved_path}: {e}", file=sys.stderr)
        sys.exit(1)
    if not isinstance(raw, dict):
        print("[配置错误] 配置文件必须为 JSON 对象。", file=sys.stderr)
        sys.exit(1)

    config_dir = os.path.dirname(os.path.abspath(resolved_path))
    project_root, uproject_path, primary_module = _find_project_root_and_file(config_dir)
    if not project_root and config_path is None:
        project_root, uproject_path, primary_module = _find_project_root_and_file(os.getcwd())

    _expand_relative_paths(raw, config_dir, project_root)
    _normalize_and_derive_config(raw, config_dir, project_root, uproject_path, primary_module)

    missing = [key for key in required_keys if not raw.get(key)]
    if missing:
        print(f"[配置错误] {resolved_path} 缺少必需配置键: {', '.join(missing)}"
              f"（参考字段说明见 Tools/Excel/generator_config.example.json）。", file=sys.stderr)
        sys.exit(1)
    return raw
