#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Cook 工具链统一调度器 (run_pipeline.py)

- 统一调度打包产物的无头自动化测试：
  - --diff / --verify-channels: CI 双通道一致性校验 (channel_diff.py)
  - --benchmark / --cold-start: 冷启动反序列化基准测试 (cold_start_benchmark.py)
  - --all: 默认模式，先执行一致性对拍，通过后再执行性能基准测试
- 配置驱动：--config 指定或经共享配置发现自动定位 generator_config.json

用法:
  python3 run_pipeline.py [--config <path>] [--all | --diff | --benchmark] [--runs N] [--channel bin|json|both] [--out <report.md>]
"""

import argparse
import sys

import channel_diff
import cold_start_benchmark
import packaged_run


def main() -> int:
    parser = argparse.ArgumentParser(description="Cook 打包验证与冷启动性能管线调度器")
    parser.add_argument("--config", default="", help="工具配置文件路径 (generator_config.json)")

    mode_group = parser.add_mutually_exclusive_group()
    mode_group.add_argument("--all", action="store_true", help="执行全套流水线（diff 对拍 + 性能基准，默认）")
    mode_group.add_argument("--diff", "--verify-channels", dest="diff_only", action="store_true", help="仅执行双通道数据一致性校验")
    mode_group.add_argument("--benchmark", "--cold-start", dest="benchmark_only", action="store_true", help="仅执行冷启动性能基准测试")

    parser.add_argument("--runs", type=int, default=3, help="基准测试每通道运行次数（默认 3）")
    parser.add_argument("--channel", choices=("bin", "json", "both"), default="both", help="基准测试待测通道")
    parser.add_argument("--out", default=cold_start_benchmark.DEFAULT_REPORT, help="基准测试报告输出路径（Markdown）")

    args = parser.parse_args()
    config_path = packaged_run.resolve_config_path(args.config)
    if config_path is None:
        print("[错误] 未找到有效的配置文件 generator_config.json。"
              "请通过 --config 或环境变量 UE_TOOL_CONFIG 指定。", file=sys.stderr)
        sys.exit(1)
    print(f"[Cook 管线] 使用配置文件: {config_path}")

    b_run_diff = args.diff_only or args.all or (not args.benchmark_only)
    b_run_benchmark = args.benchmark_only or args.all or (not args.diff_only)

    if b_run_diff:
        print("\n========================================================")
        print("  步骤 1/2: 正在执行 CI 双通道数据一致性校验 (Diff)...")
        print("========================================================")
        diff_code = channel_diff.run_diff(config_path)
        if diff_code != 0:
            print("[Cook 管线] 双通道一致性校验失败，中断后续测试！", file=sys.stderr)
            return diff_code

    if b_run_benchmark:
        print("\n========================================================")
        print("  步骤 2/2: 正在执行冷启动反序列化基准测试 (Benchmark)...")
        print("========================================================")
        bench_code = cold_start_benchmark.run_benchmark(config_path, args.runs, args.channel, args.out)
        if bench_code != 0:
            print("[Cook 管线] 基准测试执行异常！", file=sys.stderr)
            return bench_code

    print("\n[Cook 管线] 全部测试任务执行完成！")
    return 0


if __name__ == "__main__":
    sys.exit(main())

