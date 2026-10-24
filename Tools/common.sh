#!/usr/bin/env bash
# =============================================================================
# Tools 通用 Shell 工具库 (common.sh)
#
# 供 Tools/ 下各入口脚本 source 复用，收敛跨脚本重复实现的通用工具函数与常量。
# 使用方式（在调用方脚本中）:
#   SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
#   . "$(cd "${SCRIPT_DIR}/.." && pwd)/common.sh"
# =============================================================================

# 终端 ANSI 颜色常量（各入口脚本统一引用，不再各自重复定义）
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
RED='\033[0;31m'
NC='\033[0m'

# 配置发现的环境变量（按优先级；与 Tools/Excel/Script/tool_config.py 的 CONFIG_ENV_KEYS 保持一致）
CONFIG_ENV_KEYS="UE_TOOL_CONFIG UE_STATIC_DATA_CONFIG"

# 按优先级解析工具配置文件路径：$1 显式参数 > 环境变量 > 当前目录 > $2 兜底目录；全部未命中输出空串
resolve_config_path() {
    if [ -n "$1" ]; then
        echo "$1"
        return 0
    fi
    for key in $CONFIG_ENV_KEYS; do
        value="$(printenv "$key" 2>/dev/null || true)"
        if [ -n "$value" ]; then
            echo "$value"
            return 0
        fi
    done
    for dir in "." "$2"; do
        if [ -f "${dir}/generator_config.json" ]; then
            echo "${dir}/generator_config.json"
            return 0
        fi
    done
    echo ""
}

# 探测可用的 Python 3 解释器命令名（优先 python3，其次 python 且须为 3.x）；未找到时输出空串
detect_python() {
    if command -v python3 &>/dev/null; then
        echo "python3"
    elif command -v python &>/dev/null; then
        if python -c 'import sys; exit(0 if sys.version_info.major >= 3 else 1)' 2>/dev/null; then
            echo "python"
        else
            echo ""
        fi
    else
        echo ""
    fi
}
