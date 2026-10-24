#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
自动扫描 Intermediate 目录下 UBT 编译生成的最新的 *.cpp.o.rsp 文件，
并生成/更新 .vscode/compileCommands_<ProjectName>.json 和 compileCommands_Default.json。

配置驱动：复用 tool_config 统一定位与自适应推导机制（约定优于配置，自动向上寻根 .uproject）。
"""

import argparse
import glob
import json
import os
import shutil
import sys

TOOLS_SCRIPT_DIR = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Script")
)
if TOOLS_SCRIPT_DIR not in sys.path:
    sys.path.insert(0, TOOLS_SCRIPT_DIR)

import tool_config  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="同步 VSCode / Clangd 编译数据库 (compile_commands)")
    parser.add_argument("--config", default="", help="工具配置文件路径 (generator_config.json)")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    cfg = tool_config.load_config(args.config, tool_config.KEYS_BUILD)

    project_root = cfg["project_root"]
    project_name = cfg["project_name"]
    engine_source_dir = cfg["engine_source_dir"]
    clang_compiler = cfg.get("clang_compiler")

    if not clang_compiler:
        print("[错误] 未找到 clang++ 编译器：请通过环境变量 UE_CLANG_COMPILER 指定其路径！", file=sys.stderr)
        return 1

    print(f"[Info] 项目根目录: {project_root} (工程名: {project_name})")
    print(f"[Info] 引擎源码目录: {engine_source_dir}")
    print(f"[Info] Clang++ 编译器: {clang_compiler}")

    rsp_pattern = os.path.join(project_root, "**/Intermediate/**/*.cpp.o.rsp")
    rsp_files = glob.glob(rsp_pattern, recursive=True)

    file_to_rsp = {}

    for rsp in sorted(rsp_files):
        is_editor = "UnrealEditor" in rsp
        try:
            with open(rsp, "r", encoding="utf-8", errors="ignore") as f:
                for line in f:
                    stripped = line.strip().strip("\"")
                    if stripped.endswith(".cpp") and os.path.isabs(stripped):
                        if not stripped.startswith(project_root):
                            for marker in ("/Plugins/", "/Source/"):
                                idx = stripped.find(marker)
                                if idx != -1:
                                    local_candidate = os.path.join(project_root, stripped[idx + 1:])
                                    if os.path.isfile(local_candidate):
                                        stripped = local_candidate
                                        break
                        if stripped not in file_to_rsp or is_editor:
                            file_to_rsp[stripped] = rsp
                        break
        except OSError as e:
            print(f"[Warning] 读取失败 {rsp}: {e}", file=sys.stderr)

    entries = []
    for cpp_file, rsp in sorted(file_to_rsp.items()):
        entries.append({
            "file": cpp_file,
            "arguments": [
                clang_compiler,
                f"@{rsp}"
            ],
            "directory": engine_source_dir
        })

    print(f"[Info] 扫描到有效编译单元 (Translation Units): {len(entries)} 个")

    if not entries:
        print("[Warning] 未扫描到任何有效的 .cpp 编译单元。请确认项目是否已在 Xcode / VS / UBT 中完成首次编译！", file=sys.stderr)

    vscode_dir = os.path.join(project_root, ".vscode")
    os.makedirs(vscode_dir, exist_ok=True)

    primary_target_name = f"compileCommands_{project_name}.json"
    targets = [
        os.path.join(vscode_dir, primary_target_name),
        os.path.join(vscode_dir, "compileCommands_Default.json")
    ]

    for target_path in targets:
        with open(target_path, "w", encoding="utf-8") as f:
            json.dump(entries, f, indent=2, ensure_ascii=False)
        print(f"[Success] 已更新: {target_path}")

    symlink_path = os.path.join(vscode_dir, "compile_commands.json")
    if os.path.islink(symlink_path) or os.path.exists(symlink_path):
        try:
            os.remove(symlink_path)
        except OSError:
            pass

    try:
        os.symlink(primary_target_name, symlink_path)
        print(f"[Success] 创建软链接: {symlink_path} -> {primary_target_name}")
    except OSError:
        shutil.copyfile(os.path.join(vscode_dir, primary_target_name), symlink_path)
        print(f"[Success] 复制配置文件: {symlink_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
