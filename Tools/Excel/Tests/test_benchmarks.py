#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Tools/Excel Python 工具链性能微基准测试 (test_benchmarks.py)

采用 2 轮预热 + 5 轮正式采样的多轮中位数与变异系数 (CV%) 统计口径，
覆盖 1000 行真实规模表格的解析、校验、正向 JSON 导出与反向回写吞吐预算。
"""

import argparse
import math
import os
import statistics
import tempfile
import time
import unittest
from typing import Callable, Dict

from test_helpers import create_xlsx
import excel_parser
import excel_to_json
import json_to_excel
import validate_tables


def _run_benchmark(warmup: int, samples: int, fn: Callable[[], None]) -> Dict[str, float]:
    """
    执行多轮微基准并返回中位数、均值与 CV%（毫秒口径）
    @param warmup 预热轮数
    @param samples 正式采样轮数
    @param fn 单轮待测操作
    @return 统计指标字典
    """
    for _ in range(warmup):
        fn()
    timings_ms = []
    for _ in range(samples):
        t0 = time.perf_counter()
        fn()
        timings_ms.append((time.perf_counter() - t0) * 1000.0)
    median_ms = statistics.median(timings_ms)
    mean_ms = statistics.fmean(timings_ms)
    variance = sum((x - mean_ms) ** 2 for x in timings_ms) / len(timings_ms)
    cv_pct = (math.sqrt(variance) / mean_ms * 100.0) if mean_ms > 0.0 else 0.0
    return {"median_ms": median_ms, "mean_ms": mean_ms, "cv_pct": cv_pct}


class TestExcelToolchainBenchmarks(unittest.TestCase):
    """1000 行数据表解析、校验、导出与回写微基准（防性能退化门禁）"""

    def test_1000_row_parse_validate_export_and_reverse_sync(self) -> None:
        """测量 1000 行标准表全链路吞吐并验证处于合理预算内"""
        with tempfile.TemporaryDirectory() as tmp:
            dt_dir = os.path.join(tmp, "DT")
            json_dir = os.path.join(tmp, "Json")
            rows = [
                [f"Item_{i:04d}", f"物品_{i}", i * 10, round(i * 0.25, 2), (i % 2 == 0), f'["Tag.A", "Tag.B_{i % 5}"]']
                for i in range(1000)
            ]
            xlsx_path = create_xlsx(
                os.path.join(dt_dir, "Item", "DT_BenchItem.xlsx"),
                "DT_BenchItem",
                (
                    ["主键", "名称", "价格", "权重", "启用", "标签"],
                    ["RowName", "title", "price", "weight", "is_enabled", "tags"],
                    ["FName", "FText", "uint32", "float", "bool", "TArray<FGameplayTag>"],
                ),
                rows=rows,
            )

            parse_stats = _run_benchmark(2, 5, lambda: excel_parser.read_sheet_grid(xlsx_path))
            val_stats = _run_benchmark(2, 5, lambda: validate_tables.validate_directory(dt_dir))
            exp_stats = _run_benchmark(2, 5, lambda: excel_to_json.export_all(dt_dir, json_dir))

            json_path = os.path.join(json_dir, "Item", "DT_BenchItem.json")
            opts = argparse.Namespace(
                dry_run=True, table=None, allow_delete=False,
                force_formula=False, fill_defaults=False,
                float_epsilon=1e-6, no_backup=True, strict=True,
                protect_formulas_config=True,
            )
            rev_stats = _run_benchmark(
                2, 5,
                lambda: json_to_excel.perform_sync_for_table(xlsx_path, json_path, opts, backup_session=None),
            )

            print(
                f"\n[Benchmark 1000 rows] "
                f"Parse={parse_stats['median_ms']:.2f}ms (CV={parse_stats['cv_pct']:.1f}%) | "
                f"Validate={val_stats['median_ms']:.2f}ms (CV={val_stats['cv_pct']:.1f}%) | "
                f"ExportJSON={exp_stats['median_ms']:.2f}ms (CV={exp_stats['cv_pct']:.1f}%) | "
                f"ReverseSync={rev_stats['median_ms']:.2f}ms (CV={rev_stats['cv_pct']:.1f}%)"
            )

            self.assertLess(parse_stats["median_ms"], 500.0)
            self.assertLess(val_stats["median_ms"], 1000.0)
            self.assertLess(exp_stats["median_ms"], 1000.0)
            self.assertLess(rev_stats["median_ms"], 3000.0)


if __name__ == "__main__":
    unittest.main()
