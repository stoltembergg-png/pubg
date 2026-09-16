#pragma once
#include "ActorEntity.h"
#include "EngineStructs.h"
#include <unordered_map>
class Engine
{

private:
	uint64_t OwningActor;
	int MaxPacket;
	int localTempId;
	std::string oldMapName;
	CameraCacheEntry CameraEntry; // ScriptStruct Engine.CameraCacheEntry
	MinimalViewInfo CameraViewInfo; // ScriptStruct Engine.MinimalViewInfo
	
	std::chrono::steady_clock::time_point LastActorRefreshTimestamp;
	std::chrono::steady_clock::time_point LastWeaponUpdateTimestamp;
	bool bIsMapInitialized = false;
	bool bIsDriving = false;

	float CurrentBulletSpeed = 0.0f;
	float CurrentGravity = -9.8f;
	float CurrentSimulationSubstepTime = 0.016f;
	float CurrentVDragCoefficient = 1.0f;

public:
	uintptr_t CachedBaseAddress = 0;
	uint64_t LocalCharacterPawn = 0;
	uint64_t CachedDecryptKey = 0;
	bool FrameTranslateError = false;

	template<typename T>
	T SafeRead(uintptr_t addr, const char* name) {
		if (FrameTranslateError) return T{};
		T val{};
		if (!TargetProcess.Read(addr, &val, sizeof(T), name)) {
			FrameTranslateError = true;
			return T{};
		}
		return val;
	}

	float GetCurrentBulletSpeed() { return CurrentBulletSpeed; }
	float GetCurrentGravity() { return CurrentGravity; }

	uint64_t UWorld, CurrentLevel, GameInstance, LocalPlayers, PlayerController, AcknowledgedPawn, PlayerCameraManager, GNames;
	std::vector<std::shared_ptr<ActorEntity>> Actors;
	std::vector<std::shared_ptr<ActorEntity>> GrenadeActors;  // Grenade/Molotov ESP
	std::unordered_map<uint64_t, std::shared_ptr<ActorEntity>> ActorCache;
	Engine();
	~Engine();
	inline bool InitDecrypt(uint64_t offset);
	void Cache();
	void UpdatePlayers();
	void UpdateGrenades();  // Read projectile-specific offsets for grenade ESP
	void UpdateWeaponPhysics();
	Vector3 GetPrediction(const std::shared_ptr<ActorEntity>& target, const Vector3& targetPos, float& dropPitchOffset);
	std::vector<std::shared_ptr<ActorEntity>> GetActors();
	CameraCacheEntry GetCameraCache();
	void RefreshViewMatrix();
	uint64_t GetActorSize();
	uintptr_t xe_decrypt(const uintptr_t encrypted);
	void HealthDecipher(uint8_t* a1, uint32_t a3);
	float DecryptHealth(uint8_t* buffer);
	DWORD DecryptCIndex(DWORD Encrypted);
	void GetGNames();
	int IsPlayer(std::string entity_name);
	int IsProjectile(const std::string& entity_name);  // Returns explosion radius (UU) or 0
	std::string GetNamesFromList(DWORD ID);
	std::string GetNames(DWORD ID);
};