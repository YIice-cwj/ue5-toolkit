#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
通用插件与工具链一键安装调度器 (install.py)

功能特性:
1. 自动解析插件 .uplugin 依赖，递归拓扑排序，按序自动补齐前置依赖；
2. 支持 --mode link（符号链接开发模式）与 --mode copy（独立发布物理拷贝模式）；
   GameData 生成宿主在未显式指定模式时默认 copy，避免生成代码跨工程软链接；
3. 以保格式文本补丁方式向目标工程 *.uproject 注入/移除插件启用项；
4. 支持通用工具链 (Tools/Excel) 的一键部署、生成器配置注入、示例数据与 ini 注入；
5. 声明 UE 5.1 ~ 5.8 版本约束，并在目标工程版本不符时给出明确错误；
6. 支持 --list 状态巡检与 --uninstall 安全卸载（默认交互确认）。
"""

import argparse
import json
import os
import re
import shutil
import sys
from typing import Dict, List, Optional, Tuple

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR) if os.path.basename(SCRIPT_DIR) == ".scripts" else SCRIPT_DIR
PLUGINS_DIR = os.path.join(REPO_ROOT, "Plugins")
TOOLS_DIR = os.path.join(REPO_ROOT, "Tools")

SAMPLE_PLUGINS = {"GameDataSamples"}
DEFAULT_COPY_PLUGINS = {"GameData"}
COPY_EXCLUDED_DIRS = {"Binaries", "Intermediate", "Saved", "DerivedDataCache", ".history"}

SUPPORTED_MINOR_MIN = 1
SUPPORTED_MINOR_MAX = 8
SUPPORTED_ENGINE_VERSION = f"5.{SUPPORTED_MINOR_MIN} ~ 5.{SUPPORTED_MINOR_MAX}"
SAMPLE_DATA_DIRS = ("DataTables", "DataTables_Json")
GENERATION_MODULE_DIR = "Plugins/GameData/Source/GameData"
GENERATION_API_MACRO = "GAMEDATA_API"
ABILITY_GLOBALS_SECTION = "/Script/GameplayAbilities.AbilitySystemGlobals"
ABILITY_GLOBALS_KEY = "AbilitySystemGlobalsClassName"
ABILITY_GLOBALS_VALUE = "/Script/GameGameplay.GameAbilitySystemGlobals"
PACKAGING_SECTION = "/Script/UnrealEd.ProjectPackagingSettings"
STAGING_BIN_ENTRY = ("+DirectoriesToAlwaysStageAsNonUFS", '(Path="DataTables_Bin")')


def find_target_uproject(project_dir: str) -> Tuple[Optional[str], Optional[str]]:
    """在目标目录下查找 *.uproject 文件，返回 (project_root, uproject_path)"""
    curr = os.path.abspath(project_dir)
    while True:
        try:
            candidates = [f for f in os.listdir(curr) if f.endswith(".uproject") and os.path.isfile(os.path.join(curr, f))]
        except OSError:
            candidates = []
        if candidates:
            return curr, os.path.join(curr, candidates[0])
        parent = os.path.dirname(curr)
        if parent == curr:
            break
        curr = parent
    return None, None


def discover_available_plugins() -> Dict[str, str]:
    """发现当前仓库内所有可用插件，返回 {插件名: 插件绝对目录}"""
    result = {}
    if not os.path.isdir(PLUGINS_DIR):
        return result
    for name in sorted(os.listdir(PLUGINS_DIR)):
        plugin_path = os.path.join(PLUGINS_DIR, name)
        uplugin = os.path.join(plugin_path, f"{name}.uplugin")
        if os.path.isdir(plugin_path) and os.path.isfile(uplugin):
            result[name] = plugin_path
    return result


def discover_available_tools() -> Dict[str, str]:
    """发现当前仓库内所有可用工具链，返回 {工具名: 工具绝对目录}"""
    result = {}
    if not os.path.isdir(TOOLS_DIR):
        return result
    for name in sorted(os.listdir(TOOLS_DIR)):
        tool_path = os.path.join(TOOLS_DIR, name)
        if os.path.isdir(tool_path) and name not in ("Script", "Common") and not name.startswith("."):
            result[name] = tool_path
    return result


def get_plugin_internal_dependencies(plugin_name: str, available_plugins: Dict[str, str]) -> List[str]:
    """从 .uplugin 中解析出当前仓库内已有的前置依赖插件清单"""
    plugin_path = available_plugins.get(plugin_name)
    if not plugin_path:
        return []
    uplugin_file = os.path.join(plugin_path, f"{plugin_name}.uplugin")
    if not os.path.isfile(uplugin_file):
        return []
    try:
        with open(uplugin_file, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception:
        return []

    plugins_field = data.get("Plugins", [])
    dependencies = []
    if isinstance(plugins_field, list):
        for item in plugins_field:
            if isinstance(item, dict):
                dep_name = item.get("Name")
                b_enabled = item.get("Enabled", True)
                if dep_name and b_enabled and dep_name in available_plugins and dep_name != plugin_name:
                    dependencies.append(dep_name)
    return dependencies


def resolve_topological_order(targets: List[str], available_plugins: Dict[str, str]) -> List[str]:
    """计算安装顺序（拓扑排序，被依赖项优先安装）"""
    visited: set = set()
    order: List[str] = []

    def dfs(name: str):
        if name in visited:
            return
        visited.add(name)
        for dep in get_plugin_internal_dependencies(name, available_plugins):
            dfs(dep)
        order.append(name)

    for target in targets:
        if target in available_plugins:
            dfs(target)
    return order


def resolve_plugin_mode(plugin_name: str, requested_mode: Optional[str]) -> str:
    """解析插件安装模式：显式 mode 优先，否则 GameData 默认 copy，其余 link"""
    if requested_mode:
        return requested_mode
    return "copy" if plugin_name in DEFAULT_COPY_PLUGINS else "link"


def check_engine_version(uproject_path: str) -> bool:
    """校验目标工程的 EngineAssociation 是否在 UE 5.1 ~ 5.8 范围内（源码构建 GUID 仅提示不阻断）"""
    try:
        with open(uproject_path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception as e:
        print(f"[!] 无法读取目标工程文件 {uproject_path}: {e}", file=sys.stderr)
        return False

    association = str(data.get("EngineAssociation", "")).strip()
    if not association:
        print(f"[!] 目标工程未声明 EngineAssociation，请确认使用 UE {SUPPORTED_ENGINE_VERSION} 编译。")
        return True

    if re.match(r"^\d+(\.\d+)*$", association):
        parts = [int(p) for p in association.split(".")]
        major = parts[0]
        minor = parts[1] if len(parts) > 1 else -1
        if major != 5 or not (SUPPORTED_MINOR_MIN <= minor <= SUPPORTED_MINOR_MAX):
            print(
                f"[错误] 本资产库仅支持 UE {SUPPORTED_ENGINE_VERSION}，"
                f"目标工程 EngineAssociation 为 {association}。已中止安装。",
                file=sys.stderr,
            )
            return False
        return True

    print(f"[!] 目标工程使用源码构建（EngineAssociation={association}），请确认引擎版本为 UE {SUPPORTED_ENGINE_VERSION}。")
    return True


def _skip_json_string(text: str, index: int) -> int:
    """跳过以 index 处引号开始的 JSON 字符串，返回结束引号之后的位置"""
    i = index + 1
    while i < len(text):
        if text[i] == "\\":
            i += 2
            continue
        if text[i] == '"':
            return i + 1
        i += 1
    return i


def _find_matching(text: str, open_index: int, open_char: str, close_char: str) -> int:
    """在 JSON 文本中寻找与 open_index 处括号匹配的闭括号位置（跳过字符串内容）"""
    depth = 0
    i = open_index
    while i < len(text):
        ch = text[i]
        if ch == '"':
            i = _skip_json_string(text, i)
            continue
        if ch == open_char:
            depth += 1
        elif ch == close_char:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def _find_plugins_array_span(text: str) -> Optional[Tuple[int, int]]:
    """定位 \"Plugins\" 数组的 [ 与 ] 位置"""
    marker = re.search(r'"Plugins"\s*:\s*\[', text)
    if not marker:
        return None
    open_index = marker.end() - 1
    close_index = _find_matching(text, open_index, "[", "]")
    if close_index < 0:
        return None
    return open_index, close_index


def _iter_object_spans(text: str, array_open: int, array_close: int) -> List[Tuple[int, int]]:
    """遍历 Plugins 数组内的顶层对象区间（左闭右开）"""
    spans: List[Tuple[int, int]] = []
    i = array_open + 1
    while i < array_close:
        ch = text[i]
        if ch == '"':
            i = _skip_json_string(text, i)
            continue
        if ch == "{":
            obj_close = _find_matching(text, i, "{", "}")
            if obj_close < 0:
                break
            spans.append((i, obj_close + 1))
            i = obj_close + 1
            continue
        i += 1
    return spans


def _object_plugin_name(text: str, start: int, end: int) -> Optional[str]:
    """读取插件对象内的 Name 字段"""
    match = re.search(r'"Name"\s*:\s*"([^"]+)"', text[start:end])
    return match.group(1) if match else None


def _line_indent(text: str, position: int) -> str:
    """返回 position 所在行首的纯空白缩进（若行首到 position 含非空白则返回空串）"""
    line_start = text.rfind("\n", 0, position) + 1
    prefix = text[line_start:position]
    return prefix if prefix.strip() == "" else ""


def _detect_indent_style(text: str, array_open: int, array_close: int, spans: List[Tuple[int, int]]) -> Tuple[str, str, str]:
    """推导对象缩进、成员缩进与单级缩进单位"""
    if spans:
        object_indent = _line_indent(text, spans[0][0])
        match = re.search(r"\n([ \t]+)\"", text[spans[0][0]:spans[0][1]])
        member_indent = match.group(1) if match else object_indent + "\t"
        if member_indent.startswith(object_indent) and len(member_indent) > len(object_indent):
            unit = member_indent[len(object_indent):]
        else:
            unit = "\t"
        return object_indent, member_indent, unit

    close_indent = _line_indent(text, array_close)
    unit = "\t"
    object_indent = close_indent + unit
    return object_indent, object_indent + unit, unit


def _insert_plugin_entry(text: str, plugin_name: str) -> str:
    """在 Plugins 数组中保格式追加一个启用项"""
    span = _find_plugins_array_span(text)
    if span is None:
        root_close = text.rstrip().rfind("}")
        if root_close < 0:
            return text
        insert_pos = root_close
        while insert_pos > 0 and text[insert_pos - 1].isspace():
            insert_pos -= 1
        has_members = text[insert_pos - 1] not in "{[" if insert_pos > 0 else False
        separator = "," if has_members else ""
        member_indent_match = re.search(r'\n([ \t]+)"', text[:root_close])
        base_indent = member_indent_match.group(1) if member_indent_match else "\t"
        unit = "\t"
        entry = (
            f'{separator}\n{base_indent}"Plugins": [\n'
            f'{base_indent}{unit}{{\n'
            f'{base_indent}{unit}{unit}"Name": "{plugin_name}",\n'
            f'{base_indent}{unit}{unit}"Enabled": true\n'
            f'{base_indent}{unit}}}\n'
            f'{base_indent}]'
        )
        return text[:insert_pos] + entry + text[insert_pos:]

    array_open, array_close = span
    spans = _iter_object_spans(text, array_open, array_close)
    object_indent, member_indent, _ = _detect_indent_style(text, array_open, array_close, spans)
    entry = (
        f'{object_indent}{{\n'
        f'{member_indent}"Name": "{plugin_name}",\n'
        f'{member_indent}"Enabled": true\n'
        f'{object_indent}}}'
    )

    if spans:
        insert_pos = spans[-1][1]
        return text[:insert_pos] + ",\n" + entry + text[insert_pos:]

    close_indent = _line_indent(text, array_close)
    return text[:array_open + 1] + "\n" + entry + "\n" + close_indent + text[array_open + 1:]


def _enable_plugin_entry(text: str, object_start: int, object_end: int) -> str:
    """把既有插件对象内的 Enabled 置为 true（缺失时补字段）"""
    segment = text[object_start:object_end]
    if re.search(r'"Enabled"\s*:\s*true', segment):
        return text

    enabled_match = re.search(r'("Enabled"\s*:\s*)false', segment)
    if enabled_match:
        new_segment = segment[:enabled_match.start()] + enabled_match.group(1) + "true" + segment[enabled_match.end():]
        return text[:object_start] + new_segment + text[object_end:]

    close_index = object_end - 1
    body = text[object_start + 1:close_index]
    if body.strip() == "":
        object_indent = _line_indent(text, object_start)
        member_indent = object_indent + "\t"
        replacement = "{\n" + member_indent + '"Enabled": true\n' + object_indent + "}"
        return text[:object_start] + replacement + text[object_end:]

    member_match = re.search(r"\n([ \t]+)\"", body)
    object_indent = _line_indent(text, object_start)
    member_indent = member_match.group(1) if member_match else object_indent + "\t"
    stripped_body = body.rstrip()
    trailing = body[len(stripped_body):]
    new_body = stripped_body + ",\n" + member_indent + '"Enabled": true' + trailing
    return text[:object_start + 1] + new_body + text[close_index:]


def _remove_plugin_entry(text: str, object_start: int, object_end: int) -> str:
    """从 Plugins 数组中保格式移除一个插件对象"""
    start = object_start
    end = object_end

    i = start - 1
    while i >= 0 and text[i] in " \t\r\n":
        i -= 1
    if i >= 0 and text[i] == ",":
        start = i
    else:
        j = end
        while j < len(text) and text[j] in " \t\r\n":
            j += 1
        if j < len(text) and text[j] == ",":
            end = j + 1

    span = _find_plugins_array_span(text)
    if span is not None:
        array_open, _ = span
        if text[array_open + 1:start].strip() == "":
            start = array_open + 1

    return text[:start] + text[end:]


def patch_uproject_plugin(uproject_path: str, plugin_name: str, b_enable: bool) -> bool:
    """以保格式文本补丁方式向目标工程 *.uproject 添加、启用或移除插件条目"""
    if not os.path.isfile(uproject_path):
        return False
    try:
        with open(uproject_path, "r", encoding="utf-8") as f:
            text = f.read()
        json.loads(text)
    except Exception as e:
        print(f"[!] 无法读取目标工程文件 {uproject_path}: {e}", file=sys.stderr)
        return False

    span = _find_plugins_array_span(text)
    target_span: Optional[Tuple[int, int]] = None
    if span is not None:
        for object_start, object_end in _iter_object_spans(text, span[0], span[1]):
            if _object_plugin_name(text, object_start, object_end) == plugin_name:
                target_span = (object_start, object_end)
                break

    if b_enable:
        if target_span is not None:
            text = _enable_plugin_entry(text, target_span[0], target_span[1])
        else:
            text = _insert_plugin_entry(text, plugin_name)
    elif target_span is not None:
        text = _remove_plugin_entry(text, target_span[0], target_span[1])

    try:
        json.loads(text)
    except Exception as e:
        print(f"[!] .uproject 补丁后 JSON 校验失败，已放弃写入: {e}", file=sys.stderr)
        return False

    try:
        with open(uproject_path, "w", encoding="utf-8") as f:
            f.write(text)
        return True
    except Exception as e:
        print(f"[!] 写入目标工程文件失败 {uproject_path}: {e}", file=sys.stderr)
        return False


def link_or_copy_dir(src: str, dst: str, mode: str) -> bool:
    """创建软链接或全量拷贝目录"""
    if os.path.islink(dst) or os.path.isfile(dst):
        os.remove(dst)
    elif os.path.isdir(dst):
        shutil.rmtree(dst)

    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if mode == "link":
        try:
            rel_src = os.path.relpath(src, os.path.dirname(dst))
            os.symlink(rel_src, dst, target_is_directory=True)
            return True
        except (OSError, NotImplementedError):
            print(f"[-] 符号链接创建受限，降级为物理拷贝: {dst}")
            mode = "copy"

    if mode == "copy":
        shutil.copytree(
            src,
            dst,
            ignore=shutil.ignore_patterns(*COPY_EXCLUDED_DIRS),
        )
        return True
    return False


def inject_ini_setting(ini_path: str, section: str, key: str, value: str) -> bool:
    """向 ini 幂等注入单个键值（保留既有内容与换行风格）"""
    try:
        text = ""
        if os.path.isfile(ini_path):
            with open(ini_path, "r", encoding="utf-8") as f:
                text = f.read()
    except OSError as e:
        print(f"[!] 无法读取 ini {ini_path}: {e}", file=sys.stderr)
        return False

    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.splitlines()
    setting_line = f"{key}={value}"
    section_index = next((i for i, line in enumerate(lines) if line.strip() == f"[{section}]"), None)

    if section_index is None:
        if lines and lines[-1].strip():
            lines.append("")
        lines.extend([f"[{section}]", setting_line])
    else:
        next_section = next(
            (i for i in range(section_index + 1, len(lines)) if lines[i].strip().startswith("[")),
            len(lines),
        )
        key_index = next(
            (i for i in range(section_index + 1, next_section) if lines[i].split("=", 1)[0].strip() == key),
            None,
        )
        if key_index is not None:
            lines[key_index] = setting_line
        else:
            lines.insert(next_section, setting_line)

    try:
        os.makedirs(os.path.dirname(ini_path), exist_ok=True)
        with open(ini_path, "w", encoding="utf-8") as f:
            f.write(newline.join(lines) + newline)
        return True
    except OSError as e:
        print(f"[!] 写入 ini 失败 {ini_path}: {e}", file=sys.stderr)
        return False


def inject_ini_array_entry(ini_path: str, section: str, key: str, entry: str) -> bool:
    """向 ini 的数组键幂等追加一条条目（保留既有内容与换行风格）"""
    try:
        text = ""
        if os.path.isfile(ini_path):
            with open(ini_path, "r", encoding="utf-8") as f:
                text = f.read()
    except OSError as e:
        print(f"[!] 无法读取 ini {ini_path}: {e}", file=sys.stderr)
        return False

    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.splitlines()
    entry_line = f"{key}={entry}"
    section_header = f"[{section}]"
    section_index = next((i for i, line in enumerate(lines) if line.strip() == section_header), None)

    if section_index is None:
        if lines and lines[-1].strip():
            lines.append("")
        lines.extend([section_header, entry_line])
    else:
        next_section = next(
            (i for i in range(section_index + 1, len(lines)) if lines[i].strip().startswith("[")),
            len(lines),
        )
        if any(lines[i].strip() == entry_line for i in range(section_index + 1, next_section)):
            return True
        lines.insert(next_section, entry_line)

    try:
        os.makedirs(os.path.dirname(ini_path), exist_ok=True)
        with open(ini_path, "w", encoding="utf-8") as f:
            f.write(newline.join(lines) + newline)
        return True
    except OSError as e:
        print(f"[!] 写入 ini 失败 {ini_path}: {e}", file=sys.stderr)
        return False


def configure_generator_config(target_project_root: str) -> None:
    """把目标工程内的 Excel 生成器配置绑定到 GameData 生成宿主"""
    if not os.path.isdir(os.path.join(target_project_root, "Plugins", "GameData")):
        return

    config_path = os.path.join(target_project_root, "Tools", "Excel", "generator_config.json")
    example_path = os.path.join(target_project_root, "Tools", "Excel", "generator_config.example.json")
    source_path = config_path if os.path.isfile(config_path) else example_path
    if not os.path.isfile(source_path):
        print("[-] 未找到目标工程 Excel 生成器配置，跳过配置注入。")
        return

    try:
        with open(source_path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception as e:
        print(f"[!] 读取生成器配置失败，跳过注入: {e}", file=sys.stderr)
        return

    data["data_tables_source_dir"] = data.get("data_tables_source_dir", "DataTables")
    data["json_output_dir"] = data.get("json_output_dir", "DataTables_Json")
    data["gameplay_tags_ini_path"] = data.get("gameplay_tags_ini_path", "Config/DefaultGameplayTags.ini")
    data["output_module_dir"] = GENERATION_MODULE_DIR
    data["api_macro"] = GENERATION_API_MACRO
    data.setdefault("packaged_app_path", "")

    try:
        os.makedirs(os.path.dirname(config_path), exist_ok=True)
        with open(config_path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=4, ensure_ascii=False)
            f.write("\n")
        print(f"[✓] 生成器配置已绑定: {os.path.relpath(config_path, target_project_root)} -> {GENERATION_MODULE_DIR}")
    except OSError as e:
        print(f"[!] 写入生成器配置失败: {e}", file=sys.stderr)


def inject_project_settings(target_project_root: str) -> None:
    """注入 GameplayAbilities Globals 类配置（GameGameplay 上下文工厂生效前提）"""
    ini_path = os.path.join(target_project_root, "Config", "DefaultGame.ini")
    if inject_ini_setting(ini_path, ABILITY_GLOBALS_SECTION, ABILITY_GLOBALS_KEY, ABILITY_GLOBALS_VALUE):
        print(f"[✓] 已注入 {ABILITY_GLOBALS_SECTION} -> {ABILITY_GLOBALS_VALUE}")


def inject_packaging_settings(target_project_root: str) -> None:
    """注入 .bin 非资产目录的打包 staging 配置（发布包以 .bin 为运行期主通道）

    JSON 通道仅用于开发期热更与 QA 对拍，不随发布包分发（避免明文数据外泄），
    需要时由 QA 脚本把 JSON 目录拷到打包产物同级目录。
    """
    ini_path = os.path.join(target_project_root, "Config", "DefaultGame.ini")
    key, entry = STAGING_BIN_ENTRY
    if inject_ini_array_entry(ini_path, PACKAGING_SECTION, key, entry):
        print(f"[✓] 已注入打包 staging: {key}={entry}")


def install_sample_data(target_project_root: str) -> None:
    """目标工程缺少数据目录时注入示例 Excel 与配套 JSON（已存在则完全不动）"""
    for dir_name in SAMPLE_DATA_DIRS:
        src_dir = os.path.join(REPO_ROOT, dir_name)
        dst_dir = os.path.join(target_project_root, dir_name)
        if not os.path.isdir(src_dir) or os.path.exists(dst_dir):
            continue
        try:
            shutil.copytree(src_dir, dst_dir)
            print(f"[✓] 已注入示例数据目录: {dir_name}")
        except OSError as e:
            print(f"[!] 示例数据注入失败 {dir_name}: {e}", file=sys.stderr)


def install_plugin(
    plugin_name: str,
    target_project_root: str,
    target_uproject_path: str,
    mode: Optional[str],
    available_plugins: Dict[str, str],
) -> bool:
    """安装指定插件并更新目标工程 *.uproject"""
    src_dir = available_plugins[plugin_name]
    dst_dir = os.path.join(target_project_root, "Plugins", plugin_name)
    effective_mode = resolve_plugin_mode(plugin_name, mode)

    print(f"[+] 正在安装插件: {plugin_name} ({effective_mode}) -> {dst_dir}")
    if not link_or_copy_dir(src_dir, dst_dir, effective_mode):
        print(f"[✗] 插件安装失败: {plugin_name}", file=sys.stderr)
        return False

    if not patch_uproject_plugin(target_uproject_path, plugin_name, True):
        print(f"[!] 插件已落盘但 .uproject 启用项写入失败: {plugin_name}", file=sys.stderr)
        return False

    if plugin_name == "GameData":
        configure_generator_config(target_project_root)
        inject_packaging_settings(target_project_root)
    if plugin_name == "GameGameplay":
        inject_project_settings(target_project_root)

    print(f"[✓] 插件 {plugin_name} 已安装并在 {os.path.basename(target_uproject_path)} 中启用。")
    return True


def uninstall_plugin(
    plugin_name: str,
    target_project_root: str,
    target_uproject_path: str,
) -> bool:
    """卸载指定插件并清理目标工程 *.uproject"""
    dst_dir = os.path.join(target_project_root, "Plugins", plugin_name)
    if os.path.islink(dst_dir) or os.path.isfile(dst_dir):
        os.remove(dst_dir)
        print(f"[×] 已移除插件软链接: {dst_dir}")
    elif os.path.isdir(dst_dir):
        shutil.rmtree(dst_dir)
        print(f"[×] 已移除插件物理目录: {dst_dir}")
    else:
        print(f"[-] 目标工程中未找到插件: {plugin_name}")

    patch_uproject_plugin(target_uproject_path, plugin_name, False)
    print(f"[✓] 插件 {plugin_name} 已在 {os.path.basename(target_uproject_path)} 中注销。")
    return True


def install_tool(
    tool_name: str,
    target_project_root: str,
    mode: Optional[str],
    available_tools: Dict[str, str],
) -> bool:
    """安装指定工具链至目标工程 Tools/"""
    src_dir = available_tools[tool_name]
    dst_dir = os.path.join(target_project_root, "Tools", tool_name)
    effective_mode = mode or "link"

    existing_config = None
    existing_config_path = os.path.join(dst_dir, "generator_config.json")
    if tool_name == "Excel" and not os.path.islink(dst_dir) and os.path.isfile(existing_config_path):
        try:
            with open(existing_config_path, "r", encoding="utf-8") as f:
                existing_config = f.read()
        except OSError:
            existing_config = None

    print(f"[+] 正在部署工具链: {tool_name} ({effective_mode}) -> {dst_dir}")
    if not link_or_copy_dir(src_dir, dst_dir, effective_mode):
        print(f"[✗] 工具链部署失败: {tool_name}", file=sys.stderr)
        return False

    if tool_name == "Excel":
        if existing_config is not None and not os.path.islink(dst_dir):
            try:
                with open(existing_config_path, "w", encoding="utf-8") as f:
                    f.write(existing_config)
                print("[✓] 已保留目标工程既有 Excel 生成器配置。")
            except OSError as e:
                print(f"[!] 保留生成器配置失败: {e}", file=sys.stderr)

        default_cfg = os.path.join(dst_dir, "generator_config.json")
        example_cfg = os.path.join(dst_dir, "generator_config.example.json")
        if not os.path.isfile(default_cfg) and os.path.isfile(example_cfg):
            try:
                shutil.copy(example_cfg, default_cfg)
                print("[✓] 已从示例创建 Excel 生成器配置。")
            except OSError as e:
                print(f"[!] 创建生成器配置失败: {e}", file=sys.stderr)

        if os.path.islink(dst_dir):
            print("[-] link 模式与仓库共享生成器配置，跳过工程专属配置注入（发布请用 --mode copy）。")
        else:
            configure_generator_config(target_project_root)

    for common_item in ("common.sh", "common.bat", "requirements.txt", "Script"):
        c_src = os.path.join(TOOLS_DIR, common_item)
        c_dst = os.path.join(target_project_root, "Tools", common_item)
        if not os.path.exists(c_src) or os.path.exists(c_dst):
            continue
        if os.path.isfile(c_src):
            os.makedirs(os.path.dirname(c_dst), exist_ok=True)
            if effective_mode == "link":
                try:
                    os.symlink(os.path.relpath(c_src, os.path.dirname(c_dst)), c_dst)
                    continue
                except OSError:
                    pass
            shutil.copy2(c_src, c_dst)
        else:
            link_or_copy_dir(c_src, c_dst, effective_mode)

    print(f"[✓] 工具链 {tool_name} 部署完成。")
    return True


def uninstall_tool(
    tool_name: str,
    target_project_root: str,
) -> bool:
    """卸载指定工具链"""
    dst_dir = os.path.join(target_project_root, "Tools", tool_name)
    if os.path.islink(dst_dir) or os.path.isfile(dst_dir):
        os.remove(dst_dir)
        print(f"[×] 已移除工具链软链接: {dst_dir}")
    elif os.path.isdir(dst_dir):
        shutil.rmtree(dst_dir)
        print(f"[×] 已移除工具链物理目录: {dst_dir}")
    else:
        print(f"[-] 目标工程中未找到工具链: {tool_name}")
    print(f"[✓] 工具链 {tool_name} 已卸载。")
    return True


def print_status(
    target_project_root: Optional[str],
    available_plugins: Dict[str, str],
    available_tools: Dict[str, str],
) -> None:
    """打印当前仓库与目标工程的插件/工具状态"""
    print("=" * 65)
    print(" 通用资产仓库资产清单与安装状态巡检")
    print(f" 仓库源路径 : {REPO_ROOT}")
    if target_project_root:
        print(f" 目标工程   : {target_project_root}")
    print(f" 引擎约束   : UE {SUPPORTED_ENGINE_VERSION}")
    print("=" * 65)

    print("\n[可用插件]")
    for name in available_plugins:
        deps = get_plugin_internal_dependencies(name, available_plugins)
        dep_str = f" (前置依赖: {', '.join(deps)})" if deps else ""
        status_str = ""
        if target_project_root:
            target_path = os.path.join(target_project_root, "Plugins", name)
            if os.path.islink(target_path):
                status_str = " -> [已安装: 软链接]"
            elif os.path.isdir(target_path):
                status_str = " -> [已安装: 物理拷贝]"
            else:
                status_str = " -> [未安装]"
        print(f"  • {name}{dep_str}{status_str}")

    print("\n[可用工具链]")
    for name in available_tools:
        status_str = ""
        if target_project_root:
            target_path = os.path.join(target_project_root, "Tools", name)
            if os.path.islink(target_path):
                status_str = " -> [已部署: 软链接]"
            elif os.path.isdir(target_path):
                status_str = " -> [已部署: 物理拷贝]"
            else:
                status_str = " -> [未部署]"
        print(f"  • Tools/{name}{status_str}")
    print("=" * 65)


def confirm_uninstall(action: str) -> bool:
    """卸载前交互确认；非交互环境必须显式 --yes"""
    if not sys.stdin.isatty():
        print("[错误] 非交互环境执行卸载必须显式追加 --yes。", file=sys.stderr)
        return False
    answer = input(f"即将{action}，输入 yes 确认继续: ").strip().lower()
    return answer in ("yes", "y")


def main() -> None:
    parser = argparse.ArgumentParser(description="通用插件与工具链一键安装调度器")
    parser.add_argument("target", nargs="?", default=None, help="目标插件名 (如 GameData) 或工具名 (如 Tools/Excel)")
    parser.add_argument("project_path", nargs="?", default=None, help="目标虚幻工程目录 (含 *.uproject)")
    parser.add_argument("--mode", choices=("link", "copy"), default=None, help="安装模式；缺省时 GameData=copy，其余=link")
    parser.add_argument("--list", action="store_true", help="列出仓库内资产清单与安装状态")
    parser.add_argument("--all", action="store_true", help="一键安装本仓库内的全部插件与工具链")
    parser.add_argument("--uninstall", action="store_true", help="卸载指定插件或工具")
    parser.add_argument("--yes", action="store_true", help="卸载时跳过交互确认")
    parser.add_argument("--skip-sample-data", action="store_true", help="--all 安装时不注入示例数据目录")
    args = parser.parse_args()

    available_plugins = discover_available_plugins()
    available_tools = discover_available_tools()

    target_project_root = None
    target_uproject = None
    if args.all and args.target and not args.project_path:
        search_dir = args.target
    else:
        search_dir = args.project_path or os.getcwd()
    target_project_root, target_uproject = find_target_uproject(search_dir)

    if args.list:
        print_status(target_project_root, available_plugins, available_tools)
        return

    if not target_project_root or not target_uproject:
        print(f"[错误] 未在目标路径中找到有效的虚幻工程 (*.uproject): {search_dir}", file=sys.stderr)
        print("用法示例: ./.scripts/install.sh GameData /path/to/MyGameProject", file=sys.stderr)
        sys.exit(1)

    if os.path.normpath(target_project_root) == os.path.normpath(REPO_ROOT):
        print("[错误] 目标工程不能是资产仓库自身，已中止以避免覆盖仓库内插件源码。", file=sys.stderr)
        sys.exit(1)

    if not check_engine_version(target_uproject):
        sys.exit(1)

    print("=" * 65)
    print(" 通用资产调度管线")
    print(f" 目标工程   : {target_project_root} ({os.path.basename(target_uproject)})")
    print(f" 引擎版本   : UE {SUPPORTED_ENGINE_VERSION}")
    default_mode_desc = args.mode or "自动 (GameData=copy, 其余=link)"
    print(f" 操作模式   : {'卸载' if args.uninstall else '安装 (' + default_mode_desc + ')'}")
    print("=" * 65)

    if args.uninstall and not args.yes:
        action_desc = "卸载全部通用资产" if args.all else f"卸载 {args.target}"
        if not confirm_uninstall(action_desc):
            print("[-] 已取消卸载。")
            sys.exit(1)

    if args.all:
        if args.uninstall:
            for name in reversed(resolve_topological_order(list(available_plugins.keys()), available_plugins)):
                if name in SAMPLE_PLUGINS:
                    continue
                uninstall_plugin(name, target_project_root, target_uproject)
            for tool in available_tools:
                uninstall_tool(tool, target_project_root)
        else:
            install_order = resolve_topological_order(list(available_plugins.keys()), available_plugins)
            for name in install_order:
                if name in SAMPLE_PLUGINS:
                    print(f"[-] 跳过示例插件（不随通用安装部署）: {name}")
                    continue
                install_plugin(name, target_project_root, target_uproject, args.mode, available_plugins)
            for tool in available_tools:
                install_tool(tool, target_project_root, args.mode, available_tools)
            if not args.skip_sample_data:
                install_sample_data(target_project_root)
        print("\n[🎉] 全量资产调度完成！")
        return

    if not args.target:
        print("[错误] 请指定要操作的插件名 (如 GameData) 或工具名 (如 Tools/Excel)，或使用 --all / --list。", file=sys.stderr)
        sys.exit(1)

    target_name = args.target.replace("Tools/", "").replace("Plugins/", "")

    if target_name in available_tools:
        if args.uninstall:
            uninstall_tool(target_name, target_project_root)
        else:
            install_tool(target_name, target_project_root, args.mode, available_tools)
        return

    if target_name in available_plugins:
        if args.uninstall:
            uninstall_plugin(target_name, target_project_root, target_uproject)
        else:
            install_order = resolve_topological_order([target_name], available_plugins)
            if len(install_order) > 1:
                print(f"[依赖解析] 检测到前置依赖链: {' -> '.join(install_order)}")
            for name in install_order:
                install_plugin(name, target_project_root, target_uproject, args.mode, available_plugins)
        return

    print(f"[错误] 未在仓库中找到名为 '{args.target}' 的插件或工具！", file=sys.stderr)
    print(f"可用插件: {', '.join(available_plugins.keys())}", file=sys.stderr)
    print(f"可用工具: {', '.join(['Tools/' + t for t in available_tools.keys()])}", file=sys.stderr)
    sys.exit(1)


if __name__ == "__main__":
    main()
