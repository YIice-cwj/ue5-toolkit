// Automatically generated static data row serializers. Do not edit manually.
#pragma once

#include "CoreMinimal.h"

#include "StaticData/FStaticDataRepository.h"

/**
 * @brief 注册全部生成式行负载读写器（免反射，供 FStaticDataRepository 按结构名调度）
 * @param Repository 目标仓库（登记后方可按行结构类型 AddTable / ExtractTable）
 */
GAMEDATASAMPLES_API void RegisterGameStaticDataRowSerializers(FStaticDataRepository& Repository);
