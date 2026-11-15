// Copyright Epic Games, Inc. All Rights Reserved.

#include "Telemetry/ObjectPoolStats.h"

DEFINE_STAT(STAT_GamePool_AcquireActor);
DEFINE_STAT(STAT_GamePool_ReleaseActor);
DEFINE_STAT(STAT_GamePool_AcquireObject);
DEFINE_STAT(STAT_GamePool_ReleaseObject);
DEFINE_STAT(STAT_GamePool_ComponentReset);
DEFINE_STAT(STAT_GamePool_PrewarmStep);
DEFINE_STAT(STAT_GamePool_EvictStep);

DEFINE_STAT(STAT_GamePool_ActiveActors);
DEFINE_STAT(STAT_GamePool_IdleActors);
DEFINE_STAT(STAT_GamePool_ActiveObjects);
DEFINE_STAT(STAT_GamePool_IdleObjects);
DEFINE_STAT(STAT_GamePool_Hits);
DEFINE_STAT(STAT_GamePool_Misses);
