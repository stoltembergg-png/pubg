#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>
#include "SDK/ActorEntity.h"
#include "SDK/EngineStructs.h"

// Synchronization contract: GSharedData must be read or written while holding
// GDataMutex. Prefer copying a complete snapshot under the lock before using it.
// TODO: Remove remaining direct global reads (for example in Aimbot.cpp) and
// expose only synchronized snapshots to make this contract enforceable.
struct SharedData {
    // These views own deep copies made at publication time. They never point
    // at Engine's mutable actor cache.
    std::vector<std::shared_ptr<ActorEntity>> Actors;
    std::vector<std::shared_ptr<ActorEntity>> GrenadeActors;  // Grenade/Molotov ESP
    // Canonical value snapshots, produced in the same locked publication.
    std::vector<ActorEntity> ActorValues;
    std::vector<ActorEntity> GrenadeActorValues;
    CameraCacheEntry CameraCache;
    uint64_t UWorld = 0;
    uint64_t GNames = 0;
    uint64_t GameInstance = 0;
    uint64_t AcknowledgedPawn = 0;
    uint64_t LocalCharacterPawn = 0; // Character pawn addr (persists even when driving vehicle)
    uint64_t CameraManagerAddr = 0;
    Vector3 Recoil = {0.f, 0.f, 0.f};
    int LocalTeamid = 0;
    int SpectatedCount = 0;
    float CurrentBulletSpeed = 0.0f;
    float CurrentGravity = -9.8f;
    uint32_t MemoryThreadId = 0;
    long long LastUpdateTime = 0;
};

extern SharedData GSharedData;
extern std::mutex GDataMutex;
extern std::atomic<bool> GIsRunning;
extern std::condition_variable GMemoryWake;
extern std::mutex GMemoryWaitMutex;
extern std::thread GMemoryThread;
