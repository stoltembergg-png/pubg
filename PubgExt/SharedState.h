#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include "SDK/ActorEntity.h"
#include "SDK/EngineStructs.h"

// Synchronization contract: GSharedData must be read or written while holding
// GDataMutex. Prefer copying a complete snapshot under the lock before using it.
// TODO: Remove remaining direct global reads (for example in Aimbot.cpp) and
// expose only synchronized snapshots to make this contract enforceable.
struct SharedData {
    std::vector<std::shared_ptr<ActorEntity>> Actors;
    std::vector<std::shared_ptr<ActorEntity>> GrenadeActors;  // Grenade/Molotov ESP
    CameraCacheEntry CameraCache;
    uint64_t UWorld = 0;
    uint64_t GNames = 0;
    uint64_t GameInstance = 0;
    uint64_t AcknowledgedPawn = 0;
    uint64_t LocalCharacterPawn = 0; // Character pawn addr (persists even when driving vehicle)
    uint64_t CameraManagerAddr = 0;
    Vector3 Recoil = {0.f, 0.f, 0.f};
    uint32_t MemoryThreadId = 0;
    long long LastUpdateTime = 0;
};

extern SharedData GSharedData;
extern std::mutex GDataMutex;
extern bool GIsRunning;
