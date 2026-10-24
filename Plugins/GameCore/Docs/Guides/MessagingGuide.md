# GameCore 消息通信实战指南

本文指导在实际游戏开发中，如何在 `FListenerHub`、`UGameplayMessageSubsystem`、`FValueObserver` 与 `FObservableRegistry` 之间做出正确的技术选型，并提供完整的模块间解耦通信实战代码。

---

## 1. 选型指南决策树

```text
通信需求
│
├── 是任意业务数据（暴击/闪避/冷却等），写入方与订阅方互不感知？
│   └── 选【FObservableRegistry】（按 FName Key 注册，订阅即回放当前值）
│
├── 是单个变量/状态的持续追踪？
│   └── 选【FValueObserver<T>】（最轻量，直接包装成员变量）
│
├── 需不需要蓝图访问，或者基于 GameplayTag 进行通道层级管理？
│   ├── 是 ──> 选【UGameplayMessageSubsystem】（GAS/蓝图/UI推荐）
│   └── 否 ──> 选【FListenerHub】（极致性能纯 C++ 底层通信推荐）
```

---

## 2. 实战案例：关卡成就系统与战斗系统解耦

### 目标
当怪物死亡时，战斗模块需要通知成就系统增加击杀计数，但战斗模块严禁直接 `#include "AchievementSystem.h"`。

### 方案：使用 `UGameplayMessageSubsystem`

#### 步骤 1：在公共头文件中声明事件消息
```cpp
// CombatMessageTypes.h
#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "CombatMessageTypes.generated.h"

USTRUCT(BlueprintType)
struct FEnemyKilledMessage {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    FName EnemyId;

    UPROPERTY(BlueprintReadWrite)
    int32 ExpReward = 0;
};
```

#### 步骤 2：成就系统注册监听
```cpp
// AchievementSubsystem.cpp
void UAchievementSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // 监听所有击杀事件
    KilledListenerHandle = UGameplayMessageSubsystem::Get(this).RegisterListener<FEnemyKilledMessage>(
        FGameplayTag::RequestGameplayTag(TEXT("Event.Combat.EnemyKilled")),
        [this](FGameplayTag Channel, const FEnemyKilledMessage& Message) {
            this->OnEnemyDefeated(Message.EnemyId, Message.ExpReward);
        }
    );
}

void UAchievementSubsystem::Deinitialize()
{
    KilledListenerHandle.Unregister();
    Super::Deinitialize();
}
```

#### 步骤 3：战斗系统仅需广播，零耦合成就代码
```cpp
// MonsterCharacter.cpp
void AMonsterCharacter::Die()
{
    FEnemyKilledMessage Msg;
    Msg.EnemyId = this->EnemyType;
    Msg.ExpReward = this->ExpValue;

    UGameplayMessageSubsystem::Get(this).BroadcastMessage(
        FGameplayTag::RequestGameplayTag(TEXT("Event.Combat.EnemyKilled")),
        Msg
    );
}
```

---

## 3. 实战案例：高性能纯 C++ 网络协议包分发

### 目标
网络模块接收到协议包后，以零反射、零 UObject 查找的极致性能分发给对应的 C++ 业务处理器。

### 方案：使用 `FListenerHub`

```cpp
// 1. 定义底层协议负载（纯 C++ 结构体）
struct FNetworkPacketPayload {
    int32 PacketId;
    TArray<uint8> Buffer;
};

// 2. 业务模块在初始化时订阅
const TSharedRef<FObserverSubscription> Sub = FListenerHub::Get().Subscribe<FNetworkPacketPayload>(
    TEXT("Net.OnPacketReceived"),
    [](const FNetworkPacketPayload& Packet) {
        // 极速处理协议包
    }
);

// 3. 网络接收线程出锁后派发（耗时仅数十纳秒）
FListenerHub::Get().Emit(TEXT("Net.OnPacketReceived"), RawPacket);
```

---

## 4. 实战案例：任意战斗属性注册与订阅

### 目标
战斗模块写入暴击率，UI 模块读取并展示；两者互不 `#include` 对方头文件。

### 方案：使用 `FObservableRegistry`

```cpp
#include "System/UObservableRegistrySubsystem.h"

// 战斗模块：写入
void UCombatAttributeComponent::RefreshCritChance()
{
    UObservableRegistrySubsystem* Observables = UObservableRegistrySubsystem::Get(this);
    if (!Observables) return;

    Observables->GetRegistry().Set<float>(TEXT("Combat.CritChance"), ComputedCritChance);
}

// UI 模块：订阅（立即拿到当前值，之后每次变化收到回调）
void UCombatHudWidget::BindCritChance()
{
    UObservableRegistrySubsystem* Observables = UObservableRegistrySubsystem::Get(this);
    if (!Observables) return;

    CritSubscription = Observables->GetRegistry().Subscribe<float>(
        TEXT("Combat.CritChance"),
        [this](const float& Chance) { UpdateCritLabel(Chance); });
}
```

- Key 与类型由业务约定，不需要在框架中登记枚举或结构体。
- UI 控件销毁时 `CritSubscription` 自动退订；关卡结束随 World 子系统一并回收。
- 同一 Key 被注册为不同类型时会返回空守卫并记录 Error，不会发生类型踩踏。

