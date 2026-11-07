# GameCore 消息通信实战指南

本文指导在实际游戏开发中，如何在 `UGameplayMessageSubsystem`、`FValueObserver` 与 `FListenerHub` 之间做出正确的技术选型，并提供完整的模块间解耦通信实战代码。

---

## 1. 选型指南决策树

```text
通信需求
│
├── 是单个变量/状态的持续追踪与响应式监听（需当前值自动回放）？
│   └── 选【FValueObserver<T>】（最轻量，直接内嵌作为实体成员变量）
│
└── 是事件/消息的解耦广播？
    ├── 需要跨模块业务解耦、GameplayTag 树形寻址、蓝图访问或工作线程安全入队？
    │   └── 选【UGameplayMessageSubsystem】（全项目业务层统一推荐骨干）
    │
    └── 是纯 C++ 引擎/底层子系统内部分发，载荷为非反射纯结构体，追求微秒级零开销？
        └── 选【FListenerHub】（底层纳秒级静态事件总线）
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

## 4. 实战案例：实体响应式属性监听与解耦

### 目标
实体/组件维护状态（如血量、暴击率），UI 或其他系统订阅变化，解耦且订阅即得初始值。

### 方案：使用内聚的 `FValueObserver<T>`

```cpp
#include "Listener/FValueObserver.h"

// 战斗组件声明响应式属性
class UCombatAttributeComponent : public UActorComponent {
public:
    FValueObserver<float>& GetCritChanceObserver() { return CritChance; }

private:
    FValueObserver<float> CritChance{0.05f};
};

// UI 模块：订阅（SubscribeWithReplay 立即拿到当前值，之后每次变化收到回调）
void UCombatHudWidget::BindCritChance(UCombatAttributeComponent* AttributeComp)
{
    if (!AttributeComp) return;

    CritSubscription = AttributeComp->GetCritChanceObserver().SubscribeWithReplay(
        this,
        [this](const float& Chance) {
            UpdateCritLabel(Chance);
        }
    );
}
```

- Key 与类型由业务约定，不需要在框架中登记枚举或结构体。
- UI 控件销毁时 `CritSubscription` 自动退订；关卡结束随 World 子系统一并回收。
- 同一 Key 被注册为不同类型时会返回空守卫并记录 Error，不会发生类型踩踏。

