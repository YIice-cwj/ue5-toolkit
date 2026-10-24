#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CI 双通道 diff 脚本 (channel_diff.py)

- 同一打包产物分别强制走 .bin / JSON 通道冷启动，各取一次「表数 / 行数 / 内容摘要」
- 三维度逐一比对，任一不一致即退出码非零（可直接进 CI 门）
- 内容摘要由生成式读写器对全量行负载排序后计算（与装载顺序无关，见 FStaticDataRepository::ComputeContentDigest）
- 运行 / 日志解析 / 正则与通道标签统一取自 packaged_run，本脚本不再自带重复实现

用法:
  python3 channel_diff.py [--config <path>]
"""

import argparse
import sys

import packaged_run


def run_diff(config_path: str) -> int:
    """
    执行双通道 diff 对拍
    @param config_path 配置文件路径
    @return 0 表示通过，1 表示失败
    """
    target = packaged_run.load_paths(config_path)
    print(f"打包产物: {target['app_path']}")

    results = {}
    for channel in ("bin", "json"):
        sample = packaged_run.collect_channel_sample(target, channel)
        expected_label = packaged_run.CHANNEL_LABELS[channel]
        if sample["source"] != expected_label:
            print(f"[失败] 通道开关未生效：期望 {expected_label}，实际 {sample['source']}", file=sys.stderr)
            return 1
        results[channel] = sample
        print(f"[{channel}] 表数={sample['tables']} 行数={sample['rows']} 摘要={sample['digest']}")

    bin_result = results["bin"]
    json_result = results["json"]
    checks = (
        ("表数一致", bin_result["tables"] == json_result["tables"]),
        ("行数一致", bin_result["rows"] == json_result["rows"]),
        ("内容摘要一致", bin_result["digest"] == json_result["digest"]),
    )
    failed = [name for name, passed in checks if not passed]
    for name, passed in checks:
        print(f"[{'通过' if passed else '失败'}] {name}")

    if failed:
        print(f"双通道 diff 未通过：{', '.join(failed)}", file=sys.stderr)
        return 1

    print(f"双通道 diff 通过：{bin_result['tables']} 张表 / {bin_result['rows']} 行 / 摘要 {bin_result['digest']}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="CI 双通道数据一致性对拍")
    parser.add_argument("--config", default="", help="工具配置文件路径（缺省走共享配置发现）")
    args = parser.parse_args()

    config_path = packaged_run.resolve_config_path(args.config)
    if config_path is None:
        print("[错误] 未找到工具配置文件，请通过 --config 或环境变量 UE_TOOL_CONFIG 指定。", file=sys.stderr)
        return 1
    return run_diff(config_path)


if __name__ == "__main__":
    sys.exit(main())
