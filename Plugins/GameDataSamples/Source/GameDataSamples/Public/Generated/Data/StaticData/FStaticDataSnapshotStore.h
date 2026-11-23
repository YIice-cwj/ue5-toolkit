/**
 * @file FStaticDataSnapshotStore.h
 * @brief 静态数据快照 RCU 存储类型别名（复用 GameDataCore::TStaticDataSnapshotStore）
 */
#pragma once

#include "StaticData/TStaticDataSnapshotStore.h"
#include "Generated/Data/StaticData/FStaticDataSnapshot.h"

/**
 * @brief 静态数据快照 RCU 存储器（复用 GameDataCore 泛型模板 TStaticDataSnapshotStore）
 */
using FStaticDataSnapshotStore = TStaticDataSnapshotStore<FStaticDataSnapshot>;
