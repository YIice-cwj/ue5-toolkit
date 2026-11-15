// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

/**
 * @brief GamePool 全局性能与统计指标组（可在控制台键入 stat GamePool 开启实时监控）
 */
DECLARE_STATS_GROUP(TEXT("GamePool"), STATGROUP_GamePool, STATCAT_Advanced);

DECLARE_CYCLE_STAT_EXTERN(TEXT("Acquire Actor Total"), STAT_GamePool_AcquireActor, STATGROUP_GamePool, GAMEPOOL_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Release Actor Total"), STAT_GamePool_ReleaseActor, STATGROUP_GamePool, GAMEPOOL_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Acquire UObject Total"), STAT_GamePool_AcquireObject, STATGROUP_GamePool, GAMEPOOL_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Release UObject Total"), STAT_GamePool_ReleaseObject, STATGROUP_GamePool, GAMEPOOL_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Component Deep Reset"), STAT_GamePool_ComponentReset, STATGROUP_GamePool, GAMEPOOL_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Prewarm Step Time"), STAT_GamePool_PrewarmStep, STATGROUP_GamePool, GAMEPOOL_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Eviction Step Time"), STAT_GamePool_EvictStep, STATGROUP_GamePool, GAMEPOOL_API);

DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Active Actors"), STAT_GamePool_ActiveActors, STATGROUP_GamePool, GAMEPOOL_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Idle Actors"), STAT_GamePool_IdleActors, STATGROUP_GamePool, GAMEPOOL_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Active UObjects"), STAT_GamePool_ActiveObjects, STATGROUP_GamePool, GAMEPOOL_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Idle UObjects"), STAT_GamePool_IdleObjects, STATGROUP_GamePool, GAMEPOOL_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Pool Cache Hits"), STAT_GamePool_Hits, STATGROUP_GamePool, GAMEPOOL_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Pool Cache Misses (Spawns)"), STAT_GamePool_Misses, STATGROUP_GamePool, GAMEPOOL_API);
