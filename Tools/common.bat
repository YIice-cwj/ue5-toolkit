@echo off
REM =============================================================================
REM Tools 通用批处理工具库 (common.bat)
REM
REM 供 Tools/ 下各 Windows 入口脚本 call 复用，收敛跨脚本重复实现的通用探测例程。
REM 使用方式（在调用方脚本中）:
REM   call "%~dp0..\common.bat"
REM   if not defined PYTHON_BIN ( ... )
REM =============================================================================

REM 探测可用的 Python 3 解释器命令名并写入 PYTHON_BIN（支持 UE_PYTHON_BIN 覆盖，并实测过滤 Windows Store 空壳别名与 Python 2）
set "PYTHON_BIN="
if defined UE_PYTHON_BIN (
    "%UE_PYTHON_BIN%" -c "import sys; sys.exit(0 if sys.version_info.major >= 3 else 1)" >nul 2>nul && set "PYTHON_BIN=%UE_PYTHON_BIN%"
)
if not defined PYTHON_BIN (
    for %%C in (py python3 python) do (
        if not defined PYTHON_BIN (
            where %%C >nul 2>nul && (
                %%C -c "import sys; sys.exit(0 if sys.version_info.major >= 3 else 1)" >nul 2>nul && set "PYTHON_BIN=%%C"
            )
        )
    )
)

