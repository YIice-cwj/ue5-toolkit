/**
 * @file UGameAssetTestReceiver.cpp
 * @brief UGameAssetTestReceiver 蓝图动态多播委托测试接收器实现
 */

#include "Tests/UGameAssetTestReceiver.h"

#include "Blueprint/AsyncAction_LoadGameAsset.h"
#include "Blueprint/AsyncAction_LoadGameAssetsBatch.h"

void UGameAssetTestReceiver::HandleAssetProgress(float InProgress) {
    ++ProgressCount;
    LastProgress = InProgress;

    if (SingleActionToCancelOnProgress != nullptr) {
        UAsyncAction_LoadGameAsset* Action = SingleActionToCancelOnProgress;
        SingleActionToCancelOnProgress = nullptr;
        Action->Cancel();
    }

    if (BatchActionToCancelOnProgress != nullptr) {
        UAsyncAction_LoadGameAssetsBatch* BatchAction = BatchActionToCancelOnProgress;
        BatchActionToCancelOnProgress = nullptr;
        BatchAction->Cancel();
    }
}
