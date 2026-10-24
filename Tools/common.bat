@echo off
REM =============================================================================
REM Tools 通用批处理工具库 (common.bat)
REM
REM 供 Tools/ 下各 Windows 入口脚本 call 复用，收敛跨脚本重复实现的通用探测例程。
REM 使用方式（在调用方脚本中）:
REM   call "%~dp0..\common.bat"
REM   if not defined PYTHON_BIN ( ... )
REM =============================================================================

REM 探测可用的 Python 3 解释器命令名并写入 PYTHON_BIN（优先 py，其次 python3，最后 python）
set "PYTHON_BIN="
where py >nul 2>nul && set "PYTHON_BIN=py"
if not defined PYTHON_BIN where python3 >nul 2>nul && set "PYTHON_BIN=python3"
if not defined PYTHON_BIN where python >nul 2>nul && set "PYTHON_BIN=python"
