# Cook 打包验证与冷启动性能评测工具链 (Cook & Benchmark Tools)

本项目提供针对 UE5 打包客户端（Packaged App）的无头（Headless）自动化测试与冷启动基准评测套件，覆盖 **CI 双通道数据语义一致性对拍** 与 **静态数据冷启动反序列化性能基准测试**。

---

## 1. 架构与工具总览

```
[UE5 打包产物 (Staged/Packaged App)]
                │
                ▼
        [run_pipeline.py] (单点调度器)
         ├── 1. [channel_diff.py] ────────► CI 双通道一致性校验
         │                                    强制 .bin / JSON 启动对拍：
         │                                    表数 / 行数 / 内容摘要 Hash 逐一比对
         │
         └── 2. [cold_start_benchmark.py] ──► 冷启动反序列化基准测试
                                              逐通道多轮冷启动，统计反序列化耗时
                                              (Min / Median / Mean / Max) 写入报告
```

- **无头驱动 (`packaged_run.py`)**：通过 `-nullrhi -unattended -nosplash` 启动客户端，并注入生成物的启动自检开关 `-<前缀>StaticDataVerify`：冷启动装载完成后输出内容摘要、把装载结果写入存档目录的 `StaticDataVerify.txt` 并自行退出，不再依赖启动期 `-ExecCmds` 调度业务命令的时序。报告文件是 Shipping 裁剪日志（`NO_LOGGING=1`）、且 macOS 忽略 `RequestExitWithStatus` 退出码时的唯一可靠取证通道。
- **纯配置驱动**：所有工程与打包产物路径均由 `generator_config.json` 驱动，不内置写死路径；命令名与开关名按 `project_prefix` 统一推导（可用 `console_commands` 段覆盖）。

---

## 2. 快速使用

在工程根目录或 `Tools/Cook` 目录下执行：
- macOS / Linux: `./Tools/Cook/run_cook_tools.sh`
- Windows: `Tools\Cook\run_cook_tools.bat`

### 常见使用模式

```bash
# 1. 完整流水线（默认：先校验双通道一致性，通过后执行冷启动基准测试）
./Tools/Cook/run_cook_tools.sh

# 2. 仅执行 CI 双通道一致性对拍（任一不一致以非零状态码退出，适于 CI 门禁）
./Tools/Cook/run_cook_tools.sh --diff

# 3. 仅执行冷启动反序列化性能基准测试
./Tools/Cook/run_cook_tools.sh --benchmark

# 4. 指定测试轮数与通道（如跑 5 轮，仅测试 .bin 二进制通道）
./Tools/Cook/run_cook_tools.sh --benchmark --runs 5 --channel bin

# 5. 自定义配置文件与输出报告路径
./Tools/Cook/run_cook_tools.sh --config Tools/Excel/generator_config.json --out Docs/Plans/My_Benchmark_Report.md
```

---

## 3. 目录结构

```
Tools/Cook/
├── README.md                      # 工具链说明文档（本文件）
├── run_cook_tools.sh              # macOS / Linux Shell 统一执行入口
├── run_cook_tools.bat             # Windows 批处理统一执行入口
└── Script/
    ├── run_pipeline.py            # 单点统一调度器
    ├── packaged_run.py            # 打包客户端无头运行与日志捕获底层库
    ├── cold_start_benchmark.py    # 冷启动反序列化耗时基准采集器
    └── channel_diff.py            # CI 双通道一致性校验器
```

---

## 4. 依赖项与前置要求

1. **Python 3.8+**（无需第三方依赖，使用内置 `json`, `os`, `subprocess`, `statistics` 等标准库）；
2. **打包客户端**：需已完成打包并将路径配置于 `generator_config.json` 的 `packaged_app_path` 项；
3. **启动自检开关**：打包产物须由当前版本生成管线生成（含 `-<前缀>StaticDataVerify` 自检钩子），否则无法采集摘要；
4. **打包前置**：Cook 前先以导出命令行工具生成 `.bin`（`python3 .scripts/verify_package.py` 会自动完成安装、生成、导出与打包）。

