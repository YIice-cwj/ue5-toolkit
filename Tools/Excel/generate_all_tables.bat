@echo off
REM =============================================================================
REM 数据表全流程管线编排脚本 (generate_all_tables.bat) —— Windows 一键入口
REM
REM 本批处理为薄封装：统一调度逻辑收敛在 Script/run_pipeline.py（跨平台同语义），
REM 与 generate_all_tables.sh 保持一致；不内置任何工程路径与默认配置。
REM
REM 使用方法:
REM   generate_all_tables.bat [--config <path>] [--all | --validate | --struct | --json | --tags | --bin] [生成器额外参数...]
REM   （--bin 为运行期 .bin 导出，需编辑器已编译；由 run_pipeline.py 统一调度）
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

"%PYTHON_BIN%" -c "import openpyxl" >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo [错误] 检测到当前 Python 环境缺少必需依赖 openpyxl！ 1>&2
    echo 请先在命令行执行: pip install -r "%SCRIPT_DIR%..\requirements.txt" 1>&2
    exit /b 1
)

"%PYTHON_BIN%" "%PIPELINE_PY%" %*
set "EXIT_CODE=%ERRORLEVEL%"

endlocal & exit /b %EXIT_CODE%