#!/usr/bin/env bash
#
# P7.5 本地 CI：清理进程 → 编译编辑器 → 五前缀自动化测试 → 网络链路冒烟 → 日志归档
#
# 用法：
#   ./.scripts/ci_local.sh               全流程
#   ./.scripts/ci_local.sh --skip-build  复用已有二进制，跳过编译
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$(basename "${SCRIPT_DIR}")" == ".scripts" ]]; then
    ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
else
    ROOT_DIR="${SCRIPT_DIR}"
fi
UPROJECT="${UPROJECT:-${ROOT_DIR}/UEToolkit.uproject}"

resolve_ue_root() {
    if [[ -n "${UE_ROOT:-}" ]]; then
        echo "${UE_ROOT}"
        return
    fi
    if [[ -n "${UE_ENGINE_DIR:-}" ]]; then
        echo "${UE_ENGINE_DIR}"
        return
    fi
    local assoc=""
    if [[ -f "${UPROJECT}" ]]; then
        assoc="$(python3 -c 'import json,sys; print(str(json.load(open(sys.argv[1])).get("EngineAssociation","")).strip())' "${UPROJECT}" 2>/dev/null || true)"
    fi
    local candidates=()
    if [[ -n "${assoc}" ]]; then
        candidates+=("/Users/Shared/Epic Games/UE_${assoc}" "/Applications/Epic Games/UE_${assoc}")
    fi
    local minor
    for minor in 8 7 6 5 4 3 2 1; do
        candidates+=("/Users/Shared/Epic Games/UE_5.${minor}" "/Applications/Epic Games/UE_5.${minor}")
    done
    local cand
    for cand in "${candidates[@]}"; do
        if [[ -f "${cand}/Engine/Build/Build.version" ]]; then
            echo "${cand}"
            return
        fi
    done
    echo "/Users/Shared/Epic Games/UE_5.8"
}

UE_ROOT="$(resolve_ue_root)"
EDITOR_CMD="${UE_ROOT}/Engine/Binaries/Mac/UnrealEditor-Cmd"
BUILD_SH="${UE_ROOT}/Engine/Build/BatchFiles/Mac/Build.sh"
UE_MINOR_VERSION="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("MinorVersion",8))' "${UE_ROOT}/Engine/Build/Build.version" 2>/dev/null || echo 8)"

BASELINE_DIR="${ROOT_DIR}/.memory/baseline/$(date +%Y%m%d_%H%M%S)"
SMOKE_DIR="${ROOT_DIR}/Saved/NetSmoke"
SERVER_MAP="/Engine/Maps/Entry?listen?game=/Script/UEToolkit.NetSmokeGameMode"
CLIENT_ADDRESS="127.0.0.1"
TEST_PREFIXES=(Game.Core Game.Data Game.Gameplay Game.GameplayCore Game.Logging)

SKIP_BUILD=0
for arg in "$@"; do
    case "${arg}" in
        --skip-build) SKIP_BUILD=1 ;;
        *)
            echo "未知参数：${arg}"
            exit 2
            ;;
    esac
done

mkdir -p "${BASELINE_DIR}"
FAILED=0

log_step() { echo "==> $*"; }

mark_failed() {
    echo "[失败] $*"
    FAILED=1
}

# 清理上一轮残留的编辑器进程，避免端口与结果文件串味
cleanup_processes() {
    log_step "清理残留编辑器进程"
    local pids
    pids="$(pgrep -f "UnrealEditor-Cmd .*UEToolkit.uproject" || true)"
    if [[ -n "${pids}" ]]; then
        echo "${pids}" | xargs kill -9 2>/dev/null
    fi
    rm -rf "${SMOKE_DIR}"
    sleep 2
}

build_editor() {
    if [[ "${SKIP_BUILD}" -eq 1 ]]; then
        log_step "跳过编译"
        return
    fi

    log_step "编译 UEToolkitEditor Mac Development (UE 5.${UE_MINOR_VERSION})"
    local extra_ubt_flags=(-NoTrace -WaitMutex)
    if [[ "${UE_MINOR_VERSION}" -ge 4 ]]; then
        extra_ubt_flags=(-NoUBA "${extra_ubt_flags[@]}")
    fi
    if ! "${BUILD_SH}" UEToolkitEditor Mac Development -project="${UPROJECT}" "${extra_ubt_flags[@]}" \
        > "${BASELINE_DIR}/build.log" 2>&1; then
        mark_failed "编译失败，见 ${BASELINE_DIR}/build.log"
        return
    fi

    if grep -qE "error:|warning:" "${BASELINE_DIR}/build.log"; then
        mark_failed "编译存在 error/warning，见 ${BASELINE_DIR}/build.log"
    fi
}

run_tests() {
    log_step "运行全量 Game 自动化测试"
    "${EDITOR_CMD}" "${UPROJECT}" "-ExecCmds=Automation RunTests Game; Quit" \
        -unattended -nullrhi -nosplash -DDC-ForceMemoryCache \
        -log -stdout -FullStdOutLogOutput > "${BASELINE_DIR}/tests.log" 2>&1

    if ! grep -q "TEST COMPLETE. EXIT CODE: 0" "${BASELINE_DIR}/tests.log"; then
        mark_failed "自动化测试未通过，见 ${BASELINE_DIR}/tests.log"
        return
    fi

    local prefix
    for prefix in "${TEST_PREFIXES[@]}"; do
        if ! grep -q "Path={${prefix}\." "${BASELINE_DIR}/tests.log"; then
            mark_failed "前缀 ${prefix} 无测试执行"
        fi
    done

    local failed_count
    failed_count=$(grep -c "Result={Fail}\|Result={Error}" "${BASELINE_DIR}/tests.log")
    if [[ "${failed_count}" -gt 0 ]]; then
        mark_failed "存在 ${failed_count} 个 Fail/Error 用例"
    fi
}

wait_for_log_pattern() {
    local log_file="$1"
    local pattern="$2"
    local timeout_seconds="$3"
    local elapsed=0

    while [[ "${elapsed}" -lt "${timeout_seconds}" ]]; do
        if [[ -f "${log_file}" ]] && grep -q "${pattern}" "${log_file}"; then
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    return 1
}

run_net_smoke() {
    log_step "网络链路冒烟：服务端施加 Buff → 客户端确认状态标签"
    local server_log="${BASELINE_DIR}/net_smoke_server.log"
    local client_log="${BASELINE_DIR}/net_smoke_client.log"

    "${EDITOR_CMD}" "${UPROJECT}" "${SERVER_MAP}" -game -nullrhi -unattended -nosplash -DDC-ForceMemoryCache \
        -log -stdout -FullStdOutLogOutput > "${server_log}" 2>&1 &
    local server_pid=$!

    if ! wait_for_log_pattern "${server_log}" "IpNetDriver listening on port" 240; then
        mark_failed "服务端网络驱动未就绪，见 ${server_log}"
        kill -9 "${server_pid}" 2>/dev/null
        return
    fi

    "${EDITOR_CMD}" "${UPROJECT}" "${CLIENT_ADDRESS}" -game -nullrhi -unattended -nosplash -DDC-ForceMemoryCache \
        -log -stdout -FullStdOutLogOutput > "${client_log}" 2>&1 &
    local client_pid=$!

    local elapsed=0
    while [[ "${elapsed}" -lt 300 ]]; do
        if [[ -f "${SMOKE_DIR}/ClientResult.txt" ]]; then
            break
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done

    local client_result=""
    if [[ -f "${SMOKE_DIR}/ClientResult.txt" ]]; then
        client_result="$(cat "${SMOKE_DIR}/ClientResult.txt")"
    fi

    kill -9 "${client_pid}" 2>/dev/null
    kill -9 "${server_pid}" 2>/dev/null
    wait "${client_pid}" 2>/dev/null
    wait "${server_pid}" 2>/dev/null

    if [[ "${client_result}" != "PASS" ]]; then
        mark_failed "客户端未确认 Buff 状态（结果：${client_result:-无}），见 ${client_log}"
    fi

    if ! grep -q "IpNetDriver listening on port" "${server_log}"; then
        mark_failed "服务端未建立监听网络驱动"
    fi
}

archive_results() {
    log_step "归档结果到 ${BASELINE_DIR}"
    cp -R "${SMOKE_DIR}" "${BASELINE_DIR}/smoke_result" 2>/dev/null
    {
        echo "测试前缀：${TEST_PREFIXES[*]}"
        echo "测试通过数：$(grep -c 'Result={Success}' "${BASELINE_DIR}/tests.log" 2>/dev/null)"
        echo "客户端冒烟结果：$(cat "${SMOKE_DIR}/ClientResult.txt" 2>/dev/null)"
        echo "服务端冒烟结果：$(cat "${SMOKE_DIR}/ServerResult.txt" 2>/dev/null)"
    } > "${BASELINE_DIR}/summary.txt"
}

cleanup_processes
build_editor
run_tests
run_net_smoke
archive_results

if [[ "${FAILED}" -ne 0 ]]; then
    echo "[CI] 未通过，日志目录：${BASELINE_DIR}"
    exit 1
fi

echo "[CI] 全绿，日志目录：${BASELINE_DIR}"
