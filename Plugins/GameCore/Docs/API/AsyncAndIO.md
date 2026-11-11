# GameCore 异步任务调度与并发协程 API 参考（文档重定向）

> **文档重组通知**：
> - 异步调度、分帧时间切片、节流心跳与 C++20 协程系统完整技术规范与 API 参考已移至专属文档：
>   👉 **[Async.md](./Async.md)**
> - 历史关联模块解耦说明：
>   - **资产异步加载与内存保活**（`FGameAssetLoader` / `FScopedPinnedAsset`）：已迁移至独立插件 `Plugins/GameAssetCore`。
>   - **关卡流转与就绪屏障**（`ULoadingSubsystem` / `FLoadingBarrierToken`）：已迁移至业务流插件 `Plugins/GameFlow`。
>   - **目录变更热重载监听**（`FDirectoryChangeWatcher`）：已归位至数据插件 `Plugins/GameData` (`GameDataCore/Utils`)。
