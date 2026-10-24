# 工程构建与 IDE 语言服务工具 (Build & IDE Tools)

本目录用于存放项目构建、工程生成及 VSCode / Clangd 相关的维护工具脚本。

---

## 1. 编译数据库同步脚本 (`update_compile_commands.sh` / `.bat`)

### 1.1 功能背景
虚幻引擎（UBT）的常规编译（`Build.sh`）仅负责生成最终运行程序与中间编译参数（`*.cpp.o.rsp`），**不会自动写入** `.vscode/compileCommands_*.json`。当项目新增 C++ 源码、调整目录结构（如移动至 `Private/`）或增删插件依赖后，会导致 VSCode 中的 Clangd 报错找不到头文件。

该工具会自动扫描项目 `Intermediate/` 目录中 UBT 最新编译出的所有 `.rsp` 响应文件，一键提取真实完整的编译参数，同步写入：
- `.vscode/compileCommands_<ProjectName>.json`
- `.vscode/compileCommands_Default.json`
- `.vscode/compile_commands.json`（软链接 / 复制）

---

### 1.2 使用方法

#### 命令行执行
在项目根目录或任何子目录下直接运行：
```bash
# macOS / Linux
./Tools/Build/update_compile_commands.sh

# Windows
Tools\Build\update_compile_commands.bat
```

可选参数：
- `--config <path>`：指定工具配置文件（缺省自动查找 `generator_config.json`）。

#### 刷新 VSCode 提示
同步完成后，在 VS Code 中按快捷键：
- <kbd>Cmd</kbd> + <kbd>Shift</kbd> + <kbd>P</kbd> (Windows: <kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>P</kbd>) -> 执行 **`clangd: Restart language server`**

---

### 1.3 引擎源码目录与编译器解析

该工具遵循“约定优于配置”理念，全自动解析引擎源码目录与编译器路径：

1. **引擎源码目录 (`engine_source_dir`)**：
   - 优先读取配置文件中的 `engine_source_dir`（相对路径按配置文件所在目录展开）；
   - 其次检测环境变量 `UE_ENGINE_DIR`；
   - 缺省时自动根据项目根目录 `*.uproject` 中的 `EngineAssociation` 智能推导各平台默认引擎路径（macOS `/Users/Shared/Epic Games/UE_<version>/Engine/Source`、Windows 注册表及标准安装路径、Linux 路径）；
   - 仍未找到时可从已探测的虚幻编辑器路径反向解析；
   - 全部未命中时输出明确指引。

2. **Clang++ 编译器 (`clang++`)**：
   - 优先环境变量 `UE_CLANG_COMPILER`；
   - 其次由系统工具链动态探测（macOS `xcrun` / 系统 `PATH` / Windows LLVM 路径）。

---

### 1.4 目录结构
```
Tools/Build/
├── README.md                          # 工具说明文档
├── update_compile_commands.sh         # Shell 执行入口 (macOS / Linux)
├── update_compile_commands.bat        # 批处理执行入口 (Windows)
└── Script/
    └── update_compile_commands.py     # 编译参数解析与 JSON 生成核心脚本
```
