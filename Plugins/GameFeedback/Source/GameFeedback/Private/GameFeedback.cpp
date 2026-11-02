// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeedback.h"

#include "Modules/ModuleManager.h"
#include "Logging/GameLogCategories.h"

GAME_REGISTER_MODULE_LOG(LogGameFeedback);

/**
 * @brief 打击感表现解耦中枢运行时模块
 */
class FGameFeedbackModule : public FDefaultModuleImpl {
public:
    FGameFeedbackModule() = default;

    FGameFeedbackModule(const FGameFeedbackModule&) = delete;
    FGameFeedbackModule& operator=(const FGameFeedbackModule&) = delete;
    FGameFeedbackModule(FGameFeedbackModule&&) = delete;
    FGameFeedbackModule& operator=(FGameFeedbackModule&&) = delete;

    /**
     * @brief 模块启动入口，输出加载信息
     */
    void StartupModule() override {
        GAME_LOG(Log, TEXT("【GameFeedback】打击感表现解耦中枢模块初始化完成。"));
    }

    /**
     * @brief 模块卸载入口，输出注销信息
     */
    void ShutdownModule() override {
        GAME_LOG(Log, TEXT("【GameFeedback】打击感表现解耦中枢模块卸载完成。"));
    }
};

IMPLEMENT_MODULE(FGameFeedbackModule, GameFeedback);

