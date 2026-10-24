#!/usr/bin/env bash
set -euo pipefail

# 获取脚本自身所在的绝对目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PYTHON_SCRIPT="${SCRIPT_DIR}/Script/update_compile_commands.py"

# 复用 Tools 共享 Shell 库（颜色常量等）
. "${TOOLS_DIR}/common.sh"

echo -e "${BLUE}======================================================${NC}"
echo -e "${BLUE}  正在同步 VSCode / Clangd 编译数据库 (compile_commands)  ${NC}"
echo -e "${BLUE}======================================================${NC}"

# 复用共享 Shell 库探测 Python 3 解释器
PYTHON_BIN="$(detect_python)"

if [ -z "$PYTHON_BIN" ]; then
    echo -e "${RED}[Error] 未检测到 Python 3 解释器，请先安装或配置 Python3 环境！${NC}"
    exit 1
fi

# 执行 Python 解析脚本
"$PYTHON_BIN" "${PYTHON_SCRIPT}" "$@"

echo -e "${GREEN}======================================================${NC}"
echo -e "${GREEN}[Success] 编译数据库同步完成！${NC}"
echo -e "${GREEN}提示：如果在 VSCode 中未立即生效，请按 Cmd+Shift+P 执行：${NC}"
echo -e "${GREEN}      'clangd: Restart language server' 重启语言服务。${NC}"
echo -e "${GREEN}======================================================${NC}"
