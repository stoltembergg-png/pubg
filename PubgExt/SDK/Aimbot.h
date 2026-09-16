#pragma once
#include "Engine.h"
#include <atomic>
#include <cmath>
#include <random>
#include <memory>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class Aimbot {
private:
    static uint64_t currentTargetAddress;
    static int currentBoneId;
    static int lastMoveX;
    static int lastMoveY;
    
    static float lastDx;
    static float lastDy;
    static float remainderX;
    static float remainderY;
    
    static std::chrono::steady_clock::time_point lastTargetSwitchTime;
    static std::chrono::steady_clock::time_point lastTargetSeenTime;
    static bool targetInGrace;
    static std::mt19937 rng;

    static bool IsHotkeyDown(int index);
    
    static UERotator CalcAngle(Vector3 localPos, Vector3 predictedPos);
    static UERotator NormalizeAngle(UERotator angles);

    static void MoveMouseTowards(Vector2 targetScreenPos, Vector2 viewCenter);
    static float CalculateTargetScore(std::shared_ptr<ActorEntity> ent, Vector3 targetPos, float fov);
    static int GetBestBone(std::shared_ptr<ActorEntity> ent, Vector2 viewCenter);

public:
    static void Tick();
};
