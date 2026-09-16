#include "pch.h"
#include "Engine.h"
#include "ActorEntity.h"
#include "Globals.h"
#include "../Config/ConfigUtilities.h"
#include "../driver/syscall.h"
#include <codecvt>
#include <locale>
#include <algorithm>
Engine::Engine()
{
	CachedBaseAddress = TargetProcess.GetBaseAddress(ProcessName);

	if (InitDecrypt(SDK.Decrypt)) {
		GetGNames();
	}
}
typedef int64_t(__fastcall* DecryptFunctoin)(uintptr_t key, uintptr_t argv);
inline DecryptFunctoin fnDecryptFunctoin = NULL;
inline uint64_t Tmpadd;
inline bool g_IsLeaInstruction = false;

Engine::~Engine()
{
	if (fnDecryptFunctoin) {
		PVOID freeAddr = (PVOID)fnDecryptFunctoin;
		SIZE_T freeSize = 0;
		SyscallNtFreeVirtualMemory((HANDLE)-1, &freeAddr, &freeSize, MEM_RELEASE);
		fnDecryptFunctoin = NULL;
	}
}


inline bool Engine::InitDecrypt(uint64_t offset)
{
	uint64_t base = CachedBaseAddress;
	uintptr_t DecryptAddr = base + offset;
	
	
	uintptr_t DecryptPtr = 0;
	bool read_success = TargetProcess.Read(DecryptAddr, &DecryptPtr, sizeof(uintptr_t), "DecryptPtr_Init");
	
	int wait_count = 0;
	while (!DecryptPtr || !read_success)
	{
		if (wait_count % 5 == 0) {
			if (!read_success) {
				printf("[-] [CRITICAL] Driver failed the read command entirely at offset 0x%llx\n", offset);
			} else {
				printf("[-] Waiting for DecryptPtr... (Read offset 0x%llx returned 0)\n", offset);
			}
			printf("    -> Potential CR3/DTB isolation. Try restarting game/driver.\n");
		}
		
		if (wait_count > 20) {
			printf("[!] Timeout waiting for DecryptPtr. Continuing for diagnostics...\n");
			break; 
		}

		wait_count++;
		Sleep(1000);
		read_success = TargetProcess.Read(DecryptAddr, &DecryptPtr, sizeof(uintptr_t), "DecryptPtr_Retry");
	}
	
	if (DecryptPtr) {
	} else {
		return false;
	}
	
	unsigned char ShellcodeBuff[1024] = { NULL };
	ShellcodeBuff[0] = 0x90; // nop
	ShellcodeBuff[1] = 0x90; // nop
		if (!TargetProcess.Read(DecryptPtr, &ShellcodeBuff[2], sizeof(ShellcodeBuff) - 2, "DecryptShellcode")) {
		return false;
	}
	int32_t Tmp1Add = *(int32_t*)&ShellcodeBuff[5];
	Tmpadd = Tmp1Add + DecryptPtr + 7;
	if (ShellcodeBuff[3] == 0x8D) {
		g_IsLeaInstruction = true;
	} else if (ShellcodeBuff[3] == 0x8B) {
		g_IsLeaInstruction = false;
	} else {
		g_IsLeaInstruction = false;
	}

	ShellcodeBuff[2] = 0x48;
	ShellcodeBuff[3] = 0x8B;
	ShellcodeBuff[4] = 0xC1;
	ShellcodeBuff[5] = 0x90;
	ShellcodeBuff[6] = 0x90;
	ShellcodeBuff[7] = 0x90;
	ShellcodeBuff[8] = 0x90;
	
	PVOID allocAddr = NULL;
	SIZE_T allocSize = sizeof(ShellcodeBuff) + 4;
	SyscallNtAllocateVirtualMemory((HANDLE)-1, &allocAddr, 0,
		&allocSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	fnDecryptFunctoin = (DecryptFunctoin)allocAddr;
	if (!fnDecryptFunctoin) return false;
	RtlCopyMemory((LPVOID)fnDecryptFunctoin, (LPVOID)ShellcodeBuff, sizeof(ShellcodeBuff));
	return 1;
}
void Engine::UpdateWeaponPhysics()
{
	if (!AcknowledgedPawn || bIsDriving) return;

	auto now = std::chrono::steady_clock::now();
	if (std::chrono::duration_cast<std::chrono::milliseconds>(now - LastWeaponUpdateTimestamp).count() < 200) return;
	LastWeaponUpdateTimestamp = now;

	uintptr_t weaponProcessor = SafeRead<uintptr_t>(AcknowledgedPawn + SDK.WeaponProcessor, "WeaponProcessor");
	if (!weaponProcessor || weaponProcessor < 0x10000000000ULL || (weaponProcessor & 7) != 0) return;

	uint8_t weaponIndex = 0;
	uintptr_t equippedWeaponsPtr = 0;
	{
		DriverInterfaceV3::BatchReadEntry wpBatch[2];
		wpBatch[0] = { weaponProcessor + SDK.CurrentWeaponIndex, sizeof(uint8_t), &weaponIndex };
		wpBatch[1] = { weaponProcessor + SDK.EquippedWeapons, sizeof(uintptr_t), &equippedWeaponsPtr };
		TargetProcess.BatchRead(wpBatch, 2);
	}
	if (!IsAddrValid(equippedWeaponsPtr) || equippedWeaponsPtr < 0x10000000000ULL || weaponIndex > 4) return;

	uintptr_t currentWeapon = SafeRead<uintptr_t>(equippedWeaponsPtr + (weaponIndex * 8), "CurrentWeapon");
	if (!IsAddrValid(currentWeapon)) return;

	static uintptr_t lastCurrentWeapon = 0;
	if (currentWeapon == lastCurrentWeapon) return;
	lastCurrentWeapon = currentWeapon;

	LOG("[WeaponPhysics] New Weapon Selected: 0x%llX | Index: %u\n", currentWeapon, weaponIndex);


	if (weaponIndex >= 1 && weaponIndex <= 3) {
		uintptr_t actorPtr = SafeRead<uintptr_t>(currentWeapon + SDK.WeaponTrajectoryData, "Actor_TrajectoryPtr");
		uintptr_t procPtr = SafeRead<uintptr_t>(weaponProcessor + SDK.WeaponTrajectoryData, "Processor_TrajectoryPtr");

		auto IsHeapValid = [](uintptr_t p) { return p > 0x10000000000ULL && (p & 7) == 0; };
		uintptr_t validTrajectoryData = IsHeapValid(actorPtr) ? actorPtr : 
										(IsHeapValid(procPtr) ? procPtr : 0);

		if (IsAddrValid(validTrajectoryData)) {
			uintptr_t trajectoryConfigAddr = validTrajectoryData + SDK.TrajectoryConfig;

				float bulletSpeed = 0.0f, substepTime = 0.0f, vDrag = 0.0f;
			{
				DriverInterfaceV3::BatchReadEntry trajBatch[3];
				trajBatch[0] = { trajectoryConfigAddr + 0x0, sizeof(float), &bulletSpeed };
				trajBatch[1] = { trajectoryConfigAddr + 0x40, sizeof(float), &substepTime };
				trajBatch[2] = { trajectoryConfigAddr + 0x44, sizeof(float), &vDrag };
				TargetProcess.BatchRead(trajBatch, 3);
			}

			CurrentBulletSpeed = bulletSpeed;
			CurrentSimulationSubstepTime = (substepTime > 0.0f) ? substepTime : 0.016f;
			CurrentVDragCoefficient = vDrag;
			CurrentGravity = -9.8f;

			LOG("[WeaponPhysics] InitialSpeed: %f | Gravity: %f | Drag: %f\n", CurrentBulletSpeed, CurrentGravity, CurrentVDragCoefficient);
		}
		else {
			CurrentGravity = -9.8f;
			LOG("[WeaponPhysics] No trajectory data, using default Gravity: %f\n", CurrentGravity);
		}
	} else {
		CurrentBulletSpeed = 0.0f;
		CurrentSimulationSubstepTime = 0.016f;
		CurrentVDragCoefficient = 0.0f;
		CurrentGravity = 0.0f;
		LOG("[WeaponPhysics] Resetting ballistics for non-firearm index: %u\n", weaponIndex);
	}
}

Vector3 Engine::GetPrediction(const std::shared_ptr<ActorEntity>& target, const Vector3& targetPos, float& dropPitchOffset)
{
	dropPitchOffset = 0.0f;
	if (CurrentBulletSpeed <= 100.0f) return targetPos;

	Vector3 localPos = Vector3(CameraEntry.POV.Location.X, CameraEntry.POV.Location.Y, CameraEntry.POV.Location.Z);
	float distanceMax = Vector3::Distance(localPos, targetPos);
	
	float internalGravityZ = abs(CurrentGravity); 
	float substepTime = CurrentSimulationSubstepTime > 0.0f ? CurrentSimulationSubstepTime : 0.016f; 
	float dragCoef = CurrentVDragCoefficient > 0.0f ? CurrentVDragCoefficient : 1.0f;

	float flyTime = 0.0f;
	float drop = 0.0f;
	float travelDistance = 0.0f;
	float ballisticSubstepDropVelocity = 0.0f;

	float zeroingDistance = 100.0f * 100.0f; 
	float dropOffset = 0.0f;

	for (int i = 0; i < 500; i++)
	{
		float v = CurrentBulletSpeed * 100.0f;
		
		drop += ballisticSubstepDropVelocity * substepTime;
		travelDistance += substepTime * v;
		flyTime += substepTime;

		if (travelDistance >= zeroingDistance && dropOffset == 0.0f) {
			dropOffset = drop;
		}

		if (travelDistance >= distanceMax)
		{
			flyTime -= (travelDistance - distanceMax) / v;
			drop -= ((travelDistance - distanceMax) / v) * ballisticSubstepDropVelocity;
			break;
		}
		
		ballisticSubstepDropVelocity += substepTime * internalGravityZ * 100.0f * dragCoef;
	}

	float scopZ = 5.0f; 
	const float RAD_TO_DEG = 57.2957795f; 
	dropPitchOffset = (((abs(scopZ) + drop) / distanceMax) * RAD_TO_DEG) -
					 (((abs(scopZ) + dropOffset) / zeroingDistance) * RAD_TO_DEG);

	if (distanceMax > zeroingDistance) {
		if (distanceMax >= 90000.0f)      dropPitchOffset -= 0.0310f;
		else if (distanceMax >= 80000.0f) dropPitchOffset -= 0.0285f;
		else if (distanceMax >= 70000.0f) dropPitchOffset -= 0.0265f;
		else if (distanceMax >= 60000.0f) dropPitchOffset -= 0.0245f;
		else if (distanceMax >= 50000.0f) dropPitchOffset -= 0.0222f;
		else if (distanceMax >= 40000.0f) dropPitchOffset -= 0.0200f;
		else if (distanceMax >= 30000.0f) dropPitchOffset -= 0.0175f;
		else if (distanceMax >= 20000.0f) dropPitchOffset -= 0.0115f;
		else                             dropPitchOffset -= 0.0150f;
	}

	Vector3 predictionPos = targetPos + (target->Velocity * flyTime);
	
	return predictionPos;
}

uintptr_t Engine::xe_decrypt(const uintptr_t encrypted)
{
	if (!encrypted) return 0;
	
	uint64_t key = g_IsLeaInstruction ? Tmpadd : CachedDecryptKey;
	
	if (!key) return 0;
	
	uintptr_t data = fnDecryptFunctoin(key, encrypted); 
	return data;
}

DWORD Engine::DecryptCIndex(DWORD value)
{
	return ((((value ^ 0x7360F24) << 25) | (((value ^ 0x7360F24) >> 7) & 0x1FF0000)) ^ _rotr(value ^ 0x7360F24, 23) ^ 0xB621EC05);
}

void Engine::HealthDecipher(uint8_t* a1, uint32_t a3)
{
	uint32_t XorKeys[16];
	XorKeys[0] = (uint32_t)SDK.HealthXorKey0;
	XorKeys[1] = (uint32_t)SDK.HealthXorKey1;
	XorKeys[2] = (uint32_t)SDK.HealthXorKey2;
	XorKeys[3] = (uint32_t)SDK.HealthXorKey3;
	XorKeys[4] = (uint32_t)SDK.HealthXorKey4;
	XorKeys[5] = (uint32_t)SDK.HealthXorKey5;
	XorKeys[6] = (uint32_t)SDK.HealthXorKey6;
	XorKeys[7] = (uint32_t)SDK.HealthXorKey7;
	XorKeys[8] = (uint32_t)SDK.HealthXorKey8;
	XorKeys[9] = (uint32_t)SDK.HealthXorKey9;
	XorKeys[10] = (uint32_t)SDK.HealthXorKey10;
	XorKeys[11] = (uint32_t)SDK.HealthXorKey11;
	XorKeys[12] = (uint32_t)SDK.HealthXorKey12;
	XorKeys[13] = (uint32_t)SDK.HealthXorKey13;
	XorKeys[14] = (uint32_t)SDK.HealthXorKey14;
	XorKeys[15] = (uint32_t)SDK.HealthXorKey15;

	for (int i = 0; i < 4; i++)
	{
		uint8_t v1 = (i + a3) & 0x3F;
		a1[i] ^= *((uint8_t*)XorKeys + v1);
	}
}

float Engine::DecryptHealth(uint8_t* buffer)
{
	uint32_t HeaFlag_Local = SDK.HeaFlag - 0x3B0;
	uint32_t Health1_Local = SDK.Health1 - 0x3B0;
	uint32_t Health2_Local = SDK.Health2 - 0x3B0;
	uint32_t Health3_Local = SDK.Health3 - 0x3B0;
	uint32_t Health4_Local = SDK.Health4 - 0x3B0;
	uint32_t Health5_Local = SDK.Health5 - 0x3B0;
	uint32_t Health6_Local = SDK.Health6 - 0x3B0;

	if (buffer[HeaFlag_Local] != 3 && *(uint32_t*)&buffer[Health1_Local])
	{
		uint8_t v2 = buffer[Health3_Local];
		if (v2 + Health4_Local + 4 > 0x760) return 0.0f; 

		float v5 = *(float*)&buffer[v2 + Health4_Local];

		if (buffer[Health5_Local] != 0)
		{
			HealthDecipher((uint8_t*)&v5, *(uint32_t*)&buffer[Health6_Local]);
			return v5;
		}
		return v5; 
	}
	
	return *(float*)&buffer[Health2_Local];
}
std::wstring string_to_wstring(const std::string& str) {
	std::wstring ws(str.begin(), str.end());
	return ws;
}
bool contains(const std::string& haystack, const std::string& needle) {
	return haystack.find(needle) != std::string::npos;
}
void Engine::Cache()
{
	uintptr_t base_data = CachedBaseAddress;
	if (!base_data) return;

	FrameTranslateError = false;


	uint64_t localDecryptKey = 0;
	uintptr_t localRawUWorld = 0;
	
	DriverInterfaceV3::BatchReadEntry topBatch[2];
	int topCount = 0;
	if (!g_IsLeaInstruction) {
	    topBatch[topCount++] = { Tmpadd, 8, &localDecryptKey };
	}
	topBatch[topCount++] = { base_data + SDK.UWorld, 8, &localRawUWorld };
	
	if (topCount > 0) {
		if (!TargetProcess.BatchRead(topBatch, topCount)) {
			FrameTranslateError = true;
		}
	}

	if (!g_IsLeaInstruction) {
		CachedDecryptKey = localDecryptKey;
		if (!CachedDecryptKey) return;
	}

	uintptr_t rawUWorld = localRawUWorld;
	if (!rawUWorld) return;
	
	static uint64_t last_UWorld = 0;
	if (rawUWorld != last_UWorld || !UWorld) {
		UWorld = xe_decrypt(rawUWorld);
		last_UWorld = rawUWorld;
		if (!IsAddrValid(UWorld)) UWorld = 0;
		CurrentLevel = 0;
		GameInstance = 0;
		LocalPlayers = 0;
		PlayerController = 0;
		PlayerCameraManager = 0;
		AcknowledgedPawn = 0;
		OwningActor = 0;
		ActorCache.clear(); 
		Local.KV.clear();
		bIsMapInitialized = false;
	}
	if (!IsAddrValid(UWorld)) {
		return;
	}

	auto now = std::chrono::steady_clock::now();
	static std::chrono::steady_clock::time_point lastUWorldIdCheckTime;
	static uint32_t cachedRawUWorldId = 0;
	if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUWorldIdCheckTime).count() > 1000 || cachedRawUWorldId == 0) {
		cachedRawUWorldId = SafeRead<uint32_t>(UWorld + SDK.ObjectID, "UWorld_ObjectID");
		lastUWorldIdCheckTime = now;
	}
	uint32_t rawUworldId = cachedRawUWorldId;
	if (FrameTranslateError) {
		static std::chrono::steady_clock::time_point lastFault1;
		if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFault1).count() > 2000) {
			LOG("[-] FAULT on UWorld_ObjectID! UWorld: 0x%p | rawUWorld: 0x%p | DecryptKey: 0x%p\n", UWorld, (void*)rawUWorld, (void*)CachedDecryptKey);
			lastFault1 = now;
		}
		return;
	}

	auto UWorldID = DecryptCIndex(rawUworldId);
	std::string MapName = GetNamesFromList(UWorldID);
	
	if (MapName.empty() || MapName == "Unknown" || MapName == "UNKOWNNAME") {
		MapName = GetNames(UWorldID);
		if (!MapName.empty() && MapName != "UNKOWNNAME") {
			Local.KV[UWorldID] = MapName;
		}
	}

	if (MapName.find("Lobby") != std::string::npos || MapName.empty() || MapName.find("Unknown") != std::string::npos) {
		static bool lobbyLogged = false;
		if (!lobbyLogged) {
			LOG("[!] WARNING: Map check triggered Lobby/Reset state. MapName: '%s' | UWorldID: %u | UWorld: 0x%p\n", MapName.c_str(), UWorldID, (void*)UWorld);
			lobbyLogged = true;
		}
		oldMapName = "";
		MaxPacket = 0;
		PlayerCameraManager = 0;    
		AcknowledgedPawn = 0;
		bIsMapInitialized = false;   
		return;
	}

	if (MapName != oldMapName) {
		MaxPacket = 0;
		LOG("[+] Map Changed: %s\n", MapName.c_str());
		bIsMapInitialized = false;
	}
	oldMapName = MapName;

	if (!bIsMapInitialized) {
		CurrentLevel = xe_decrypt(SafeRead<uint64_t>(UWorld + SDK.CurrentLevel, "CurrentLevel"));
		if (!IsAddrValid(CurrentLevel) || FrameTranslateError) return;

		GameInstance = xe_decrypt(SafeRead<uint64_t>(UWorld + SDK.GameInstance, "GameInstance"));
		if (!IsAddrValid(GameInstance) || FrameTranslateError) return;

		LocalPlayers = xe_decrypt(SafeRead<uint64_t>(SafeRead<uint64_t>(GameInstance + SDK.LocalPlayers, "LocalPlayers_Ptr"), "LocalPlayers_Val"));
		if (!IsAddrValid(LocalPlayers) || FrameTranslateError) return;

		PlayerController = xe_decrypt(SafeRead<uint64_t>(LocalPlayers + SDK.PlayerController, "PlayerController"));
		if (!IsAddrValid(PlayerController) || FrameTranslateError) return;

		OwningActor = xe_decrypt(SafeRead<uint64_t>(CurrentLevel + SDK.Actors, "ActorsArray"));
		if (!IsAddrValid(OwningActor) || FrameTranslateError) return;

		bIsMapInitialized = true;
	}

	static uint64_t lastRawPawn = 0;
	uint64_t rawPawn = 0;
	uint64_t rawCameraManager = 0;
	int spectatedCount = 0;
	int maxPacket = 0;
	uint64_t actorsArray = 0;

	DriverInterfaceV3::BatchReadEntry cacheBatch[6];
	int bCount = 0;
	
	cacheBatch[bCount++] = { PlayerController + SDK.PlayerCameraManager, 8, &rawCameraManager };
	cacheBatch[bCount++] = { PlayerController + SDK.AcknowledgedPawn, 8, &rawPawn };
	if (AcknowledgedPawn) cacheBatch[bCount++] = { AcknowledgedPawn + SDK.SpectatedCount, 4, &spectatedCount };
	cacheBatch[bCount++] = { OwningActor, 8, &actorsArray };
	cacheBatch[bCount++] = { OwningActor + 8, 4, &maxPacket };

	if (bCount > 0) TargetProcess.BatchRead(cacheBatch, bCount);

	PlayerCameraManager = rawCameraManager;
	if (!IsAddrValid(PlayerCameraManager)) {
		return;
	}

	if (rawPawn != lastRawPawn || !AcknowledgedPawn) {
		AcknowledgedPawn = xe_decrypt(rawPawn);
		if (!IsAddrValid(AcknowledgedPawn)) AcknowledgedPawn = 0;
		lastRawPawn = rawPawn;
	}

	Local.SpectatedCount = spectatedCount;
	MaxPacket = maxPacket;
	uint64_t ActorsArray = actorsArray;

	if (MaxPacket <= 0 || MaxPacket > 20000 || !IsAddrValid(ActorsArray)) return;

	if (std::chrono::duration_cast<std::chrono::milliseconds>(now - LastActorRefreshTimestamp).count() > 1000 || Actors.empty()) {
		std::vector<uint64_t> entitylist;
		entitylist.resize(MaxPacket);
		std::unique_ptr<uint64_t[]> object_raw_ptr = std::make_unique<uint64_t[]>(MaxPacket);
		
		if (!TargetProcess.Read(ActorsArray, object_raw_ptr.get(), MaxPacket * sizeof(uint64_t), "ActorEntityBuffer")) {
			return;
		}

		for (size_t i = 0; i < MaxPacket; i++) {
			entitylist[i] = object_raw_ptr[i];
		}

		std::list<std::shared_ptr<ActorEntity>> actors;
		std::vector<std::shared_ptr<ActorEntity>> grenadeList;
		std::unordered_map<uint64_t, std::shared_ptr<ActorEntity>> newCache;

		for (size_t i = 0; i < entitylist.size(); i += MAX_BATCH_ENTRIES) {
			size_t chunkSize = (entitylist.size() - i > MAX_BATCH_ENTRIES) ? MAX_BATCH_ENTRIES : (entitylist.size() - i);
			DriverInterfaceV3::BatchReadEntry batchEntries[MAX_BATCH_ENTRIES];
			uint32_t batchObjectIDs[MAX_BATCH_ENTRIES];
			int activeInBatch = 0;

			for (size_t j = 0; j < chunkSize; j++) {
				uintptr_t actorAddr = entitylist[i + j];
				if (actorAddr < 0x1000000) {
					batchEntries[j].address = 0;
					batchEntries[j].size = 0;
					batchEntries[j].buffer = nullptr;
					continue;
				}
				batchEntries[j].address = actorAddr + SDK.ObjectID;
				batchEntries[j].size = sizeof(uint32_t);
				batchEntries[j].buffer = &batchObjectIDs[j];
				activeInBatch++;
			}

			if (activeInBatch == 0) continue;

			if (TargetProcess.BatchRead(batchEntries, chunkSize)) {
				for (size_t j = 0; j < chunkSize; j++) {
					uintptr_t actorAddr = entitylist[i + j];
					if (actorAddr < 0x1000000) continue;

					uint32_t encryptedID = batchObjectIDs[j];
					if (encryptedID == 0) continue;

					int id = DecryptCIndex(encryptedID);
					std::string name = GetNamesFromList(id);
					if (name == "") {
						name = GetNames(id);
						Local.KV[id] = name;
					}

					int playerType = IsPlayer(name);
					if (playerType == 0) {
						int explosionRadius = IsProjectile(name);
						if (explosionRadius > 0) {
							std::shared_ptr<ActorEntity> grenadeEntity;
							auto git = ActorCache.find(actorAddr);
							if (git != ActorCache.end()) {
								grenadeEntity = git->second;
							} else {
								grenadeEntity = std::make_shared<ActorEntity>(actorAddr);
								grenadeEntity->id = encryptedID;
								grenadeEntity->PlayerType = 3; 
								grenadeEntity->objName = name;
								grenadeEntity->ExplosionRadius = (float)explosionRadius;
							}
							newCache[actorAddr] = grenadeEntity;
							grenadeList.push_back(grenadeEntity);
						}
						if (actorAddr == AcknowledgedPawn) {
							this->bIsDriving = true;
						}
						continue; 
					}
					if (actorAddr == AcknowledgedPawn) {
						this->bIsDriving = false;
						this->LocalCharacterPawn = actorAddr; 
					}

					std::shared_ptr<ActorEntity> entity;
					auto it = ActorCache.find(actorAddr);
					if (it != ActorCache.end()) {
						entity = it->second;
					} else {
						entity = std::make_shared<ActorEntity>(actorAddr);
						entity->id = encryptedID;
						entity->PlayerType = playerType;
						entity->objName = name;
						entity->TempId = id;
						GetIndex(entity->index, entity->objName);
					}

					newCache[actorAddr] = entity;
					actors.push_back(entity);
				}
			}
		}
		
		ActorCache = std::move(newCache);
		std::vector<std::shared_ptr<ActorEntity>> playerlist;
		for (std::shared_ptr<ActorEntity> entity : actors) {
			if (entity->RootComponent) {
				entity->isCheck = true;
			}
			playerlist.push_back(entity);
		}
		Actors = playerlist;
		GrenadeActors = std::move(grenadeList);
		LastActorRefreshTimestamp = now;
	}

}

int Engine::IsPlayer(std::string entity_name) {
	if (entity_name.empty() || entity_name == "" || entity_name == "Unknown") { return 0; }
	if (entity_name == "AIPawn_Base_Female_C") { return 1; }
	if (entity_name == "AIPawn_Base_Male_C") { return 1; }
	if (entity_name == "UltAIPawn_Base_C") { return 1; }
	if (entity_name == "UltAIPawn_Base_Female_C") { return 1; }
	if (entity_name == "UltAIPawn_Base_Male_C") {return 1; }
	if (entity_name == "UltAIPawn_Base_Pillar_C") {return 1; }
	if (entity_name == "UltAIPawn_Base_Female_Pillar_C") {return 1; }
	if (entity_name == "UltAIPawn_Base_Male_Pillar_C") {return 1; }
	if (entity_name == "BP_MarketAI_Pawn_C") {return 1; }
	if (entity_name == "RegistedPlayer") {return 2; }
	if (entity_name == "PlayerMale_A") {return 2; }
	if (entity_name == "PlayerMale_A_C") {return 2; }
	if (entity_name == "PlayerFemale_A") {return 2; }
	if (entity_name == "PlayerFemale_A_C") {return 2; }
	return 0;
}
int Engine::IsProjectile(const std::string& name) {
	if (name == "ProjGrenade_C")      return 500;  
	if (name == "ProjMolotov_C")      return 200;  
	return 0;
}

void Engine::UpdateGrenades() {
	if (GrenadeActors.empty()) return;

	struct GrenadeReadInfo {
		std::shared_ptr<ActorEntity> actor;
		float timeTillExplosion = 0.0f;
		uint32_t explodeState = 0;
		UEVector pos = {};
		uint64_t encryptedRoot = 0;
	};

	std::vector<GrenadeReadInfo> infos(GrenadeActors.size());

	std::vector<DriverInterfaceV3::BatchReadEntry> batch1;
	for (size_t i = 0; i < GrenadeActors.size(); i++) {
		infos[i].actor = GrenadeActors[i];
		auto addr = GrenadeActors[i]->Class;
		if (!addr) continue;

		batch1.push_back({ addr + SDK.RootComponent, sizeof(uint64_t), &infos[i].encryptedRoot });
		batch1.push_back({ addr + SDK.TimeTillExplosion, sizeof(float), &infos[i].timeTillExplosion });
		batch1.push_back({ addr + SDK.ExplodeState, sizeof(uint32_t), &infos[i].explodeState });
	}

	for (size_t i = 0; i < batch1.size(); i += MAX_BATCH_ENTRIES) {
		size_t chunk = std::min((size_t)MAX_BATCH_ENTRIES, batch1.size() - i);
		TargetProcess.BatchRead(&batch1[i], chunk);
	}

	std::vector<DriverInterfaceV3::BatchReadEntry> batch2;
	for (size_t i = 0; i < infos.size(); i++) {
		if (!infos[i].encryptedRoot) continue;
		auto root = xe_decrypt(infos[i].encryptedRoot);
		if (!IsAddrValid(root)) continue;
		infos[i].actor->RootComponent = root;
		batch2.push_back({ root + SDK.ComponentLocation, sizeof(UEVector), &infos[i].pos });
	}

	for (size_t i = 0; i < batch2.size(); i += MAX_BATCH_ENTRIES) {
		size_t chunk = std::min((size_t)MAX_BATCH_ENTRIES, batch2.size() - i);
		TargetProcess.BatchRead(&batch2[i], chunk);
	}

	for (size_t i = 0; i < infos.size(); i++) {
		auto& a = infos[i].actor;
		a->TimeTillExplosion = infos[i].timeTillExplosion;
		a->ExplodeState = infos[i].explodeState;
		if (infos[i].pos.X != 0.0 || infos[i].pos.Y != 0.0) {
			a->UEPosition = infos[i].pos;
			a->LastGoodPosition = infos[i].pos;
		}
	}
}
void Engine::GetGNames()
{
	GNames = xe_decrypt(SafeRead<uint64_t>(CachedBaseAddress + SDK.GNames, "GNamesArray"));
	GNames = xe_decrypt(SafeRead<uint64_t>(GNames + 0x10, "GNamesPtr"));
}
struct StringA
{
	char buffer[1024];
};
std::string Engine::GetNamesFromList(DWORD ID)
{
	return Local.KV[ID];
}
std::string Engine::GetNames(DWORD ID)
{
	std::string emp = "UNKOWNNAME";
	if (ID <= 0) return emp;
	uint32_t IdDiv = ID / SDK.ElementsPerChunk;
	uint32_t Idtemp = ID % SDK.ElementsPerChunk;
	uint64_t Serial = TargetProcess.Read<uint64_t>(GNames + IdDiv * 0x8, "GNamesChunk");

	if (!Serial || Serial < 0x100000)
		return emp;
	uint64_t pName = TargetProcess.Read<uint64_t>(Serial + 0x8 * Idtemp, "GNamesSerial");

	if (!pName || pName < 0x100000)
		return emp;

	StringA names = TargetProcess.Read<StringA>(pName + 0x10, "GNamesStringA");
	names.buffer[1023] = '\0'; 


	char te[256];
	memset(&te, 0, 256);
	if (memcmp(names.buffer, te, 256) == 0)
		return emp;
	std::string str(names.buffer);
	return str;
}
void Engine::UpdatePlayers()
{
	UpdateWeaponPhysics();
	if (Actors.empty()) return;


	struct ActorReadInfo {
		std::shared_ptr<ActorEntity> actor;
		int lastTeamNum = 0;
		UEVector pos = {};
		Vector3 velocity = Vector3(0.f, 0.f, 0.f);
		uint8_t meshBulk[0xAF0] = {0};
		uint8_t healthBulk[0x760] = { 0 };
		uint8_t fullBoneBuffer[190 * sizeof(FTransform)] = {0}; 
		uint64_t freshMesh = 0; 
		
		bool successPos = false;
		bool successMesh = false;
	};

	std::vector<ActorReadInfo> infos(Actors.size());
	for (size_t i = 0; i < Actors.size(); i++) infos[i].actor = Actors[i];

	std::vector<DriverInterfaceV3::BatchReadEntry> batch1;
	Vector3 localRecoil = {0.f, 0.f, 0.f};
	if (AcknowledgedPawn && !bIsDriving) {
		batch1.push_back({ AcknowledgedPawn + SDK.RecoilADSRotation_CP, sizeof(Vector3), &localRecoil });
	}

	for (size_t i = 0; i < infos.size(); i++) {
		if (!infos[i].actor->Class || !infos[i].actor->RootComponent || !infos[i].actor->isCheck) continue;
		batch1.push_back({ infos[i].actor->Class + SDK.LastTeamNum, sizeof(int), &infos[i].lastTeamNum });
		batch1.push_back({ infos[i].actor->RootComponent + infos[i].actor->RelativeLocation, sizeof(UEVector), &infos[i].pos });
		batch1.push_back({ infos[i].actor->RootComponent + SDK.ComponentVelocity, sizeof(Vector3), &infos[i].velocity });
		batch1.push_back({ infos[i].actor->Class + 0x3B0, 0x760, infos[i].healthBulk });
		batch1.push_back({ infos[i].actor->Class + SDK.Mesh, sizeof(uint64_t), &infos[i].freshMesh }); 
	}

	for (size_t i = 0; i < batch1.size(); i += MAX_BATCH_ENTRIES) {
		size_t chunk = std::min((size_t)MAX_BATCH_ENTRIES, batch1.size() - i);
		TargetProcess.BatchRead(&batch1[i], chunk);
	}
	Local.Recoil = localRecoil;

std::vector<DriverInterfaceV3::BatchReadEntry> batch2;
	Vector3 localPos = Vector3(CameraEntry.POV.Location.X, CameraEntry.POV.Location.Y, CameraEntry.POV.Location.Z);

	for (size_t i = 0; i < infos.size(); i++) {
		auto& a = infos[i].actor;
		if (!a->Class || !a->RootComponent || !a->isCheck) continue;

		a->LastTeamNum = infos[i].lastTeamNum;
		if (a->Class == AcknowledgedPawn) Local.Teamid = a->LastTeamNum;
		a->Health = DecryptHealth(infos[i].healthBulk);
		// Mark dead: invalid health range means dead or garbage actor
		// a->isDie = (a->Health <= 0.0f || a->Health > 150.0f);
		
		if (a->LastTeamNum == Local.Teamid && a->Class != AcknowledgedPawn) continue;

		a->UEPosition = infos[i].pos;
		if (a->UEPosition.X != 0.0 || a->UEPosition.Y != 0.0 || a->UEPosition.Z != 0.0) {
			a->LastGoodPosition = a->UEPosition;
		} else {
			a->UEPosition = a->LastGoodPosition;
		}
		a->Velocity = infos[i].velocity;
		float distance = Vector3::Distance(localPos, a->GetPosition()) / 100.0f;

		if (IsAddrValid(infos[i].freshMesh)) {
			a->Mesh = infos[i].freshMesh;
		}

		if ((a->PlayerType != 0 && distance < (float)Configs.Survivor.MaxDistance) || a->Class == AcknowledgedPawn) {
			batch2.push_back({ a->Mesh, sizeof(infos[i].meshBulk), &infos[i].meshBulk });
			infos[i].successPos = true;
		}
	}

	for (size_t i = 0; i < batch2.size(); i += MAX_BATCH_ENTRIES) {
		size_t chunk = std::min((size_t)MAX_BATCH_ENTRIES, batch2.size() - i);
		TargetProcess.BatchRead(&batch2[i], chunk);
	}
	std::vector<DriverInterfaceV3::BatchReadEntry> batch3;
	std::vector<size_t> validIndices;

	for (size_t i = 0; i < infos.size(); i++) {
		if (infos[i].successPos) {
			validIndices.push_back(i);
		}
	}

	Vector3 stage3CamPos = Vector3(CameraEntry.POV.Location.X, CameraEntry.POV.Location.Y, CameraEntry.POV.Location.Z);
	std::sort(validIndices.begin(), validIndices.end(), [&](size_t a, size_t b) {
		float distA = Vector3::Distance(stage3CamPos, infos[a].actor->GetPosition());
		float distB = Vector3::Distance(stage3CamPos, infos[b].actor->GetPosition());
		return distA < distB;
	});

	int skeletonsProcessed = 0;
	int maxSkeletons = 15;

	for (size_t idx : validIndices) {
		auto& info = infos[idx];
		auto& a = info.actor;

		a->ToWorld = *(FTransform*)(&info.meshBulk[SDK.ComponentToWorld]);
		a->BoneArray = *(uint64_t*)(&info.meshBulk[SDK.StaticMesh]);

		if (a->Class == AcknowledgedPawn) {
			continue;
		}

		if (Configs.Survivor.Skeleton && skeletonsProcessed < maxSkeletons) {
			if (a->BoneArray > 0x1000000) {
				bool isFemale = (a->objName.find("Female") != std::string::npos);
				size_t boneCount = isFemale ? 190 : 183;
				batch3.push_back({ a->BoneArray + (1 * sizeof(FTransform)), boneCount * sizeof(FTransform), &info.fullBoneBuffer });
				info.successMesh = true;
			}
			skeletonsProcessed++;
		}
	}

	for (size_t i = 0; i < batch3.size(); i += MAX_BATCH_ENTRIES) {
		size_t chunk = std::min((size_t)MAX_BATCH_ENTRIES, batch3.size() - i);
		TargetProcess.BatchRead(&batch3[i], chunk);
	}

	
	float localRenderTime = 0.0f;
	for (size_t i = 0; i < infos.size(); i++) {
		if (infos[i].actor->Class == AcknowledgedPawn && infos[i].successPos) {
			localRenderTime = *(float*)(&infos[i].meshBulk[SDK.LastRenderTimeOnScreen]);
			break;
		}
	}

	for (size_t i = 0; i < infos.size(); i++) {
		if (!infos[i].successPos) continue;
		auto& a = infos[i].actor;

		a->LastRenderTime = *(float*)(&infos[i].meshBulk[SDK.LastRenderTimeOnScreen]);
		if (a->Class != AcknowledgedPawn) {
			a->IsVisible = (a->LastRenderTime + 0.05f >= localRenderTime);
		}

		if (a->Class == AcknowledgedPawn) continue;
		if (!infos[i].successMesh) {
			continue;
		}

		auto GetBone = [&](int idx) -> FTransform {
			if (idx >= 1 && idx <= 189) {
				return *(FTransform*)(&infos[i].fullBoneBuffer[(idx - 1) * sizeof(FTransform)]);
			}
			return FTransform();
		};

		a->Head      = GetBone(a->index.Head);
		a->neck      = GetBone(a->index.neck);
		a->pelvis    = GetBone(a->index.pelvis);
		a->Lshoulder = GetBone(a->index.Lshoulder);
		a->Lelbow    = GetBone(a->index.Lelbow);
		a->Lhand     = GetBone(a->index.Lhand);
		a->Rshoulder = GetBone(a->index.Rshoulder);
		a->Relbow    = GetBone(a->index.Relbow);
		a->Rhand     = GetBone(a->index.Rhand);
		a->Lbuttock  = GetBone(a->index.Lbuttock);
		a->Lknee     = GetBone(a->index.Lknee);
		a->Lfoot     = GetBone(a->index.Lfoot);
		a->Lball     = GetBone(a->index.Lball);
		a->Rbuttock  = GetBone(a->index.Rbuttock);
		a->Rknee     = GetBone(a->index.Rknee);
		a->Rfoot     = GetBone(a->index.Rfoot);
		a->Rball     = GetBone(a->index.Rball);

		a->SetUp3();
		a->bHasBones = true; 
	}
}


void Engine::RefreshViewMatrix()
{
	if (!PlayerCameraManager) return; 
	
	float camFov = 0.0f;
	UEVector camPos = {};
	UERotator camRot = {};

	DriverInterfaceV3::BatchReadEntry camBatch[3];
	camBatch[0] = { PlayerCameraManager + SDK.CameraFov, sizeof(float), &camFov };
	camBatch[1] = { PlayerCameraManager + SDK.CameraPos, sizeof(UEVector), &camPos };
	camBatch[2] = { PlayerCameraManager + SDK.CameraRot, sizeof(UERotator), &camRot };

	if (TargetProcess.BatchRead(camBatch, 3)) {
		CameraEntry.POV.FOV = camFov;
		CameraEntry.POV.Location = camPos;
		CameraEntry.POV.Rotation = camRot;
		CameraViewInfo = CameraEntry.POV;
	}
}

CameraCacheEntry Engine::GetCameraCache()
{
	return CameraEntry;
}

std::vector<std::shared_ptr<ActorEntity>> Engine::GetActors()
{
	return Actors;
}

uint64_t Engine::GetActorSize()
{
	return MaxPacket;
}
