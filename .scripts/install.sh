#!/usr/bin/env bash
# =============================================================================
# UEToolkit 智能双模一键安装引导器 (.scripts/install.sh)
#
# 模式一（本地仓库直连）：
#   当在已克隆的 UEToolkit 仓库内执行时，直接转发至同目录 install.py（支持 link / copy）。
#   示例：./.scripts/install.sh --all /path/to/MyProject
#
# 模式二（GitHub 远程无痕安装，支持 curl / wget 管道执行）：
#   当通过 curl / wget 管道执行且本地无仓库时，自动拉取 GitHub 源码压缩包至系统临时目录，
#   强制以物理拷贝模式 (--mode copy) 注入目标 UE5 工程，退出时自动销毁临时目录（零残留）。
#   示例：
#     curl -fsSL https://raw.githubusercontent.com/yiice/UEToolkit/main/.scripts/install.sh | bash -s -- --all .
#     wget -qO- https://raw.githubusercontent.com/yiice/UEToolkit/main/.scripts/install.sh | bash -s -- GameGameplay /path/to/MyProject
#
# 环境变量覆盖：
#   UETOOLKIT_REPO         GitHub 仓库路径（默认: yiice/UEToolkit）
#   UETOOLKIT_REF          分支、Tag 或 Commit（默认: main）
#   UETOOLKIT_ARCHIVE_URL  自定义源码包地址或本地 .tar.gz 路径
# =============================================================================
set -euo pipefail

detect_python() {
    if command -v python3 >/dev/null 2>&1; then
        echo "python3"
        return 0
    fi
    if command -v python >/dev/null 2>&1 && python -c 'import sys; raise SystemExit(0 if sys.version_info.major >= 3 else 1)' 2>/dev/null; then
        echo "python"
        return 0
    fi
    echo "[错误] 未找到 Python 3 环境，请先安装 python3。" >&2
    exit 1
}

PYTHON_BIN="$(detect_python)"

SCRIPT_SOURCE="${BASH_SOURCE[0]:-}"
if [[ -n "${SCRIPT_SOURCE}" && -f "${SCRIPT_SOURCE}" && -z "${UETOOLKIT_FORCE_REMOTE:-}" ]]; then
    SCRIPT_DIR="$(cd "$(dirname "${SCRIPT_SOURCE}")" && pwd)"
    REPO_CANDIDATE="$(cd "${SCRIPT_DIR}/.." && pwd)"
    if [[ -f "${SCRIPT_DIR}/install.py" && -d "${REPO_CANDIDATE}/Plugins" ]]; then
        exec "${PYTHON_BIN}" "${SCRIPT_DIR}/install.py" "$@"
    fi
fi

UETOOLKIT_REPO="${UETOOLKIT_REPO:-yiice/UEToolkit}"
UETOOLKIT_REF="${UETOOLKIT_REF:-main}"
UETOOLKIT_ARCHIVE_URL="${UETOOLKIT_ARCHIVE_URL:-https://codeload.github.com/${UETOOLKIT_REPO}/tar.gz/${UETOOLKIT_REF}}"

TMP_DIR="$(mktemp -d 2>/dev/null || mktemp -d -t 'uetoolkit')"
cleanup() {
    if [[ -n "${TMP_DIR:-}" && -d "${TMP_DIR}" ]]; then
        rm -rf "${TMP_DIR}"
    fi
}
trap cleanup EXIT INT TERM

echo "[+] 正在从远程获取 UEToolkit (${UETOOLKIT_REPO}@${UETOOLKIT_REF}) ..."
if [[ -f "${UETOOLKIT_ARCHIVE_URL}" ]]; then
    tar -xzf "${UETOOLKIT_ARCHIVE_URL}" -C "${TMP_DIR}"
elif command -v curl >/dev/null 2>&1; then
    curl -fsSL "${UETOOLKIT_ARCHIVE_URL}" | tar -xzf - -C "${TMP_DIR}"
elif command -v wget >/dev/null 2>&1; then
    wget -qO- "${UETOOLKIT_ARCHIVE_URL}" | tar -xzf - -C "${TMP_DIR}"
else
    echo "[错误] 远程安装需要 curl 或 wget，请先安装其中之一。" >&2
    exit 1
fi

EXTRACTED_INSTALL_PY="$(find "${TMP_DIR}" -maxdepth 3 -path "*/.scripts/install.py" | head -n 1)"
if [[ -z "${EXTRACTED_INSTALL_PY}" || ! -f "${EXTRACTED_INSTALL_PY}" ]]; then
    echo "[错误] 源码包解压异常，未找到 .scripts/install.py。" >&2
    exit 1
fi

FORWARD_ARGS=()
HAS_MODE=0
IS_QUERY_OR_UNINSTALL=0
SKIP_NEXT=0
for arg in "$@"; do
    if [[ "${SKIP_NEXT}" -eq 1 ]]; then
        SKIP_NEXT=0
        if [[ "${arg}" == "link" ]]; then
            echo "[-] 远程无痕安装不支持软链接（临时目录退出即销毁），已自动切换为 --mode copy。"
        fi
        FORWARD_ARGS+=("copy")
        continue
    fi
    case "${arg}" in
        --mode)
            HAS_MODE=1
            SKIP_NEXT=1
            FORWARD_ARGS+=("--mode")
            ;;
        --mode=link)
            HAS_MODE=1
            echo "[-] 远程无痕安装不支持软链接（临时目录退出即销毁），已自动切换为 --mode copy。"
            FORWARD_ARGS+=("--mode" "copy")
            ;;
        --mode=*)
            HAS_MODE=1
            FORWARD_ARGS+=("${arg}")
            ;;
        --list|--uninstall|-h|--help)
            IS_QUERY_OR_UNINSTALL=1
            FORWARD_ARGS+=("${arg}")
            ;;
        *)
            FORWARD_ARGS+=("${arg}")
            ;;
    esac
done

if [[ "${HAS_MODE}" -eq 0 && "${IS_QUERY_OR_UNINSTALL}" -eq 0 && "${#FORWARD_ARGS[@]}" -gt 0 ]]; then
    FORWARD_ARGS+=("--mode" "copy")
fi

"${PYTHON_BIN}" "${EXTRACTED_INSTALL_PY}" ${FORWARD_ARGS[@]+"${FORWARD_ARGS[@]}"}
