/**
 * @file UGameStaticDataExportCommandlet.cpp
 * @brief 静态数据 .bin 导出命令行工具实现（由 Tools/Excel 管线自动生成）
 */
#include "Generated/Registration/UGameStaticDataExportCommandlet.h"

#include "Generated/Data/StaticData/UStaticDataSubsystem.h"

/**
 * @brief 命令入口（执行静态数据 .bin 全量导出，成功返回 0，失败返回 1）
 * @param Params 命令行参数（本工具无需额外参数）
 * @return 进程退出码
 */
int32 UGameStaticDataExportCommandlet::Main([[maybe_unused]] const FString& Params) {
    return UStaticDataSubsystem::ExportStaticDataBin().IsSuccess() ? 0 : 1;
}
