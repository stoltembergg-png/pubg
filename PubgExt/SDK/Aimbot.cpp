#include "pch.h"
#include "Aimbot.h"
#include "../Config/ConfigUtilities.h"
#include "Camera.h"
#include "../SharedState.h"
#include "../OS-ImGui/OS-ImGui.h"
#include "../Globals.h"
#include <algorithm>
#include <chrono>

// Helper for C++14 compatibility
template <typename T> static T clamp_value(T v, T lo, T hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

uint64_t Aimbot::currentTargetAddress = 0;
int Aimbot::lastMoveX = 0;
int Aimbot::lastMoveY = 0;
float Aimbot::lastDx = 0.0f;
float Aimbot::lastDy = 0.0f;
float Aimbot::remainderX = 0.0f;
float Aimbot::remainderY = 0.0f;
std::chrono::steady_clock::time_point Aimbot::lastTargetSwitchTime;
std::chrono::steady_clock::time_point Aimbot::lastTargetSeenTime;
bool Aimbot::targetInGrace = false;
int Aimbot::currentBoneId = 5;
std::mt19937 Aimbot::rng(std::random_device{}());

bool Aimbot::IsHotkeyDown(int index) {
    int vkey = 0;
    switch(index) {
        case 0: vkey = VK_LMENU; break;     // Left Alt
        case 1: vkey = VK_RBUTTON; break;   // Right Click
        case 2: vkey = VK_LSHIFT; break;    // Left Shift
        case 3: vkey = VK_XBUTTON2; break;  // Mouse 5
        case 4: vkey = VK_XBUTTON1; break;  // Mouse 4
        case 5: vkey = VK_LBUTTON; break;   // Left Click
        default: return false;
    }
    return (GetAsyncKeyState(vkey) & 0x8000) != 0;
}

// Auto bone selection - picks closest bone to crosshair or fallback to config
int Aimbot::GetBestBone(std::shared_ptr<ActorEntity> ent, Vector2 viewCenter) {
    if (!Configs.Aimbot.AutoBone) {
        // Use config TargetBone as fallback
        switch (Configs.Aimbot.TargetBone) {
            case 0: return 1; // Head
            case 1: return 2; // Neck
            case 2: return 5; // Spine / Chest
            case 3: return 0; // Pelvis
        }
        return 5;
    }

    int bestBone = 5; // Default Spine
    float bestDist = 999999.0f;
    int bonesToCheck[] = {1, 2, 5}; // Head, Neck, Chest
    
    for (int bone : bonesToCheck) {
        Vector3 targetPos = ent->GetPosition();
        if (bone == 1) targetPos = Vector3(ent->Head3D.X, ent->Head3D.Y, ent->Head3D.Z);
        else if (bone == 2) targetPos = Vector3(ent->neck3D.X, ent->neck3D.Y, ent->neck3D.Z);
        else if (bone == 5) targetPos = Vector3(ent->pelvis3D.X, ent->pelvis3D.Y, ent->pelvis3D.Z + 30.0f);

        Vector3 camLoc(GSharedData.CameraCache.POV.Location.X, GSharedData.CameraCache.POV.Location.Y, GSharedData.CameraCache.POV.Location.Z);
        float dist = Vector3::Distance(camLoc, targetPos);
        float bulletSpeed = EngineInstance->GetCurrentBulletSpeed(); 
        
        float flyTime = (bulletSpeed > 1.0f) ? (dist / (bulletSpeed * 100.0f)) : 0.0f;
        
        Vector3 predictedTarget = targetPos + (ent->Velocity * flyTime);
        Vector2 screenPos = Camera::WorldToScreen(GSharedData.CameraCache.POV, predictedTarget);
        if (screenPos.x == 0.0f && screenPos.y == 0.0f) continue;

        float dx = screenPos.x - viewCenter.x;
        float dy = screenPos.y - viewCenter.y;
        float distScreen = std::sqrt(dx * dx + dy * dy);

        float bias = (bone == currentBoneId) ? 0.8f : 1.0f;
        if (distScreen * bias < bestDist) {
            bestDist = distScreen;
            bestBone = bone;
        }
    }
    return bestBone;
}

UERotator Aimbot::CalcAngle(Vector3 localPos, Vector3 predictedPos) {
    Vector3 delta = predictedPos - localPos;
    float hyp = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    
    UERotator angles;
    angles.Pitch = -std::atan2(delta.z, hyp) * (180.0f / (float)M_PI);
    angles.Yaw = std::atan2(delta.y, delta.x) * (180.0f / (float)M_PI);
    angles.Roll = 0.0f;
    return angles;
}

UERotator Aimbot::NormalizeAngle(UERotator angles) {
    if (angles.Pitch > 89.0f) angles.Pitch = 89.0f;
    if (angles.Pitch < -89.0f) angles.Pitch = -89.0f;

    while (angles.Yaw > 180.0f) angles.Yaw -= 360.0f;
    while (angles.Yaw < -180.0f) angles.Yaw += 360.0f;

    return angles;
}

float Aimbot::CalculateTargetScore(std::shared_ptr<ActorEntity> ent, Vector3 targetPos, float fov) {
    const float BASE = 100.0f;
    float score = 0.0f;

    float fovWeight = 3.0f;
    float distanceWeight = 1.0f;

    // FOV score (closer to center = higher)
    float fovRatio = fov / Configs.Aimbot.FOV;
    score += (1.0f - std::pow(fovRatio, 1.8f)) * BASE * fovWeight;

    // Distance score (closer = higher)
    Vector3 camLoc(GSharedData.CameraCache.POV.Location.X, GSharedData.CameraCache.POV.Location.Y, GSharedData.CameraCache.POV.Location.Z);
    float dx = targetPos.x - camLoc.x;
    float dy = targetPos.y - camLoc.y;
    float dz = targetPos.z - camLoc.z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    float maxDist = 300.0f * 100.0f; // Approx 300m in unreal units (1 uu = 1 cm)
    float distRatio = (std::min)(dist / maxDist, 1.0f);
    float distScore = (1.0f - std::pow(distRatio, 1.2f)) * BASE * distanceWeight;
    score += distScore;

    // Visibility bonus
    float visBonus = 0.0f;
    if (ent->IsVisible) {
        visBonus = 35.0f;
        score += visBonus; 
    }

    // Persistence bonus (stick to current target) — raised for aggressive lock
    float persistBonus = 0.0f;
    if (ent->Class == currentTargetAddress) {
        persistBonus = 80.0f; 
        score += persistBonus; 
    }

    // Verbose score //LOG for current target
    if (ent->Class == currentTargetAddress) {
        /*
        LOG("[Score] FOV_S: %.1f | Dist_S: %.1f | Vis: %.1f | Pers: %.1f | Total: %.1f\n", 
            (1.0f - std::pow(fovRatio, 1.8f)) * BASE * fovWeight, distScore, visBonus, persistBonus, score);
        */
    }

    return score;
}

void Aimbot::MoveMouseTowards(Vector2 targetScreenPos, Vector2 viewCenter) {
    static std::chrono::steady_clock::time_point lastMoveTime;
    auto now = std::chrono::steady_clock::now();

    if (lastMoveTime.time_since_epoch().count() != 0) {
        auto deltaMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMoveTime).count();
        if (deltaMs < 2) { 
            return;
        }
    }

    float dx = targetScreenPos.x - viewCenter.x;
    float dy = targetScreenPos.y - viewCenter.y;

    float distPixels = std::sqrt(dx * dx + dy * dy);

    if (distPixels <= 1.5f) { 
        remainderX = 0; remainderY = 0;
        return;
    }

    float clampedSmooth = clamp_value(Configs.Aimbot.AR_SmoothValue / 10.0f, 0.01f, 1.0f);
    
    float timeSinceSwitch = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTargetSwitchTime).count() / 1000.0f;
    if (timeSinceSwitch < 0.3f) {
        float progress = timeSinceSwitch / 0.3f;
        clampedSmooth += clampedSmooth * (2.0f - 1.0f) * (1.0f - progress);
    }

    bool isFiring = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (isFiring) {
        clampedSmooth *= 0.8f;
    }

    // Smoothing: AR_SmoothValue and Aggression directly control speed at all ranges
    float aggression = clamp_value(Configs.Aimbot.Aggression, 0.5f, 3.0f);
    float smoothFactor = clampedSmooth / aggression;
    smoothFactor = clamp_value(smoothFactor, 0.15f, 1.0f);

    float curve = 1.0f - std::pow(smoothFactor, 2.0f);
    if (curve < 0.05f) curve = 0.05f; 
    if (curve > 0.45f) curve = 0.45f;
    // ADVANCED DIAGNOSTICS
    /*
    LOG("[Aimbot] Target: (%.1f, %.1f) | Center: (%.1f, %.1f) | Delta: (%.1f, %.1f)\n", 
        targetScreenPos.x, targetScreenPos.y, viewCenter.x, viewCenter.y, dx, dy);
    LOG("[MoveMath] DistP: %.1f | Smooth: %.2f | Curve: %.3f\n", distPixels, smoothFactor, curve);
    */

    dx *= curve;
    dy *= curve;
    
    //LOG("[MoveMath] DistP: %.1f | Smooth: %.2f | Curve: %.2f\n", distPixels, smoothFactor, curve);

    // Random multiplier for organic feel 
    std::uniform_real_distribution<float> randMult(1.0f, 1.5f);
    float maxStep = (distPixels < 80.0f) ? 60.0f : 30.0f;
    maxStep *= aggression * randMult(rng);

    float stepLen = std::sqrt(dx * dx + dy * dy);
    if (stepLen > maxStep && stepLen > 0.0f) {
        float scale = maxStep / stepLen;
        dx *= scale;
        dy *= scale;
    }

    // Subtle jitter layer - reduced for PUBG stability (was 0.6f)
    std::uniform_real_distribution<float> jitterDist(-0.15f, 0.15f);
    dx += jitterDist(rng);
    dy += jitterDist(rng);

    dx *= Configs.Aimbot.DriverScale;
    dy *= Configs.Aimbot.DriverScale;

    float targetX = dx + remainderX;
    float targetY = dy + remainderY;

    int moveX = static_cast<int>(std::round(targetX));
    int moveY = static_cast<int>(std::round(targetY));
    
    remainderX = targetX - static_cast<float>(moveX);
    remainderY = targetY - static_cast<float>(moveY);

    bool breakingX = std::abs(moveX) < std::abs(lastMoveX);
    if (!breakingX && std::abs(moveX - lastMoveX) > Configs.Aimbot.MaxDelta) {
        int oldMove = moveX;
        moveX = lastMoveX + (moveX > lastMoveX ? Configs.Aimbot.MaxDelta : -Configs.Aimbot.MaxDelta);
        //LOG("[BRAKE] X Cap: %d -> %d\n", oldMove, moveX);
    }

    bool breakingY = std::abs(moveY) < std::abs(lastMoveY);
    if (!breakingY && std::abs(moveY - lastMoveY) > Configs.Aimbot.MaxDelta) {
        moveY = lastMoveY + (moveY > lastMoveY ? Configs.Aimbot.MaxDelta : -Configs.Aimbot.MaxDelta);
    }

    lastMoveX = moveX;
    lastMoveY = moveY;

    if (moveX == 0 && moveY == 0) return;

    //LOG("[Aimbot] Inject: [%d, %d] (Rem: %.2f, %.2f)\n", moveX, moveY, remainderX, remainderY);
    TargetProcess.driver.InjectMouseMove(moveX, moveY);
    lastMoveTime = now;
}

void Aimbot::Tick() {
    static uint64_t lastProcessedTime = 0;
    
    if (!Configs.Aimbot.Enabled) {
        currentTargetAddress = 0;
        return;
    }
    
    if (!IsHotkeyDown(Configs.Aimbot.Hotkey)) {
        currentTargetAddress = 0;
        lastMoveX = 0;
        lastMoveY = 0;
        return;
    }
    
    SharedData data;
    {
        std::lock_guard<std::mutex> lock(GDataMutex);
        if (GSharedData.LastUpdateTime <= lastProcessedTime) {
            return;
        }
        data = GSharedData;
        lastProcessedTime = data.LastUpdateTime;
    }

    if (data.Actors.empty() || !EngineInstance) return;

    Vector2 viewCenter(Gui.Window.Size.x / 2.0f, Gui.Window.Size.y / 2.0f);
    Vector3 localPos = Vector3(data.CameraCache.POV.Location.X, data.CameraCache.POV.Location.Y, data.CameraCache.POV.Location.Z);
    
    float fovRad = Configs.Aimbot.FOV * (float)M_PI / 180.0f;
    float fovPixelRadius = std::tan(fovRad * 0.5f) * Gui.Window.Size.y;
    fovPixelRadius = clamp_value(fovPixelRadius, 10.0f, Gui.Window.Size.y * 0.5f);

    std::shared_ptr<ActorEntity> bestTarget = nullptr;
    float bestScore = -999999.0f;
    Vector3 bestTargetPos(0, 0, 0);

    if (Configs.Aimbot.LockOnTarget && currentTargetAddress != 0) {
        for (auto& actor : data.Actors) {
            if (!actor->isCheck || actor->isDie || actor->Health <= 0) continue;
            if (actor->Class != currentTargetAddress) continue;
            bestTarget = actor;
            int boneIndex = GetBestBone(actor, viewCenter);
            if (boneIndex == 1) bestTargetPos = Vector3(actor->Head3D.X, actor->Head3D.Y, actor->Head3D.Z);
            else if (boneIndex == 2) bestTargetPos = Vector3(actor->neck3D.X, actor->neck3D.Y, actor->neck3D.Z);
            else if (boneIndex == 5) bestTargetPos = Vector3(actor->pelvis3D.X, actor->pelvis3D.Y, actor->pelvis3D.Z + 30.0f);
            else if (boneIndex == 0) bestTargetPos = Vector3(actor->pelvis3D.X, actor->pelvis3D.Y, actor->pelvis3D.Z);
            else bestTargetPos = actor->GetPosition();
            break;
        }
    }

    if (!bestTarget) {
        for (auto& actor : data.Actors) {
            if (!actor->isCheck || actor->isDie || actor->Health <= 0) continue;
            if (actor->Class == data.AcknowledgedPawn || actor->LastTeamNum == Local.Teamid) continue;
            if (Configs.Aimbot.VisibilityCheck && !actor->IsVisible) continue;

            int boneIndex = GetBestBone(actor, viewCenter);
            Vector3 targetPos = actor->GetPosition();
            
            if (boneIndex == 1) targetPos = Vector3(actor->Head3D.X, actor->Head3D.Y, actor->Head3D.Z);
            else if (boneIndex == 2) targetPos = Vector3(actor->neck3D.X, actor->neck3D.Y, actor->neck3D.Z);
            else if (boneIndex == 5) targetPos = Vector3(actor->pelvis3D.X, actor->pelvis3D.Y, actor->pelvis3D.Z + 30.0f);
            else if (boneIndex == 0) targetPos = Vector3(actor->pelvis3D.X, actor->pelvis3D.Y, actor->pelvis3D.Z);

            float dist = Vector3::Distance(localPos, targetPos);
            float bulletSpeed = EngineInstance->GetCurrentBulletSpeed();
            float flyTime = (bulletSpeed > 1.0f) ? (dist / (bulletSpeed * 100.0f)) : 0.0f;
            
            Vector3 predictedTarget = targetPos + (actor->Velocity * flyTime);
            Vector2 screenPos = Camera::WorldToScreen(data.CameraCache.POV, predictedTarget);
            if (screenPos.x == 0.0f && screenPos.y == 0.0f) continue;
            
            float dx = screenPos.x - viewCenter.x;
            float dy = screenPos.y - viewCenter.y;
            float distPixels = std::sqrt(dx * dx + dy * dy);
            
            float effectiveFovRadius = fovPixelRadius;
            if (actor->Class == currentTargetAddress) {
                effectiveFovRadius *= 2.5f;
            }
            if (distPixels > effectiveFovRadius) continue;

            float fovDegreesMatch = std::atan2(distPixels, Gui.Window.Size.y) * (180.0f / (float)M_PI) * 2.0f;
            float score = CalculateTargetScore(actor, targetPos, fovDegreesMatch);

            if (score > bestScore) {
                bestScore = score;
                bestTarget = actor;
                bestTargetPos = targetPos;
            }
        }
    }

    if (!bestTarget) {
        if (currentTargetAddress != 0 && !targetInGrace) {
            lastTargetSeenTime = std::chrono::steady_clock::now();
            targetInGrace = true;
            return;
        }
        if (targetInGrace) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - lastTargetSeenTime).count();
            if (elapsed < 500) return; // Extended grace for DMA latency
        }
        currentTargetAddress = 0;
        targetInGrace = false;
        lastMoveX = 0;
        lastMoveY = 0;
        return;
    }

    targetInGrace = false;

    if (currentTargetAddress != bestTarget->Class) {
        //LOG("[Aimbot] New Target: 0x%llX\n", bestTarget->Class);
        lastTargetSwitchTime = std::chrono::steady_clock::now();
        lastMoveX = 0; 
        lastMoveY = 0;
    }
    currentTargetAddress = bestTarget->Class;
    int oldBone = currentBoneId;
    currentBoneId = GetBestBone(bestTarget, viewCenter);
    if (oldBone != currentBoneId) {
        //LOG("[Aimbot] Bone Switch: %d -> %d\n", oldBone, currentBoneId);
    }    
    float dropPitchOffset = 0.0f;
    Vector3 predictedPos = EngineInstance->GetPrediction(bestTarget, bestTargetPos, dropPitchOffset);
    
    float dx_3d = predictedPos.x - localPos.x;
    float dy_3d = predictedPos.y - localPos.y;
    float dist2D = std::sqrt(dx_3d * dx_3d + dy_3d * dy_3d);
    
    float dropPitchRad = dropPitchOffset * ((float)M_PI / 180.0f);
    
    float zShift = 0.0f;
    if (dist2D > 5000.0f) {
        zShift = dist2D * std::tan(dropPitchRad);
    }
    
    predictedPos.z += zShift;

    // PAOD-style recoil compensation - reduces aim movement instead of modifying target position
    bool isFiring = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (isFiring) {
        // Get current camera rotation
        float currentPitch = data.CameraCache.POV.Rotation.Pitch;
        float currentYaw = data.CameraCache.POV.Rotation.Yaw;

        // Calculate angle to target
        float dx = predictedPos.x - localPos.x;
        float dy = predictedPos.y - localPos.y;
        float dz = predictedPos.z - localPos.z;
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        float targetPitch = -std::atan2(dz, std::sqrt(dx * dx + dy * dy)) * (180.0f / (float)M_PI);
        float targetYaw = std::atan2(dy, dx) * (180.0f / (float)M_PI);

        // Normalize angles
        while (targetYaw > 180.0f) targetYaw -= 360.0f;
        while (targetYaw < -180.0f) targetYaw += 360.0f;
        if (targetPitch > 89.0f) targetPitch = 89.0f;
        if (targetPitch < -89.0f) targetPitch = -89.0f;

        // Calculate angle delta
        float deltaPitch = targetPitch - currentPitch;
        float deltaYaw = targetYaw - currentYaw;

        // Normalize delta yaw
        while (deltaYaw > 180.0f) deltaYaw -= 360.0f;
        while (deltaYaw < -180.0f) deltaYaw += 360.0f;

        // Apply recoil compensation (PAOD style - reduces the angle delta)
        float recoilComp = Configs.Aimbot.AR_RecoilValue / 100.0f;
        float recoilCompYaw = Configs.Aimbot.AR_RecoilYaw / 100.0f;

        deltaPitch *= recoilComp;
        deltaYaw *= recoilCompYaw;

        // Calculate final compensated angle
        float finalPitch = currentPitch + deltaPitch;
        float finalYaw = currentYaw + deltaYaw;

        // Normalize final angles
        while (finalYaw > 180.0f) finalYaw -= 360.0f;
        while (finalYaw < -180.0f) finalYaw += 360.0f;
        if (finalPitch > 89.0f) finalPitch = 89.0f;
        if (finalPitch < -89.0f) finalPitch = -89.0f;

        // Convert back to world position using compensated angle
        float pitchRad = finalPitch * ((float)M_PI / 180.0f);
        float yawRad = finalYaw * ((float)M_PI / 180.0f);

        predictedPos.x = localPos.x + std::cos(yawRad) * std::cos(pitchRad) * dist;
        predictedPos.y = localPos.y + std::sin(yawRad) * std::cos(pitchRad) * dist;
        predictedPos.z = localPos.z - std::sin(pitchRad) * dist;
    }

    Vector2 moveMouse = Camera::WorldToScreen(data.CameraCache.POV, predictedPos);
    
    float dist = Vector3::Distance(localPos, predictedPos);
   // LOG("[AimbotDebug] Target: 0x%llX | Bone: %d | Dist: %.1f | DropPitchOffset: %.4f | ZShift: %.1f\n", 
     //   bestTarget->Class, currentBoneId, dist, dropPitchOffset, zShift);
   // LOG("[AimbotDebug] Recoil: {%.2f, %.2f} | FinalScreenPos: {X:%.1f, Y:%.1f} | ViewCenter: {X:%.1f, Y:%.1f}\n", 
     //   data.Recoil.x, data.Recoil.y, moveMouse.x, moveMouse.y, viewCenter.x, viewCenter.y);

    if (moveMouse.x == 0.0f && moveMouse.y == 0.0f) {
        lastMoveX = 0;
        lastMoveY = 0;
        return;
    }

    MoveMouseTowards(moveMouse, viewCenter);
}
