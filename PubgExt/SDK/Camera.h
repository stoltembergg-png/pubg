#pragma once
#include "Engine.h"

class Camera
{
public:
	static Vector2 WorldToScreen(MinimalViewInfo viewinfo, Vector3 world);
	static Vector2 WorldToScreen(MinimalViewInfo viewinfo, UERotator customRotation, Vector3 world);
	static Vector2 WorldToScreenCustomRot(Vector3 localPos, Vector3 customRotation, float fov, Vector3 world);
	static MinimalViewInfo GetCurrentView(uintptr_t managerPtr);
};