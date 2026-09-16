#include "pch.h"
#include "../OS-ImGui/imgui/imgui.h"
#include "../SDK/Engine.h"
#include <cmath>
#include <vector>
#include <cstdint>
#include "../SharedState.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class Radar {
public:
    Radar() = default;

    void Render(const SharedData& data);

private:
    ImVec2 RotatePoint(const Vector3& localPos, const Vector3& enemyPos,
                      float localYaw, float radarX, float radarY, float radarW,
                      float radarH, float range);

    void DrawArrow(ImDrawList* drawList, ImVec2 center, float yaw, float size,
                  ImU32 color);
};
