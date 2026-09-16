#include "pch.h"
#include "ActorEntity.h"
#include "Camera.h"
#include "Globals.h"
FMatrix FTransform::ToMatrixWithScale()
{
	FMatrix m;

	m._41 = Translation.X;
	m._42 = Translation.Y;
	m._43 = Translation.Z;

	float x2 = Rotation.X + Rotation.X;
	float y2 = Rotation.Y + Rotation.Y;
	float z2 = Rotation.Z + Rotation.Z;

	float xx2 = Rotation.X * x2;
	float yy2 = Rotation.Y * y2;
	float zz2 = Rotation.Z * z2;
	m._11 = (1.0f - (yy2 + zz2)) * Scale3D.X;
	m._22 = (1.0f - (xx2 + zz2)) * Scale3D.Y;
	m._33 = (1.0f - (xx2 + yy2)) * Scale3D.Z;


	float yz2 = Rotation.Y * z2;
	float wx2 = Rotation.W * x2;
	m._32 = (yz2 - wx2) * Scale3D.Z;
	m._23 = (yz2 + wx2) * Scale3D.Y;


	float xy2 = Rotation.X * y2;
	float wz2 = Rotation.W * z2;
	m._21 = (xy2 - wz2) * Scale3D.Y;
	m._12 = (xy2 + wz2) * Scale3D.X;


	float xz2 = Rotation.X * z2;
	float wy2 = Rotation.W * y2;
	m._31 = (xz2 + wy2) * Scale3D.Z;
	m._13 = (xz2 - wy2) * Scale3D.X;

	m._14 = 0.0f;
	m._24 = 0.0f;
	m._34 = 0.0f;
	m._44 = 1.0f;

	return m;
}


FMatrix FMatrix::MatrixMultiplication(const FMatrix& other)
{
	FMatrix NewMatrix;

	NewMatrix._11 = this->_11 * other._11 + this->_12 * other._21 + this->_13 * other._31 + this->_14 * other._41;
	NewMatrix._12 = this->_11 * other._12 + this->_12 * other._22 + this->_13 * other._32 + this->_14 * other._42;
	NewMatrix._13 = this->_11 * other._13 + this->_12 * other._23 + this->_13 * other._33 + this->_14 * other._43;
	NewMatrix._14 = this->_11 * other._14 + this->_12 * other._24 + this->_13 * other._34 + this->_14 * other._44;
	NewMatrix._21 = this->_21 * other._11 + this->_22 * other._21 + this->_23 * other._31 + this->_24 * other._41;
	NewMatrix._22 = this->_21 * other._12 + this->_22 * other._22 + this->_23 * other._32 + this->_24 * other._42;
	NewMatrix._23 = this->_21 * other._13 + this->_22 * other._23 + this->_23 * other._33 + this->_24 * other._43;
	NewMatrix._24 = this->_21 * other._14 + this->_22 * other._24 + this->_23 * other._34 + this->_24 * other._44;
	NewMatrix._31 = this->_31 * other._11 + this->_32 * other._21 + this->_33 * other._31 + this->_34 * other._41;
	NewMatrix._32 = this->_31 * other._12 + this->_32 * other._22 + this->_33 * other._32 + this->_34 * other._42;
	NewMatrix._33 = this->_31 * other._13 + this->_32 * other._23 + this->_33 * other._33 + this->_34 * other._43;
	NewMatrix._34 = this->_31 * other._14 + this->_32 * other._24 + this->_33 * other._34 + this->_34 * other._44;
	NewMatrix._41 = this->_41 * other._11 + this->_42 * other._21 + this->_43 * other._31 + this->_44 * other._41;
	NewMatrix._42 = this->_41 * other._12 + this->_42 * other._22 + this->_43 * other._32 + this->_44 * other._42;
	NewMatrix._43 = this->_41 * other._13 + this->_42 * other._23 + this->_43 * other._33 + this->_44 * other._43;
	NewMatrix._44 = this->_41 * other._14 + this->_42 * other._24 + this->_43 * other._34 + this->_44 * other._44;

	return NewMatrix;
}
void GetIndex(Index& index, const std::string& objName)
{
	index.Head = 10;
	index.neck = 5;
	index.pelvis = 1;

	if (objName.find("Female") != std::string::npos) {
		// PlayerFemale bone indices
		index.Lshoulder = 95;
		index.Lelbow = 96;
		index.Lhand = 97;
		index.Rshoulder = 122;
		index.Relbow = 123;
		index.Rhand = 124;
		index.Lbuttock = 180;
		index.Lknee = 183;
		index.Lfoot = 181;
		index.Lball = 182;
		index.Rbuttock = 186;
		index.Rknee = 189;
		index.Rfoot = 187;
		index.Rball = 188;
	} else {
		// PlayerMale bone indices 
		index.Lshoulder = 88;
		index.Lelbow = 89;
		index.Lhand = 90;
		index.Rshoulder = 115;
		index.Relbow = 116;
		index.Rhand = 117;
		index.Lbuttock = 172;
		index.Lknee = 173;
		index.Lfoot = 174;
		index.Lball = 175;
		index.Rbuttock = 178;
		index.Rknee = 179;
		index.Rfoot = 180;
		index.Rball = 181;
	}
}
ActorEntity::ActorEntity(uint64_t address)
{
	Class = address;
	PlayerRole = 0;
	TempId = 0;
	id = 0;
	PlayerType = 0;
	Health = 100.0f;
	PlayerState = 0;
	RootComponent = 0;
	MeshLastTeamNum = 0;
	Mesh = 0;
	BoneArray = 0;
	AcknowledgedPawn = 0;
	RelativeLocation = 0;
	LastTeamNum = 0;
	isCheck = false;
	isDie = false; 
	
	if(!address || !IsAddrValid(address))
		return;
		
	uint8_t buffer[0x500];
	memset(buffer, 0, sizeof(buffer));
	TargetProcess.Read(Class, buffer, sizeof(buffer), "ActorBase");

	PlayerState = *(uint64_t*)(buffer + SDK.PlayerState);
	if (PlayerState) PlayerState = EngineInstance->xe_decrypt(PlayerState);
	if (!IsAddrValid(PlayerState)) PlayerState = 0;

	RootComponent = *(uint64_t*)(buffer + SDK.RootComponent);
	if (RootComponent) RootComponent = EngineInstance->xe_decrypt(RootComponent);
	if (!IsAddrValid(RootComponent)) RootComponent = 0;
	
	Mesh = *(uint64_t*)(buffer + SDK.Mesh);
	if (!IsAddrValid(Mesh)) Mesh = 0;

	id = *(int*)(buffer + SDK.ObjectID);
	
	RelativeLocation = SDK.ComponentLocation;
}
void ActorEntity::SetUp1()
{
	// Deprecated
}

UEVector ActorEntity::GetBoneMatrix(FTransform bone)
{
		float bx = bone.Translation.X;
	float by = bone.Translation.Y;
	float bz = bone.Translation.Z;

	float x2 = ToWorld.Rotation.X + ToWorld.Rotation.X;
	float y2 = ToWorld.Rotation.Y + ToWorld.Rotation.Y;
	float z2 = ToWorld.Rotation.Z + ToWorld.Rotation.Z;

	float xx2 = ToWorld.Rotation.X * x2;
	float yy2 = ToWorld.Rotation.Y * y2;
	float zz2 = ToWorld.Rotation.Z * z2;

	float _11 = (1.0f - (yy2 + zz2)) * ToWorld.Scale3D.X;
	float _22 = (1.0f - (xx2 + zz2)) * ToWorld.Scale3D.Y;
	float _33 = (1.0f - (xx2 + yy2)) * ToWorld.Scale3D.Z;

	float yz2 = ToWorld.Rotation.Y * z2;
	float wx2 = ToWorld.Rotation.W * x2;
	float _32 = (yz2 - wx2) * ToWorld.Scale3D.Z;
	float _23 = (yz2 + wx2) * ToWorld.Scale3D.Y;

	float xy2 = ToWorld.Rotation.X * y2;
	float wz2 = ToWorld.Rotation.W * z2;
	float _21 = (xy2 - wz2) * ToWorld.Scale3D.Y;
	float _12 = (xy2 + wz2) * ToWorld.Scale3D.X;

	float xz2 = ToWorld.Rotation.X * z2;
	float wy2 = ToWorld.Rotation.W * y2;
	float _31 = (xz2 + wy2) * ToWorld.Scale3D.Z;
	float _13 = (xz2 - wy2) * ToWorld.Scale3D.X;

	float wx = bx * _11 + by * _21 + bz * _31 + ToWorld.Translation.X;
	float wy = bx * _12 + by * _22 + bz * _32 + ToWorld.Translation.Y;
	float wz = bx * _13 + by * _23 + bz * _33 + ToWorld.Translation.Z;

	return UEVector(wx, wy, wz);
}

void ActorEntity::SetUp2()
{
	// Deprecated
}
void ActorEntity::SetUp3()
{
	Head3D = GetBoneMatrix(Head);
	neck3D = GetBoneMatrix(neck);
	pelvis3D = GetBoneMatrix(pelvis);
	Lshoulder3D = GetBoneMatrix(Lshoulder);
	Lelbow3D = GetBoneMatrix(Lelbow);
	Lhand3D = GetBoneMatrix(Lhand);
	Rshoulder3D = GetBoneMatrix(Rshoulder);
	Relbow3D = GetBoneMatrix(Relbow);
	Rhand3D = GetBoneMatrix(Rhand);
	Lbuttock3D = GetBoneMatrix(Lbuttock);
	Lknee3D = GetBoneMatrix(Lknee);
	Lfoot3D = GetBoneMatrix(Lfoot);
	Lball3D = GetBoneMatrix(Lball);
	Rbuttock3D = GetBoneMatrix(Rbuttock);
	Rknee3D = GetBoneMatrix(Rknee);
	Rfoot3D = GetBoneMatrix(Rfoot);
	Rball3D = GetBoneMatrix(Rball);
}
int ActorEntity::GetPlayerRole()
{
	return PlayerRole;
}

uint64_t ActorEntity::GetClass()
{
	return Class;
}

std::wstring ActorEntity::GetName()
{
	return Name;
}

Vector3 ActorEntity::GetPosition()
{
	Position = Vector3(UEPosition.X, UEPosition.Y, UEPosition.Z);
	return Position;
}

// Refresh the cached world position from the actor's root component.
void ActorEntity::UpdatePosition()
{
	if (!Class || !RootComponent || !isCheck || !IsAddrValid(RootComponent))
		return;

	if (!TargetProcess.Read(RootComponent + RelativeLocation, &UEPosition, sizeof(UEVector), "RelativeLocation")) {
		LOG("[-] ActorEntity::UpdatePosition failed to read RelativeLocation.\n");
	}
}
void ActorEntity::UpdateVelocity()
{
	if (!Class || !RootComponent || !isCheck || !IsAddrValid(RootComponent))
		return;
	
	if (!TargetProcess.Read(RootComponent + SDK.ComponentVelocity, &Velocity, sizeof(Vector3), "ComponentVelocity")) {
	}
}
void ActorEntity::UpdateBone()
{
	if (!Class)
		return;
	if (!RootComponent)
		return;
	if (!PlayerState)
		return;
	if (!isCheck) // players aren't pawns
		return;
	if (Mesh < 65535) {
		return;
	}
	

	
}
