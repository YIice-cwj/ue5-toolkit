# UE5 通用工程与自动化工具链 (UE5 Foundation Toolchain)

本目录为 Unreal Engine 5 跨项目通用自动化工具套件，采用**配置驱动、零硬编码、跨平台同语义**设计，支持直接剥离为独立的 Git 仓库（如 `ue5-foundation-tools`）或作为 Git Submodule 挂载在任何 UE5 工程中使用。

---

## 1. 工具套件概览

| 模块 | 核心功能 | 跨平台入口 |
| :--- | :--- | :--- |
| **`Tools/Script`** | 通用基础设施与配置加载器（提供约定优于配置、项目/引擎/编译器路径自适应推导） | 共享 Python 模块 `tool_config` |
| **`Tools/Build`** | VSCode / Clangd 编译数据库自适应生成（动态识别项目 `.uproject`，支持 rsp 解析与软链接兜底） | `update_compile_commands.sh` / `.bat` |
| **`Tools/Excel`** | 数据表全流程管线（跨表外键校验、C++ RowStruct/容器/子系统生成、JSON 导出、双向逆向同步、写前快照回滚） | `generate_all_tables.sh` / `.bat` |
| **`Tools/Cook`** | 打包产物 CI 双通道一致性对拍（.bin vs JSON）与无头冷启动反序列化基准采集 | `run_cook_tools.sh` / `.bat` |

---

## 2. 依赖与运行环境

* **操作系统**：macOS (Apple Silicon / Intel) / Windows 10/11 / Linux
* **解释器**：Python 3.8 及以上版本
* **运行库**：仅需单个极简依赖库 `openpyxl`（处理 Excel 数据）：
  ```bash
  pip install -r requirements.txt
  ```

---

## 3. 剥离为独立 Git 仓库说明

若需将本目录剥离为独立仓库：
1. **推荐仓库命名**：
   * 仅包含脚本时：`ue5-pipeline-tools` / `ue5-devops-tools`
   * 包含通用插件时：`ue5-foundation-kit` / `ue5-common-toolkit`
2. **根目录保留规范**：
   * 必须包含 `.gitattributes`（声明 `*.bat text eol=crlf` 与 `*.sh text eol=lf`）
   * 必须包含 `requirements.txt`

---

## 4. 在新项目中接入指南

### 方式 A：Git Submodule（推荐标准做法）

1. 在新项目根目录下挂载本仓库：
   ```bash
   cd /path/to/NewProject
   git submodule add <Tools仓库Git地址> Tools
   ```
2. 安装 Python 依赖：
   ```bash
   pip install -r Tools/requirements.txt
   ```
3. 初始化本地配置：
   ```bash
   cd Tools/Excel
   cp generator_config.example.json generator_config.json
   # 修改 generator_config.json 中的 output_module_dir 与 api_macro 为新项目的模块名称即可
   ```

### 方式 B：本地开发软链接（个人多项目共用）

* **macOS / Linux**：
  ```bash
  ln -s /path/to/FoundationTools Tools
  ```
* **Windows**（以管理员运行 CMD）：
  ```cmd
  mklink /D "Tools" "D:\Dev\FoundationTools"
  ```

---

## 5. 日常使用命令速查

```bash
# 同步 VSCode / Clangd 索引
./Tools/Build/update_compile_commands.sh

# 数据表完整性校验
./Tools/Excel/generate_all_tables.sh --validate

# 数据表完整正向生成 (校验 + C++ 代码生成 + JSON导出 + GameplayTag注册)
./Tools/Excel/generate_all_tables.sh

# 逆向同步 (JSON 自动写回 Excel)
./Tools/Excel/generate_all_tables.sh --reverse
```

