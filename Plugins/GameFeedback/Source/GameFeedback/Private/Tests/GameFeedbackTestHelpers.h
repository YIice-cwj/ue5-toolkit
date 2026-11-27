/**
 * @file GameFeedbackTestHelpers.h
 * @brief GameFeedback 自动化测试共享夹具与隔离世界管理（消除重复定义与全局状态污染）
 */

#pragma once

#if WITH_AUTOMATION_TESTS

#include "Asset/GameFeedbackDefinition.h"
#include "Asset/GameFeedbackRegistry.h"
#include "Config/GameFeedbackSettings.h"
#include "Engine/DemoNetDriver.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "UObject/Package.h"

namespace GameFeedbackTestHelpers {

/**
 * @brief 创建具有稳定资产路径的测试 Definition
 * @param ObjectPath 完整对象路径（Package.Object）
 * @return 测试 Definition 指针
 */
inline UGameFeedbackDefinition* CreateDefinitionAtPath(const FString& ObjectPath) {
    FString PackageName;
    FString ObjectName;
    ObjectPath.Split(TEXT("."), &PackageName, &ObjectName);
    return NewObject<UGameFeedbackDefinition>(CreatePackage(*PackageName), *ObjectName);
}

/**
 * @brief RAII 保护 UGameFeedbackSettings 全局配置，离开作用域自动还原所有改动
 */
class FScopedFeedbackSettingsGuard {
public:
    /**
     * @brief 构造：保存当前全局配置快照
     */
    FScopedFeedbackSettingsGuard() {
        Settings = GetMutableDefault<UGameFeedbackSettings>();
        PrevEnableFeedback = Settings->bEnableFeedback;
        PrevRegistry = Settings->GlobalRegistry;
        PrevHapticScale = Settings->GlobalHapticScale;
        PrevAllowAuthoritativeHitStop = Settings->bAllowAuthoritativeHitStop;
        PrevNetworkCullDistance = Settings->DefaultNetworkCullDistance;
        PrevMaxDispatchPerFrame = Settings->MaxDispatchPerFrame;
    }

    /**
     * @brief 析构：还原全局配置快照
     */
    ~FScopedFeedbackSettingsGuard() {
        if (Settings) {
            Settings->bEnableFeedback = PrevEnableFeedback;
            Settings->GlobalRegistry = PrevRegistry;
            Settings->GlobalHapticScale = PrevHapticScale;
            Settings->bAllowAuthoritativeHitStop = PrevAllowAuthoritativeHitStop;
            Settings->DefaultNetworkCullDistance = PrevNetworkCullDistance;
            Settings->MaxDispatchPerFrame = PrevMaxDispatchPerFrame;
        }
    }

    FScopedFeedbackSettingsGuard(const FScopedFeedbackSettingsGuard&) = delete;
    FScopedFeedbackSettingsGuard& operator=(const FScopedFeedbackSettingsGuard&) = delete;

    /**
     * @brief 获取可变全局配置对象指针
     * @return 全局配置对象
     */
    [[nodiscard]] UGameFeedbackSettings* Get() const noexcept {
        return Settings;
    }

private:
    UGameFeedbackSettings* Settings = nullptr;
    bool PrevEnableFeedback = true;
    TSoftObjectPtr<UGameFeedbackRegistry> PrevRegistry;
    float PrevHapticScale = 1.0f;
    bool PrevAllowAuthoritativeHitStop = false;
    float PrevNetworkCullDistance = 5000.0f;
    int32 PrevMaxDispatchPerFrame = 32;
};

/**
 * @brief RAII 隔离独立 GameInstance 与游戏世界，避免污染编辑器世界或遗留子系统
 */
class FScopedStandaloneFeedbackWorld {
public:
    /**
     * @brief 构造并初始化带独立 GameInstance 的测试世界
     * @param WorldName 测试世界名称
     */
    explicit FScopedStandaloneFeedbackWorld(const FName WorldName) {
        GameInstance = NewObject<UGameInstance>(GEngine);
        GameInstance->InitializeStandalone(WorldName);
        World = GameInstance->GetWorld();
    }

    /**
     * @brief 析构：安全关闭独立 GameInstance 并销毁测试世界
     */
    ~FScopedStandaloneFeedbackWorld() {
        if (GameInstance) {
            GameInstance->Shutdown();
            GameInstance = nullptr;
            World = nullptr;
        }
    }

    FScopedStandaloneFeedbackWorld(const FScopedStandaloneFeedbackWorld&) = delete;
    FScopedStandaloneFeedbackWorld& operator=(const FScopedStandaloneFeedbackWorld&) = delete;

    [[nodiscard]] UGameInstance* GetGameInstance() const noexcept { return GameInstance; }
    [[nodiscard]] UWorld* GetWorld() const noexcept { return World; }

private:
    UGameInstance* GameInstance = nullptr;
    UWorld* World = nullptr;
};

/**
 * @brief RAII 隔离无 GameInstance 的轻量级游戏世界（用于纯世界子系统与执行器测试）
 */
class FScopedLightweightGameWorld {
public:
    /**
     * @brief 构造轻量级 Game 世界
     */
    FScopedLightweightGameWorld() {
        World = UWorld::CreateWorld(EWorldType::Game, false);
    }

    /**
     * @brief 析构：销毁世界并从 Root 移除
     */
    ~FScopedLightweightGameWorld() {
        if (World) {
            World->DestroyWorld(false);
            World->RemoveFromRoot();
            World = nullptr;
        }
    }

    FScopedLightweightGameWorld(const FScopedLightweightGameWorld&) = delete;
    FScopedLightweightGameWorld& operator=(const FScopedLightweightGameWorld&) = delete;

    [[nodiscard]] UWorld* GetWorld() const noexcept { return World; }

private:
    UWorld* World = nullptr;
};

/**
 * @brief 安全清理测试用未初始化模拟网络连接，避免 GC 时在 UNetConnection::Close 越界访问空 Channels
 * @param Connection 待清理的模拟网络连接
 */
inline void CleanupSimulatedConnection(USimulatedClientNetConnection* Connection) {
    if (!Connection) {
        return;
    }
    Connection->SetConnectionState(USOCK_Closed);
    Connection->SetClientLoginState(EClientLoginState::CleanedUp);
    Connection->OwningActor = nullptr;
    Connection->PlayerController = nullptr;
    Connection->PackageMap = nullptr;
    Connection->Driver = nullptr;
    Connection->MarkAsGarbage();
}

/**
 * @brief 解除测试用 DemoNetDriver 与 World 及 WorldContext 的绑定并标记回收
 * @param Driver 待清理的测试网络驱动
 * @param WorldContext 可选的世界上下文（非空时同步从 ActiveNetDrivers 移除）
 */
inline void CleanupTestDemoNetDriver(UDemoNetDriver* Driver, FWorldContext* WorldContext = nullptr) {
    if (!Driver) {
        return;
    }
    Driver->SendRPCDel.Unbind();
    if (WorldContext) {
        WorldContext->ActiveNetDrivers.RemoveAll([Driver](const FNamedNetDriver& Entry) {
            return Entry.NetDriver == Driver;
        });
    }
    Driver->World = nullptr;
    Driver->MarkAsGarbage();
}

}  // namespace GameFeedbackTestHelpers

#endif  // WITH_AUTOMATION_TESTS
