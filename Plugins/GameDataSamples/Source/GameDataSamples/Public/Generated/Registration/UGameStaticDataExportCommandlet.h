// Automatically generated static data export commandlet. Do not edit manually.
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "UGameStaticDataExportCommandlet.generated.h"

/**
 * @brief 静态数据 .bin 导出命令行工具（Cook 前置）
 *
 * 设计思路：以 -run= 无头执行，把 JSON 全量导出为运行期 .bin 后按结果返回退出码
 * （0 成功 / 1 失败），命令名 GameStaticDataExport；
 * 全程不依赖世界与会话，供打包流水线在 Cook 前确定性调用
 */
UCLASS()
class GAMEDATASAMPLES_API UGameStaticDataExportCommandlet : public UCommandlet {
    GENERATED_BODY()

public:
    /**
     * @brief 命令入口（导出成功返回 0，失败返回 1）
     * @param Params 命令行参数（本工具无需额外参数）
     * @return 进程退出码
     */
    virtual int32 Main(const FString& Params) override;
};
