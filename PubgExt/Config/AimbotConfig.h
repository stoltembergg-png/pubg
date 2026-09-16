#pragma once
#include "pch.h"
class AimbotConfig
{
	std::string ConfigName;

public:
	AimbotConfig(const std::string& name)
	{
		ConfigName = name;
	}
	bool Enabled = false;
	int Hotkey = 1; // 0: Left Alt, 1: Right Click, 2: Shift, 3: Mouse 5, 4: Mouse 4
	int TargetBone = 0; // 0: Head, 1: Neck, 2: Chest, 3: Pelvis
	bool AutoBone = false;
	bool VisibilityCheck = true; 
	float FOV = 50.0f;
	float AR_RecoilValue = 100.0f;
	float AR_RecoilYaw = 70.0f;      // Horizontal recoil compensation (0-100%)
	float AR_SmoothValue = 3.0f;
	float SR_baseSmooth = 2.0f;
	int MaxDelta = 10;
	float Aggression = 1.2f;         // Movement speed multiplier (0.5-3.0)
	float DriverScale = 0.30f;       // Mouse-to-pixel conversion (0.10-0.50)
	bool LockOnTarget = true;        // Explicit lock-on mode

    json ToJson()
    {
        json j;
        j[ConfigName][LIT("Enabled")] = Enabled;
        j[ConfigName][LIT("Hotkey")] = Hotkey;
        j[ConfigName][LIT("TargetBone")] = TargetBone;
        j[ConfigName][LIT("AutoBone")] = AutoBone;
        j[ConfigName][LIT("VisibilityCheck")] = VisibilityCheck;
        j[ConfigName][LIT("FOV")] = FOV;
        j[ConfigName][LIT("AR_RecoilValue")] = AR_RecoilValue;
        j[ConfigName][LIT("AR_RecoilYaw")] = AR_RecoilYaw;
        j[ConfigName][LIT("AR_SmoothValue")] = AR_SmoothValue;
        j[ConfigName][LIT("SR_baseSmooth")] = SR_baseSmooth;
        j[ConfigName][LIT("MaxDelta")] = MaxDelta;
        j[ConfigName][LIT("Aggression")] = Aggression;
        j[ConfigName][LIT("DriverScale")] = DriverScale;
        j[ConfigName][LIT("LockOnTarget")] = LockOnTarget;

        return j;
    }
    void FromJson(const json& j)
    {
        if (!j.contains(ConfigName))
            return;
        if (j[ConfigName].contains(LIT("Enabled")))
            Enabled = j[ConfigName][LIT("Enabled")];
        if (j[ConfigName].contains(LIT("Hotkey")))
            Hotkey = j[ConfigName][LIT("Hotkey")];
        if (j[ConfigName].contains(LIT("TargetBone")))
            TargetBone = j[ConfigName][LIT("TargetBone")];
        if (j[ConfigName].contains(LIT("AutoBone")))
            AutoBone = j[ConfigName][LIT("AutoBone")];
        if (j[ConfigName].contains(LIT("VisibilityCheck")))
            VisibilityCheck = j[ConfigName][LIT("VisibilityCheck")];
        if (j[ConfigName].contains(LIT("FOV")))
            FOV = j[ConfigName][LIT("FOV")];
        if (j[ConfigName].contains(LIT("AR_RecoilValue")))
            AR_RecoilValue = j[ConfigName][LIT("AR_RecoilValue")];
        if (j[ConfigName].contains(LIT("AR_RecoilYaw")))
            AR_RecoilYaw = j[ConfigName][LIT("AR_RecoilYaw")];
        if (j[ConfigName].contains(LIT("AR_SmoothValue")))
            AR_SmoothValue = j[ConfigName][LIT("AR_SmoothValue")];
        if (j[ConfigName].contains(LIT("SR_baseSmooth")))
            SR_baseSmooth = j[ConfigName][LIT("SR_baseSmooth")];
        if (j[ConfigName].contains(LIT("MaxDelta")))
            MaxDelta = j[ConfigName][LIT("MaxDelta")];
        if (j[ConfigName].contains(LIT("Aggression")))
            Aggression = j[ConfigName][LIT("Aggression")];
        if (j[ConfigName].contains(LIT("DriverScale")))
            DriverScale = j[ConfigName][LIT("DriverScale")];
        if (j[ConfigName].contains(LIT("LockOnTarget")))
            LockOnTarget = j[ConfigName][LIT("LockOnTarget")];
    }
};
