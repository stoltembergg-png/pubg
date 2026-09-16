#pragma once
#include "EngineStructs.h"
enum class EPlayerRole: uint8_t
{
	EPlayerRole__VE_None = 0,
	EPlayerRole__VE_Slasher = 1,
	EPlayerRole__VE_Camper = 2,
	EPlayerRole__VE_Observer = 3,
	EPlayerRole__Max = 4,
	EPlayerRole__EPlayerRole_MAX = 5
};

struct Index
{
	int Head,
		neck,
		pelvis,
		Lshoulder,
		Lelbow,
		Lhand,
		Rshoulder,
		Relbow,
		Rhand;
	int	Lbuttock,
		Lknee,
		Lfoot,
		Lball,
		Rbuttock,
		Rknee,
		Rfoot,
		Rball;
};

class FMatrix
{
public:
	float _11, _12, _13, _14;
	float _21, _22, _23, _24;
	float _31, _32, _33, _34;
	float _41, _42, _43, _44;
	float m[4][4];
	FMatrix MatrixMultiplication(const FMatrix& other);
};
struct FQuat
{
	float X;
	float Y;
	float Z;
	float W;
};
struct FTransform
{
	FQuat Rotation;
	FQuat Translation;
	FQuat Scale3D;

	FMatrix ToMatrixWithScale();
};

void GetIndex(Index& index, const std::string& objName);

class ActorEntity
{
public:

	FTransform Head, neck, pelvis, Lshoulder, Lelbow, Lhand, Rshoulder, Relbow, Rhand,
		Lbuttock, Lknee, Lfoot, Lball, Rbuttock, Rknee, Rfoot, Rball;
	UEVector Head3D, neck3D, pelvis3D, Lshoulder3D, Lelbow3D, Lhand3D, Rshoulder3D, Relbow3D, Rhand3D,
		Lbuttock3D, Lknee3D, Lfoot3D, Lball3D, Rbuttock3D, Rknee3D, Rfoot3D, Rball3D;


	uint64_t Class = 0;
	int PlayerRole;
	bool isDie = true;
	int TempId;
	int id;
	int PlayerType;
	float Health;
	std::string objName;
	Index index{};
	uint64_t PlayerState, RootComponent, MeshLastTeamNum, Mesh, BoneArray, AcknowledgedPawn, RelativeLocation;
	int LastTeamNum;
	FTransform ToWorld;
	bool isCheck = FALSE;
	bool IsVisible = false;
	bool bHasBones = false;
	float LastRenderTime = 0.0f;
	std::wstring Name = LIT(L"Entity");
	UEVector UEPosition;
	UEVector LastGoodPosition;  // Cache for surviving transient zero-reads
	Vector3 Position;
	Vector3 Velocity;

	// Grenade ESP fields (only valid when PlayerType == 3)
	float TimeTillExplosion = 0.0f;
	uint32_t ExplodeState = 0;
	float ExplosionRadius = 0.0f;  // Hardcoded per grenade type (in Unreal Units)
	std::chrono::steady_clock::time_point SpawnTime;
	bool SpawnTimeSet = false;

	ActorEntity(uint64_t address);
	void SetUp1();
	void SetUp2();
	void SetUp3();
	uint64_t GetClass();
	int GetPlayerRole();
	std::wstring GetName();
	Vector3 GetPosition();
	void UpdatePosition();
	void UpdateVelocity();
	UEVector GetBoneMatrix(FTransform bone);
	void UpdateBone();
};