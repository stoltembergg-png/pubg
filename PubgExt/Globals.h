#pragma once
#include "Engine.h"

// TODO: OFFSET is deprecated; new consumers should use Config/Offsets.h.

extern std::shared_ptr<Engine> EngineInstance;
extern std::string ProcessName;

// TODO: Replace these mutable process-wide globals (including Local/SDK below)
// with an owned runtime state/context and explicit synchronization.
#define IsAddrValid(ptr) (EngineInstance && EngineInstance->CachedBaseAddress && (uintptr_t)(ptr) >= 0x100000 && (uintptr_t)(ptr) < EngineInstance->CachedBaseAddress)



struct _Local
{
	int Teamid;
	int SpectatedCount;
	bool reset;
	Vector3 Recoil;
	std::map<int, std::string> KV;
}inline Local;

struct OFFSET {
	uint64_t GNames_offset = 0x10;
	uint64_t UWorld = 0x1225F938; 
	uint64_t Decrypt = 0x1079C028;
	uint64_t GNames = 0x124EF760;
	uint32_t ElementsPerChunk = 0x3E4C;

	uint32_t Offset = 0x0020;
	uint64_t NameIndexXor1 = 0x7360F24;
	uint32_t NameIndexOne = 0x0007;
	uint32_t NameIndexTwo = 0x0019;
	uint64_t NameIndexXor2 = 0xB621EC05;
	uint32_t NameIsROR = 0x0001; 

	uint32_t CurrentLevel = 0x800;
	uint32_t GameInstance = 0x3B0;
	uint32_t LocalPlayers = 0xF0;
	uint32_t Actors = 0x38;
	uint32_t ObjectID = 0x20;
	uint32_t PlayerController = 0x38;
	uint32_t AcknowledgedPawn = 0x4A8;
	uint32_t PlayerCameraManager = 0x4D0;
	uint32_t RootComponent = 0x308;
	uint32_t PlayerState = 0x418;
	uint32_t PlayerStatistics = 0xA10;
	uint32_t Mesh = 0x4A0;
	uint32_t AnimScriptInstance = 0xE30;
	uint32_t StaticMesh = 0xAE8; 
	uint32_t LastRenderTimeOnScreen = 0x75C;
	uint32_t Health = 0xA3C;
	uint32_t HeaFlag = 0x3B9;
	uint32_t Health1 = 0xA3C;
	uint32_t Health2 = 0xA38;
	uint32_t Health3 = 0xA24;
	uint32_t Health4 = 0xA10;
	uint32_t Health5 = 0xA25;
	uint32_t Health6 = 0xA20;
	uint64_t HealthXorKey0 = 0xCEC7A593;
	uint64_t HealthXorKey1 = 0x9B63B2A7;
	uint64_t HealthXorKey2 = 0xCAD3C3A5;
	uint64_t HealthXorKey3 = 0xA738484B;
	uint64_t HealthXorKey4 = 0xCC911D0A;
	uint64_t HealthXorKey5 = 0x23DDA185;
	uint64_t HealthXorKey6 = 0x09454BC8;
	uint64_t HealthXorKey7 = 0xA521BA21;
	uint64_t HealthXorKey8 = 0x0BA17A58;
	uint64_t HealthXorKey9 = 0xB0EFA787;
	uint64_t HealthXorKey10 = 0xE275B2BA;
	uint64_t HealthXorKey11 = 0x878ADBD0;
	uint64_t HealthXorKey12 = 0xBDCC62D5;
	uint64_t HealthXorKey13 = 0xA7934B07;
	uint64_t HealthXorKey14 = 0x4B099E38;
	uint64_t HealthXorKey15 = 0xEEDB2A7D;

	uint32_t GroggyHealth = 0x14B0; 
	uint32_t LastTeamNum = 0x2A98;
	uint32_t CharacterName = 0x1D70; 
	uint32_t SpectatedCount = 0x113C;
	uint32_t Eyes = 0x75C;

	uint32_t WorldToMap = 0xA04;
	uint32_t ComponentToWorld = 0x320;
	uint32_t ComponentLocation = 0x330;
	uint32_t ComponentVelocity = 0x23C;
	uint32_t CameraFov = 0xA2C;
	uint32_t CameraRot = 0xA10;
	uint32_t CameraPos = 0xA30;

	uint32_t ItemID = 0x244;
	uint32_t ItemTable = 0xB0;
	uint32_t DroppedItemGroup = 0x1C0;
	uint32_t DroppedItemGroup_UItem = 0x870;

	uint32_t WeaponProcessor = 0x968;
	uint32_t EquippedWeapons = 0x208;
	uint32_t CurrentWeaponIndex = 0x319;
	uint32_t WeaponTrajectoryData = 0x11A8;
	uint32_t TrajectoryGravityZ = 0x106C;
	uint32_t TrajectoryConfig = 0x108;
	uint32_t ControlRotation_CP = 0x064C; 
	uint32_t RecoilADSRotation_CP = 0x824; 
	uint32_t LeanLeftAlpha_CP = 0x694; 
	uint32_t LeanRightAlpha_CP = 0x698; 

	uint32_t AimOffsets = 0x1AB8;
	uint32_t ReplicatedMovement = 0xD0;
	uint32_t VehicleRiderComponent = 0x2050;
	uint32_t LastVehiclePawn = 0x270;

	uint32_t TimeTillExplosion = 0x824;
	uint32_t ExplodeState = 0x628;
}inline SDK;
