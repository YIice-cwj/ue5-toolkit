#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
打包产物无头运行助手 (packaged_run.py)

- 以「-nullrhi -unattended -nosplash」无头运行打包后的可执行体，并回收本次运行的引擎日志文本
- 日志定位：macOS 沙盒应用写容器内 Logs（<Container>/Data/Library/Logs/<Project>/<Project>.log），
  非沙盒 / Windows 写 <打包目录>/<Project>/Saved/Logs/<Project>.log；两者均按「运行后 mtime 变化」判定本次日志
- 冷启动通道开关、完整性报告命令与日志解析正则由本模块统一提供，供 channel_diff / cold_start_benchmark 复用
- 运行期取证依赖生成物的启动自检开关（装载完成后输出摘要、写 StaticDataVerify.txt 并自行退出），不依赖启动期 ExecCmds 时序
- 不内置任何工程路径与控制台命令字面量：路径与开关名均来自工具配置（generator_config.json）

用法（作为库被同目录脚本导入）:
  import packaged_run
  target = packaged_run.load_paths(config_path)
  sample = packaged_run.collect_channel_sample(target, "bin")
"""

import json
import os
import re
import subprocess
import sys
import time
from typing import Any, Optional

# 复用 Tools 共享配置解析实现（配置发现只有一处真源）
TOOLS_SCRIPT_DIR = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Script"))
if TOOLS_SCRIPT_DIR not in sys.path:
    sys.path.insert(0, TOOLS_SCRIPT_DIR)

from tool_config import CONVENTIONAL_CONFIG_PATH, derive_console_names, resolve_config_path  # noqa: E402

__all__ = [
    "CONVENTIONAL_CONFIG_PATH",
    "resolve_config_path",
    "CHANNEL_LABELS",
    "VERIFY_REPORT_NAME",
    "load_paths",
    "collect_channel_sample",
    "run_packaged",
    "read_run_log",
    "read_verify_report",
    "log_candidates",
]

# 配置中需要的路径类键（相对配置文件所在目录展开）
CONFIG_PATH_KEYS = ("project_file", "packaged_app_path")

# 配置中承载控制台命令名的段落
CONSOLE_COMMANDS_KEY = "console_commands"

# 常规配置文件位置（取自配置解析模块，各脚本不再各自拼路径）
DEFAULT_CONFIG = CONVENTIONAL_CONFIG_PATH

# 双通道标签与日志解析正则（channel_diff 与 cold_start_benchmark 共用）
CHANNEL_LABELS = {"bin": ".bin", "json": "JSON"}
SOURCE_PATTERN = re.compile(r"本批次数据来源：(?P<channel>\.bin|JSON) 通道")
TIMING_PATTERN = re.compile(r"静态数据装载耗时】(?P<channel>\.bin|JSON) 通道反序列化 (?P<ms>[0-9.]+) ms")
DIGEST_PATTERN = re.compile(r"(?:【静态数据校验】)?表数=(?P<tables>\d+) 行数=(?P<rows>\d+) 内容摘要=(?P<digest>[0-9a-f]+)")

# 启动自检报告文件名与解析正则（Shipping 裁剪日志时的可靠取证通道）
VERIFY_REPORT_NAME = "StaticDataVerify.txt"
VERIFY_REPORT_PATTERN = re.compile(
    r"loaded=(?P<loaded>[01]) tables=(?P<tables>\d+) rows=(?P<rows>\d+) digest=(?P<digest>[0-9a-f]+)")


def load_paths(config_path: str) -> dict[str, str]:
    """
    读取并解析工程文件、打包产物、工程名与配置驱动的命令行开关
    @param config_path 工具配置文件路径
    @return 字典：project_file / app_path / project_name / channel_arg / verify_switch
    @raises RuntimeError 配置缺少 project_file 或 packaged_app_path
    """
    with open(config_path, "r", encoding="utf-8") as handle:
        raw = json.load(handle)

    base_dir = os.path.dirname(os.path.abspath(config_path))
    resolved = {}
    for key in CONFIG_PATH_KEYS:
        value = raw.get(key) or ""
        if value and not os.path.isabs(value):
            value = os.path.normpath(os.path.join(base_dir, value))
        resolved[key] = value

    if not resolved["project_file"] or not resolved["packaged_app_path"]:
        raise RuntimeError(f"配置文件缺少必需项 project_file / packaged_app_path: {config_path}")

    console_commands = raw.get(CONSOLE_COMMANDS_KEY) or {}
    derived = derive_console_names(str(raw.get("project_prefix") or "Game"))
    channel_arg = console_commands.get("channel_cmd_arg") or derived["channel_cmd_arg"]
    verify_switch = console_commands.get("verify_switch") or derived["verify_switch"]

    project_file = resolved["project_file"]
    return {
        "project_file": project_file,
        "app_path": resolved["packaged_app_path"],
        "project_name": os.path.splitext(os.path.basename(project_file))[0],
        "channel_arg": channel_arg,
        "verify_switch": verify_switch,
    }


def log_candidates(target: dict[str, str]) -> list[str]:
    """
    枚举可能的引擎日志路径
    @param target load_paths 返回值
    @return 候选日志路径列表（沙盒容器优先）
    """
    project_name = target["project_name"]
    file_name = f"{project_name}.log"
    candidates = []

    containers_dir = os.path.expanduser(os.path.join("~", "Library", "Containers"))
    if os.path.isdir(containers_dir):
        for entry in os.listdir(containers_dir):
            candidates.append(os.path.join(containers_dir, entry, "Data", "Library", "Logs", project_name, file_name))

    staged_game_dir = os.path.normpath(
        os.path.join(os.path.dirname(target["app_path"]), os.pardir, "UE", project_name))
    candidates.append(os.path.join(staged_game_dir, "Saved", "Logs", file_name))
    return candidates


def read_run_log(target: dict[str, str], started_at: float) -> str:
    """
    读取本次运行写入的引擎日志
    @param target load_paths 返回值
    @param started_at 运行开始时间戳（秒）
    @return 日志文本；未发现新日志返回空串
    """
    newest_path = None
    newest_mtime = 0.0
    for candidate in log_candidates(target):
        if not os.path.isfile(candidate):
            continue
        mtime = os.path.getmtime(candidate)
        if mtime >= started_at - 2.0 and mtime > newest_mtime:
            newest_path = candidate
            newest_mtime = mtime

    if newest_path is None:
        return ""

    with open(newest_path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def report_candidates(target: dict[str, str]) -> list[str]:
    """
    枚举启动自检报告的候选路径
    @param target load_paths 返回值
    @return 候选报告路径列表（打包产物的项目存档目录优先）
    """
    project_name = target["project_name"]
    candidates = [
        os.path.expanduser(os.path.join(
            "~", "Library", "Application Support", "Epic", project_name, "Saved", VERIFY_REPORT_NAME)),
    ]

    staged_game_dir = os.path.normpath(
        os.path.join(os.path.dirname(target["app_path"]), os.pardir, "UE", project_name))
    candidates.append(os.path.join(staged_game_dir, "Saved", VERIFY_REPORT_NAME))

    containers_dir = os.path.expanduser(os.path.join("~", "Library", "Containers"))
    if os.path.isdir(containers_dir):
        for entry in os.listdir(containers_dir):
            candidates.append(os.path.join(
                containers_dir, entry, "Data", "Library", "Application Support", "Epic",
                project_name, "Saved", VERIFY_REPORT_NAME))
    return candidates


def read_verify_report(target: dict[str, str], started_at: float) -> Optional[dict[str, Any]]:
    """
    读取本次运行产出的启动自检报告
    @param target load_paths 返回值
    @param started_at 运行开始时间戳（秒）
    @return 字典：path / loaded / tables / rows / digest；报告缺失或格式不符返回 None
    """
    newest_path = None
    newest_mtime = 0.0
    for candidate in report_candidates(target):
        if not os.path.isfile(candidate):
            continue
        mtime = os.path.getmtime(candidate)
        if mtime >= started_at - 2.0 and mtime > newest_mtime:
            newest_path = candidate
            newest_mtime = mtime

    if newest_path is None:
        return None

    with open(newest_path, "r", encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    match = VERIFY_REPORT_PATTERN.search(text)
    if match is None:
        return None
    return {
        "path": newest_path,
        "loaded": match.group("loaded") == "1",
        "tables": int(match.group("tables")),
        "rows": int(match.group("rows")),
        "digest": match.group("digest"),
    }


def run_packaged(target: dict[str, str], channel: Optional[str] = None,
                 timeout_seconds: int = 600, retries: int = 1) -> tuple[bool, str]:
    """
    无头运行打包产物并返回本次运行的引擎日志文本
    @param target load_paths 返回值
    @param channel 冷启动通道（"bin" / "json"；None 表示不施加开关，走默认 .bin 优先）
    @param timeout_seconds 单次运行超时秒数
    @param retries 启动失败（非零退出或无新日志，例如刚重打包的包体首启）的重试次数
    @return (是否正常退出, 引擎日志文本)

    运行方式：注入启动自检开关（生成物在冷启动装载完成后输出内容摘要并自行退出），
    不使用启动期 ExecCmds 调度业务命令，避免子系统尚未就绪的时序问题。
    """
    app_path = target["app_path"]
    if not os.path.isfile(app_path):
        raise FileNotFoundError(f"打包产物不存在: {app_path}")

    cmd = [app_path, "-nullrhi", "-unattended", "-nosplash", f"-{target['verify_switch']}"]
    if channel:
        cmd.append(f"-{target['channel_arg']}={channel}")

    exit_ok = False
    log_text = ""
    for _ in range(retries + 1):
        started_at = time.time()
        try:
            with open(os.devnull, "wb") as dev_null:
                result = subprocess.run(cmd, stdout=dev_null, stderr=dev_null, timeout=timeout_seconds)
            exit_ok = result.returncode == 0
        except subprocess.TimeoutExpired:
            print(f"[!] 打包产物运行超时（超过 {timeout_seconds} 秒）: {app_path}", file=sys.stderr)
            exit_ok = False
        log_text = read_run_log(target, started_at)
        if exit_ok and log_text:
            break

    return exit_ok, log_text


def collect_channel_sample(target: dict[str, str], channel: str) -> dict[str, Any]:
    """
    无头运行一次并采集该次冷启动的通道 / 耗时 / 表数 / 行数 / 内容摘要
    @param target load_paths 返回值
    @param channel 强制通道（"bin" / "json"）
    @return 字典：source / milliseconds（无耗时日志时为 None）/ tables / rows / digest
    @raises RuntimeError 退出异常或关键日志缺失
    """
    exit_ok, log_text = run_packaged(target, channel)
    source_match = SOURCE_PATTERN.search(log_text)
    digest_match = DIGEST_PATTERN.search(log_text)
    if not exit_ok or source_match is None or digest_match is None:
        raise RuntimeError(f"通道 {channel} 采集失败（退出正常={exit_ok}）；日志尾部:\n{log_text[-800:]}")

    timing_match = TIMING_PATTERN.search(log_text)
    return {
        "source": source_match.group("channel"),
        "milliseconds": float(timing_match.group("ms")) if timing_match else None,
        "tables": int(digest_match.group("tables")),
        "rows": int(digest_match.group("rows")),
        "digest": digest_match.group("digest"),
    }
