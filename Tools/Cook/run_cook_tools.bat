@echo off
REM =============================================================================
REM Cook 打包验证与性能评测脚本 (run_cook_tools.bat) —— Windows 一键入口
REM
REM 本批处理为薄封装：统一调度逻辑收敛在 Script/run_pipeline.py（跨平台同语义），
REM 与 run_cook_tools.sh 保持一致；不内置任何工程路径与默认配置。
REM
REM 使用方法:
REM   run_cook_tools.bat [--config <path>] [--all | --diff | --benchmark] [--runs N] [--channel bin|json|both] [--out <report.md>]
REM =============================================================================
setlocal

set "SCRIPT_DIR=%~dp0"
set "PIPELINE_PY=%SCRIPT_DIR%Script\run_pipeline.py"

REM 复用 Tools 共享批处理库（Python 解释器探测）
call "%~dp0..\common.bat"

if not defined PYTHON_BIN (
    echo [错误] 未检测到 Python 3 解释器，请先安装 Python 3 或将其添加至环境变量 PATH。 1>&2
    exit /b 1
)

"%PYTHON_BIN%" "%PIPELINE_PY%" %*
set "EXIT_CODE=%ERRORLEVEL%"

endlocal & exit /b %EXIT_CODE%

