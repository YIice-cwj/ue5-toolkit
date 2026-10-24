#!/usr/bin/env bash
# =============================================================================
# Cook 打包产物验证与冷启动基准调度入口 (run_cook_tools.sh)
#
# 功能说明:
# 1. 调用 Script/channel_diff.py          CI 双通道一致性校验（.bin vs JSON）
# 2. 调用 Script/cold_start_benchmark.py  无头冷启动反序列化耗时基准采集
# 3. 默认调度 Script/run_pipeline.py 执行全流程对拍与性能评测
#
# 使用方式:
#   ./run_cook_tools.sh [--config <path>] [--all | --diff | --benchmark] [--runs N] [--channel bin|json|both] [--out <report.md>]
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# 复用 Tools 共享 Shell 库（detect_python 等工具函数）
. "${TOOLS_DIR}/common.sh"

PIPELINE_PY="${SCRIPT_DIR}/Script/run_pipeline.py"

PYTHON_BIN="$(detect_python)"

if [ -z "$PYTHON_BIN" ]; then
    echo -e "${RED}[错误] 未检测到 Python 3 解释器，请先安装 Python 3 或将其添加至环境变量 PATH。${NC}" >&2
    exit 1
fi

echo -e "${BLUE}======================================================${NC}"
echo -e "${BLUE}      Cook 打包验证与性能评测工具链      ${NC}"
echo -e "${BLUE}======================================================${NC}"

# 转发全部参数至统一调度器
"$PYTHON_BIN" "$PIPELINE_PY" "$@"

echo -e "${GREEN}======================================================${NC}"
echo -e "${GREEN}[成功] Cook 验证与评测任务执行完毕！${NC}"
echo -e "${GREEN}======================================================${NC}"

