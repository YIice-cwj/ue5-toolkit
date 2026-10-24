@echo off
REM =============================================================================
REM VSCode / Clangd 编译数据库同步脚本 (update_compile_commands.bat) —— Windows 一键入口
REM
REM 本批处理为薄封装：核心逻辑收敛在 Script/update_compile_commands.py（跨平台同语义），
REM 与 update_compile_commands.sh 保持一致；自适应推导引擎与项目路径。
REM
REM 使用方法:
REM   update_compile_commands.bat [--config <path>]
REM =============================================================================
setlocal

set "SCRIPT_DIR=%~dp0"
set "PYTHON_SCRIPT=%SCRIPT_DIR%Script\update_compile_commands.py"

REM 复用 Tools 共享批处理库（Python 解释器探测）
call "%~dp0..\common.bat"

if not defined PYTHON_BIN (
    echo [错误] 未检测到 Python 3 解释器，请先安装 Python 3 或将其添加至环境变量 PATH。 1>&2
    exit /b 1
)

"%PYTHON_BIN%" "%PYTHON_SCRIPT%" %*
set "EXIT_CODE=%ERRORLEVEL%"

endlocal & exit /b %EXIT_CODE%

