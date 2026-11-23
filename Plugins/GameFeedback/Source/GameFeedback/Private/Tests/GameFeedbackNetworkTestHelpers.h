/**
 * @file GameFeedbackNetworkTestHelpers.h
 * @brief 反馈系统网络自动化测试共享辅助函数（消除 Unity Build 符号重定义）
 */

#pragma once

#if WITH_AUTOMATION_TESTS

#include "Asset/GameFeedbackDefinition.h"
#include "Engine/DemoNetDriver.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "UObject/Package.h"

namespace GameFeedbackNetworkTestHelpers {

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

}  // namespace GameFeedbackNetworkTestHelpers

#endif  // WITH_AUTOMATION_TESTS
