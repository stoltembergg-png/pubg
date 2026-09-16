#include "pch.h"
#include "Radar.h"
#include "../Config/ConfigUtilities.h"
#include "../OS-ImGui/OS-ImGui.h"
#include <string>

ImVec2 Radar::RotatePoint(const Vector3& localPos, const Vector3& enemyPos,
                         float localYaw, float radarX, float radarY, float radarW,
                         float radarH, float range) {
    float dx = enemyPos.x - localPos.x;
    float dy = enemyPos.y - localPos.y;

    // Rotate based on local player's view angle
    // PUBG: Yaw 0 is X+
    float yawRad = (localYaw) * (float)(M_PI / 180.0f);
    float cosY = cosf(-yawRad);
    float sinY = sinf(-yawRad);

    float rotX = dx * cosY - dy * sinY;
    float rotY = dx * sinY + dy * cosY;

    // Scale (Range in meters, PUBG units are CM)
    float rangeUnits = range * 100.0f;
    float scale = (radarW / 2.0f) / rangeUnits;

    rotX *= scale;
    rotY *= scale;

    float centerX = radarX + radarW / 2.0f;
    float centerY = radarY + radarH / 2.0f;

    float finalX = centerX + rotY; 
    float finalY = centerY - rotX;

    float halfW = radarW / 2.0f - 5.0f;
    float halfH = radarH / 2.0f - 5.0f;
    finalX = fmaxf(centerX - halfW, fminf(finalX, centerX + halfW));
    finalY = fmaxf(centerY - halfH, fminf(finalY, centerY + halfH));

    return ImVec2(finalX, finalY);
}

void Radar::DrawArrow(ImDrawList* drawList, ImVec2 center, float yaw, float size, ImU32 color) {
    float angle = yaw * (float)(M_PI / 180.0f);

    float tipLen = size * 1.5f;
    float baseLen = size * 0.8f;
    float baseWidth = size * 0.6f;

    ImVec2 tip(center.x + tipLen * sinf(angle), center.y - tipLen * cosf(angle));
    
    float perpAngle = angle + (float)(M_PI / 2.0f);
    ImVec2 baseLeft(
        center.x - baseLen * sinf(angle) + baseWidth * sinf(perpAngle),
        center.y + baseLen * cosf(angle) - baseWidth * cosf(perpAngle));
    ImVec2 baseRight(
        center.x - baseLen * sinf(angle) - baseWidth * sinf(perpAngle),
        center.y + baseLen * cosf(angle) + baseWidth * cosf(perpAngle));

    drawList->AddTriangleFilled(tip, baseLeft, baseRight, color);
    drawList->AddTriangle(tip, baseLeft, baseRight, IM_COL32(0, 0, 0, 200), 1.0f);
}

void Radar::Render(const SharedData& data) {
    auto& config = Configs.Survivor;
    if (!config.Radar) return;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    
    float x = 20.0f; // Fixed position for now
    float y = 100.0f;
    float w = config.RadarSize;
    float h = config.RadarSize;

    drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0, 0, 0, 120), 5.0f);
    drawList->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(255, 255, 255, 50), 5.0f, 0, 1.0f);

    float centerX = x + w / 2.0f;
    float centerY = y + h / 2.0f;
    drawList->AddLine(ImVec2(x, centerY), ImVec2(x + w, centerY), IM_COL32(255, 255, 255, 30));
    drawList->AddLine(ImVec2(centerX, y), ImVec2(centerX, y + h), IM_COL32(255, 255, 255, 30));

    drawList->AddCircleFilled(ImVec2(centerX, centerY), 3.0f, IM_COL32(0, 255, 0, 255));

    Vector3 localPos = Vector3(data.CameraCache.POV.Location.X, data.CameraCache.POV.Location.Y, data.CameraCache.POV.Location.Z);
    float localYaw = data.CameraCache.POV.Rotation.Yaw;

    for (auto& entity : data.Actors) {
        if (!entity || entity->isDie || entity->PlayerType == 0) continue;
        if (entity->Class == data.AcknowledgedPawn) continue;

        Vector3 enemyPos = entity->GetPosition();
        ImVec2 radarPos = RotatePoint(localPos, enemyPos, localYaw, x, y, w, h, config.RadarRange);

        ImU32 color = IM_COL32(255, 0, 0, 200); 
        if (entity->PlayerType == 1) color = IM_COL32(255, 165, 0, 200); // Orange for bots

        drawList->AddCircleFilled(radarPos, 4.0f, color);
        
        // Directional Line (Placeholder for viewangles if we find them later, currently dots)
    }
}
