#!/usr/bin/env bash
# =============================================================================
# 数据表全流程管线编排入口 (generate_all_tables.sh) —— 跨平台统一调度薄封装
#
# 本脚本为薄封装：统一调度逻辑收敛在 Script/run_pipeline.py（跨平台同语义），
# 与 generate_all_tables.bat 保持 100% 一致；不内置任何工程路径与默认配置。
#
# 使用方法:
#   ./generate_all_tables.sh [--config <path>] [模式] [额外参数...]
#   模式: --all (默认) | --validate | --struct | --json | --tags | --bin | --reverse | --rollback | --roundtrip
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

if ! "$PYTHON_BIN" -c "import openpyxl" &>/dev/null; then
    echo -e "${RED}[错误] 检测到当前 Python 环境缺少必需依赖 openpyxl！${NC}" >&2
    echo -e "${YELLOW}请先在终端执行: pip3 install -r \"${TOOLS_DIR}/requirements.txt\"${NC}" >&2
    exit 1
fi

exec "$PYTHON_BIN" "$PIPELINE_PY" "$@"