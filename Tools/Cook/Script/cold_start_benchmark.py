#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Cold-Start 基准脚本 (cold_start_benchmark.py)

- 对同一打包产物逐通道（.bin / JSON）重复无头冷启动，采集「反序列化阶段」耗时
- 逐通道输出样本数 / 最小 / 中位 / 均值 / 最大，并把对比数据写入验收报告（Markdown）
- 不内置任何工程路径：打包产物路径来自工具配置，通道开关经命令行注入

用法:
  python3 cold_start_benchmark.py [--config <path>] [--runs N] [--channel bin|json|both] [--out <report.md>]
"""

import argparse
import os
import statistics
import sys
from datetime import datetime
from typing import Any

import packaged_run

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_REPORT = os.path.normpath(
    os.path.join(SCRIPT_DIR, "..", "..", "..", "Docs", "Benchmark", "ColdStart_Benchmark_Report.md"))


def summarize(samples: list[float]) -> dict[str, Any]:
    """
    汇总单通道样本
    @param samples 耗时样本列表（毫秒）
    @return 字典：count / min / median / mean / max
    """
    return {
        "count": len(samples),
        "min": min(samples),
        "median": statistics.median(samples),
        "mean": statistics.fmean(samples),
        "max": max(samples),
    }


def write_report(report_path: str, results: dict[str, list[dict[str, Any]]], runs: int, channel_arg: str) -> None:
    """
    输出基准报告（Markdown）
    @param report_path 报告输出路径
    @param results 每个通道的采集结果列表
    @param runs 每通道运行次数
    @param channel_arg 冷启动通道开关名（随配置提供，用于报告口径说明）
    """
    lines = [
        "# Cold-Start 基准报告",
        "",
        f"> **生成时间**：{datetime.now().strftime('%Y-%m-%d %H:%M:%S')} ｜ **每通道样本数**：{runs}",
        "> **口径**：指标为冷启动**反序列化阶段**耗时（读源 + 建容器），",
        f"> 由生成物 `【静态数据装载耗时】` 日志采集；同一打包产物逐通道无头运行，仅注入 `-{channel_arg}`。",
        "",
        "| 通道 | 样本 | 最小 (ms) | 中位 (ms) | 均值 (ms) | 最大 (ms) | 表数 | 行数 | 内容摘要 |",
        "| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- |",
    ]
    for channel, samples in results.items():
        if not samples:
            continue
        stats = summarize([item["milliseconds"] for item in samples])
        first = samples[0]
        lines.append(
            f"| {channel} | {stats['count']} | {stats['min']:.3f} | {stats['median']:.3f} | "
            f"{stats['mean']:.3f} | {stats['max']:.3f} | {first['tables']} | {first['rows']} | `{first['digest'][:16]}…` |")

    lines.extend(["", "## 原始样本（毫秒）", ""])
    for channel, samples in results.items():
        values = ", ".join(f"{item['milliseconds']:.3f}" for item in samples)
        lines.append(f"- **{channel}**：{values}")

    lines.extend([
        "",
        "## 结论口径",
        "",
        "- 两通道内容摘要必须一致（由 `channel_diff.py` 做 100% 对拍）；本报告只负责耗时对比。",
        "- 档 2「内存镜像档」的切换以本表数据为依据，未测量前不做承诺。",
        "",
    ])

    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))


def run_benchmark(config_path: str, runs: int = 3, channel: str = "both", out_report: str = DEFAULT_REPORT) -> int:
    """
    执行冷启动反序列化基准测试
    @param config_path 配置文件路径
    @param runs 每通道运行次数
    @param channel 待测通道 ("bin" / "json" / "both")
    @param out_report 报告输出路径
    @return 0 表示成功
    """
    target = packaged_run.load_paths(config_path)
    channels = ("bin", "json") if channel == "both" else (channel,)

    print(f"打包产物: {target['app_path']}")
    print(f"每通道运行 {runs} 次，通道: {', '.join(channels)}")
    results = {}
    for ch in channels:
        samples = []
        for index in range(runs):
            sample = packaged_run.collect_channel_sample(target, ch)
            expected_label = packaged_run.CHANNEL_LABELS[ch]
            if sample["source"] != expected_label:
                raise RuntimeError(f"通道开关未生效：期望 {expected_label}，实际 {sample['source']}")
            if sample["milliseconds"] is None:
                raise RuntimeError(f"通道 {ch} 缺少「静态数据装载耗时」日志，无法采集耗时")
            samples.append(sample)
            print(f"  [{ch}] #{index + 1}: {sample['milliseconds']:.3f} ms（表数 {sample['tables']} 行数 {sample['rows']}）")
        results[ch] = samples

    for ch, samples in results.items():
        stats = summarize([item["milliseconds"] for item in samples])
        print(f"[{ch}] 最小 {stats['min']:.3f} / 中位 {stats['median']:.3f} / 均值 {stats['mean']:.3f} / 最大 {stats['max']:.3f} ms")

    write_report(out_report, results, runs, target["channel_arg"])
    print(f"基准报告已写入: {out_report}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Cold-Start 基准（.bin vs JSON）")
    parser.add_argument("--config", default="", help="工具配置文件路径（缺省走共享配置发现）")
    parser.add_argument("--runs", type=int, default=3, help="每通道运行次数（默认 3）")
    parser.add_argument("--channel", choices=("bin", "json", "both"), default="both", help="待测通道")
    parser.add_argument("--out", default=DEFAULT_REPORT, help="基准报告输出路径（Markdown）")
    args = parser.parse_args()

    config_path = packaged_run.resolve_config_path(args.config)
    if config_path is None:
        print("[错误] 未找到工具配置文件，请通过 --config 或环境变量 UE_TOOL_CONFIG 指定。", file=sys.stderr)
        return 1
    return run_benchmark(config_path, args.runs, args.channel, args.out)


if __name__ == "__main__":
    sys.exit(main())

