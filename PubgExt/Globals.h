#pragma once
#include <mutex>

extern std::mutex GEngineMutex;
extern std::mutex GConfigMutex;

#include "Engine.h"
#include "Config/Offsets.h"

// TODO: OFFSET values are canonical in Config/Offsets.h.  This compatibility
// view keeps the existing SDK.<name> call sites source-compatible without
// maintaining a second copy of the offsets.

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

#define SDK_OFFSET_REF(name) const std::uint64_t& name = OFFSET::name;
struct SDKOffsets {
	SDK_OFFSET_REF(GNames_offset)
	SDK_OFFSET_REF(UWorld)
	SDK_OFFSET_REF(Decrypt)
	SDK_OFFSET_REF(GNames)
	SDK_OFFSET_REF(ElementsPerChunk)
	SDK_OFFSET_REF(Offset)
	SDK_OFFSET_REF(NameIndexXor1)
	SDK_OFFSET_REF(NameIndexOne)
	SDK_OFFSET_REF(NameIndexTwo)
	SDK_OFFSET_REF(NameIndexXor2)
	SDK_OFFSET_REF(NameIsROR)
	SDK_OFFSET_REF(CurrentLevel)
	SDK_OFFSET_REF(GameInstance)
	SDK_OFFSET_REF(LocalPlayers)
	SDK_OFFSET_REF(Actors)
	SDK_OFFSET_REF(ObjectID)
	SDK_OFFSET_REF(PlayerController)
	SDK_OFFSET_REF(AcknowledgedPawn)
	SDK_OFFSET_REF(PlayerCameraManager)
	SDK_OFFSET_REF(RootComponent)
	SDK_OFFSET_REF(PlayerState)
	SDK_OFFSET_REF(PlayerStatistics)
	SDK_OFFSET_REF(Mesh)
	SDK_OFFSET_REF(AnimScriptInstance)
	SDK_OFFSET_REF(StaticMesh)
	SDK_OFFSET_REF(LastRenderTimeOnScreen)
	SDK_OFFSET_REF(Health)
	SDK_OFFSET_REF(HeaFlag)
	SDK_OFFSET_REF(Health1)
	SDK_OFFSET_REF(Health2)
	SDK_OFFSET_REF(Health3)
	SDK_OFFSET_REF(Health4)
	SDK_OFFSET_REF(Health5)
	SDK_OFFSET_REF(Health6)
	SDK_OFFSET_REF(HealthXorKey0)
	SDK_OFFSET_REF(HealthXorKey1)
	SDK_OFFSET_REF(HealthXorKey2)
	SDK_OFFSET_REF(HealthXorKey3)
	SDK_OFFSET_REF(HealthXorKey4)
	SDK_OFFSET_REF(HealthXorKey5)
	SDK_OFFSET_REF(HealthXorKey6)
	SDK_OFFSET_REF(HealthXorKey7)
	SDK_OFFSET_REF(HealthXorKey8)
	SDK_OFFSET_REF(HealthXorKey9)
	SDK_OFFSET_REF(HealthXorKey10)
	SDK_OFFSET_REF(HealthXorKey11)
	SDK_OFFSET_REF(HealthXorKey12)
	SDK_OFFSET_REF(HealthXorKey13)
	SDK_OFFSET_REF(HealthXorKey14)
	SDK_OFFSET_REF(HealthXorKey15)
	SDK_OFFSET_REF(GroggyHealth)
	SDK_OFFSET_REF(LastTeamNum)
	SDK_OFFSET_REF(CharacterName)
	SDK_OFFSET_REF(SpectatedCount)
	SDK_OFFSET_REF(Eyes)
	SDK_OFFSET_REF(WorldToMap)
	SDK_OFFSET_REF(ComponentToWorld)
	SDK_OFFSET_REF(ComponentLocation)
	SDK_OFFSET_REF(ComponentVelocity)
	SDK_OFFSET_REF(CameraFov)
	SDK_OFFSET_REF(CameraRot)
	SDK_OFFSET_REF(CameraPos)
	SDK_OFFSET_REF(ItemID)
	SDK_OFFSET_REF(ItemTable)
	SDK_OFFSET_REF(DroppedItemGroup)
	SDK_OFFSET_REF(DroppedItemGroup_UItem)
	SDK_OFFSET_REF(WeaponProcessor)
	SDK_OFFSET_REF(EquippedWeapons)
	SDK_OFFSET_REF(CurrentWeaponIndex)
	SDK_OFFSET_REF(WeaponTrajectoryData)
	SDK_OFFSET_REF(TrajectoryGravityZ)
	SDK_OFFSET_REF(TrajectoryConfig)
	SDK_OFFSET_REF(ControlRotation_CP)
	SDK_OFFSET_REF(RecoilADSRotation_CP)
	SDK_OFFSET_REF(LeanLeftAlpha_CP)
	SDK_OFFSET_REF(LeanRightAlpha_CP)
	SDK_OFFSET_REF(AimOffsets)
	SDK_OFFSET_REF(ReplicatedMovement)
	SDK_OFFSET_REF(VehicleRiderComponent)
	SDK_OFFSET_REF(LastVehiclePawn)
	SDK_OFFSET_REF(TimeTillExplosion)
	SDK_OFFSET_REF(ExplodeState)
};
#undef SDK_OFFSET_REF

inline SDKOffsets SDK;
