#include "pch.h"
#include "PlayerEsp.h"
#include "Engine.h"
#include "Globals.h"
#include "Camera.h"
#include "../Config/ConfigUtilities.h"
#include "../OS-ImGui/OS-ImGui.h"
#include "../SharedState.h"
extern bool RunDrawingTest;

void DrawPlayerEsp(const SharedData& data)
{
	float minDis = 9999.0f;
	auto drawList = ImGui::GetBackgroundDrawList();

	for (auto entity : data.Actors)
	{
		if (!entity) continue;
		if (entity->isDie) continue;
		if (entity->PlayerType == 0) continue; // Unidentified/Local
		if (entity->Class == data.AcknowledgedPawn || entity->Class == data.LocalCharacterPawn) continue; // Skip local player

		Vector3 actorPos = entity->GetPosition();
		if (actorPos.IsZero()) continue;

		auto& config = Configs.Survivor;

		// Raw Debug Drawing (Always draw a small dot/text for any discovered actor if in debug)
		Vector2 rootScreen = Camera::WorldToScreen(data.CameraCache.POV, actorPos);
		if (!rootScreen.IsZero() && RunDrawingTest) { // Use RunDrawingTest as a proxy for "Draw Everything"
			char debugInfo[128];
			sprintf(debugInfo, "[%d] %s", entity->id, entity->objName.c_str());
			Gui.Text(debugInfo, Vec2(rootScreen.x, rootScreen.y), IM_COL32(255, 255, 0, 200), 12.0f);
		}

		if (entity->PlayerType == 0) continue; // Skip non-players for main ESP

		// Calculate Head and Foot positions for the box
		Vector3 headPos;
		Vector3 footPos;

		if (entity->bHasBones) {
			headPos = Vector3(entity->Head3D.X, entity->Head3D.Y, entity->Head3D.Z + 10.0f); // Top of head + helmet margin
			
			// Use the lowest ball-of-foot bone as the actual base of the player (ground level)
			Vector3 ballL = Vector3(entity->Lball3D.X, entity->Lball3D.Y, entity->Lball3D.Z);
			Vector3 ballR = Vector3(entity->Rball3D.X, entity->Rball3D.Y, entity->Rball3D.Z);
			footPos = (ballL.z < ballR.z) ? ballL : ballR; // Z is up in Unreal Engine

			// Fallback to foot bones if ball reads fail
			if (footPos.IsZero()) {
				Vector3 footL = Vector3(entity->Lfoot3D.X, entity->Lfoot3D.Y, entity->Lfoot3D.Z);
				Vector3 footR = Vector3(entity->Rfoot3D.X, entity->Rfoot3D.Y, entity->Rfoot3D.Z);
				footPos = (footL.z < footR.z) ? footL : footR;
			}

			// Fallback to actorPos if all bone reads fail
			if (footPos.IsZero()) {
				footPos = actorPos;
			}
		} else {
			// Estimate based on actorPos (since actorPos is elevated at the pelvis, adjust up and down)
			headPos = Vector3(actorPos.x, actorPos.y, actorPos.z + 90.0f);
			footPos = Vector3(actorPos.x, actorPos.y, actorPos.z - 90.0f);
		}

		Vector2 HeadScreen = Camera::WorldToScreen(data.CameraCache.POV, headPos);
		Vector2 FootScreen = Camera::WorldToScreen(data.CameraCache.POV, footPos);

		if (HeadScreen.IsZero() || FootScreen.IsZero()) continue;

		Vector3 campos = Vector3(data.CameraCache.POV.Location.X, data.CameraCache.POV.Location.Y, data.CameraCache.POV.Location.Z);
		float distance = (Vector3::Distance(campos, actorPos) / 100.0f);

		// Skip rendering for players outside the configured ESP Render Distance
		if (distance > config.MaxDistance) continue;

		char wdistance[256] = {0};
		if (entity->PlayerType == 1) {
			if (config.Distance || config.Name) sprintf(wdistance, "[AI] %dm", (int)distance);
		} else if (entity->PlayerType == 2) {
			if (config.Distance || config.Name) sprintf(wdistance, "Player %dm", (int)distance);
		} else {
			continue;
		}

		if (!HeadScreen.IsZero() && !FootScreen.IsZero())
		{
			if (entity->LastTeamNum != Local.Teamid) {
				if (distance < minDis) {
					minDis = distance;
				}
			}

            auto getCol32 = [](Colour c) -> ImU32 { return IM_COL32(c.r, c.g, c.b, c.a); };
			ImU32 boxColor = entity->IsVisible ? IM_COL32(0, 255, 0, 255) : getCol32(config.TextColour);
			ImU32 skeletonColor = entity->IsVisible ? IM_COL32(0, 255, 0, 180) : IM_COL32(255, 255, 255, 180);

			// Draw Box — use bone spread when available for accurate width
			float height = abs(HeadScreen.y - FootScreen.y);
			float centerX = (HeadScreen.x + FootScreen.x) / 2.0f;
			float width = height / 2.0f; // Default ratio fallback

			if (config.Box) {
				if (entity->bHasBones) {
					// Project all key bones to find actual screen-space bounding box
					float minX = 99999.0f, maxX = -99999.0f;
					float minY = 99999.0f, maxY = -99999.0f;

					UEVector bonesToProject[] = {
						entity->Head3D, entity->neck3D, entity->pelvis3D,
						entity->Lshoulder3D, entity->Lelbow3D, entity->Lhand3D,
						entity->Rshoulder3D, entity->Relbow3D, entity->Rhand3D,
						entity->Lbuttock3D, entity->Lknee3D, entity->Lfoot3D, entity->Lball3D,
						entity->Rbuttock3D, entity->Rknee3D, entity->Rfoot3D, entity->Rball3D
					};

					for (auto& bone : bonesToProject) {
						Vector2 bp = Camera::WorldToScreen(data.CameraCache.POV, Vector3(bone.X, bone.Y, bone.Z));
						if (bp.IsZero()) continue;
						if (bp.x < minX) minX = bp.x;
						if (bp.x > maxX) maxX = bp.x;
						if (bp.y < minY) minY = bp.y;
						if (bp.y > maxY) maxY = bp.y;
					}

					if (maxX > minX && maxY > minY) {
						float boneWidth = (maxX - minX);
						float boneHeight = (maxY - minY);
						// Add 15% padding for gear/backpack/weapon
						float padX = boneWidth * 0.15f;
						float padY = boneHeight * 0.08f;
						centerX = (minX + maxX) / 2.0f;
						width = boneWidth + padX * 2.0f;
						height = boneHeight + padY * 2.0f;
						float top = minY - padY;
						Gui.Rectangle(Vec2(centerX - width / 2.0f, top), Vec2(width, height), boxColor, 1.5f);
					} else {
						// Bone projections failed, use fallback
						Gui.Rectangle(Vec2(centerX - width / 2, HeadScreen.y), Vec2(width, height), boxColor, 1.5f);
					}
				} else {
					Gui.Rectangle(Vec2(centerX - width / 2, HeadScreen.y), Vec2(width, height), boxColor, 1.5f);
				}
			}

			// Draw Horizontal Health Bar (on top of box)
			if (config.Health) {
				float healthPct = entity->Health * 100.0f;
				if (healthPct > 100.0f) healthPct = 100.0f;
				if (healthPct < 0.0f) healthPct = 0.0f;
				float healthFrac = healthPct / 100.0f;

				// Bar dimensions scale with box (distance-dependent)
				float barWidth = width;
				float barHeight = height * 0.04f;
				if (barHeight < 3.0f) barHeight = 3.0f;
				if (barHeight > 6.0f) barHeight = 6.0f;
				float barX = centerX - barWidth / 2.0f;
				float barY = HeadScreen.y - barHeight - 3.0f; // sits just above the box

				// Health color
				ImU32 healthColor;
				if (healthPct < 30.0f)
					healthColor = IM_COL32(255, 40, 40, 255);
				else if (healthPct < 60.0f)
					healthColor = IM_COL32(255, 220, 50, 255);
				else
					healthColor = IM_COL32(0, 255, 80, 255);

				float rounding = barHeight * 0.5f; // pill-shaped rounded ends

				// Background bar (dark)
				drawList->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barWidth, barY + barHeight), IM_COL32(0, 0, 0, 180), rounding);
				// Filled portion
				if (healthFrac > 0.01f) {
					float fillW = barWidth * healthFrac;
					if (fillW < rounding * 2.0f) fillW = rounding * 2.0f; // min width for rounding to render
					drawList->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + fillW, barY + barHeight), healthColor, rounding);
				}
				// Outline
				drawList->AddRect(ImVec2(barX, barY), ImVec2(barX + barWidth, barY + barHeight), IM_COL32(0, 0, 0, 220), rounding, 0, 1.0f);
			}

			// Draw Skeleton
			if (config.Skeleton && entity->bHasBones) {
				auto W2S = [&](UEVector world) -> Vec2 {
					Vector2 screen = Camera::WorldToScreen(data.CameraCache.POV, Vector3(world.X, world.Y, world.Z));
					return Vec2(screen.x, screen.y);
				};

				Vec2 head = W2S(entity->Head3D);
				Vec2 neck = W2S(entity->neck3D);
				Vec2 pelvis = W2S(entity->pelvis3D);
				Vec2 lShoulder = W2S(entity->Lshoulder3D), lElbow = W2S(entity->Lelbow3D), lHand = W2S(entity->Lhand3D);
				Vec2 rShoulder = W2S(entity->Rshoulder3D), rElbow = W2S(entity->Relbow3D), rHand = W2S(entity->Rhand3D);
				Vec2 lButtock = W2S(entity->Lbuttock3D), lKnee = W2S(entity->Lknee3D), lFoot = W2S(entity->Lfoot3D), lBall = W2S(entity->Lball3D);
				Vec2 rButtock = W2S(entity->Rbuttock3D), rKnee = W2S(entity->Rknee3D), rFoot = W2S(entity->Rfoot3D), rBall = W2S(entity->Rball3D);

				// Spine
				Gui.Line(head, neck, skeletonColor, 1.0f);
				Gui.Line(neck, pelvis, skeletonColor, 1.0f);

				// Left Arm
				Gui.Line(neck, lShoulder, skeletonColor, 1.0f);
				Gui.Line(lShoulder, lElbow, skeletonColor, 1.0f);
				Gui.Line(lElbow, lHand, skeletonColor, 1.0f);

				// Right Arm
				Gui.Line(neck, rShoulder, skeletonColor, 1.0f);
				Gui.Line(rShoulder, rElbow, skeletonColor, 1.0f);
				Gui.Line(rElbow, rHand, skeletonColor, 1.0f);

				// Left Leg (4-segment: hip→knee→foot→ball)
				Gui.Line(pelvis, lButtock, skeletonColor, 1.0f);
				Gui.Line(lButtock, lKnee, skeletonColor, 1.0f);
				Gui.Line(lKnee, lFoot, skeletonColor, 1.0f);
				Gui.Line(lFoot, lBall, skeletonColor, 1.0f);

				// Right Leg (4-segment: hip→knee→foot→ball)
				Gui.Line(pelvis, rButtock, skeletonColor, 1.0f);
				Gui.Line(rButtock, rKnee, skeletonColor, 1.0f);
				Gui.Line(rKnee, rFoot, skeletonColor, 1.0f);
				Gui.Line(rFoot, rBall, skeletonColor, 1.0f);
			}

	

            // Draw Distance/Name Text
			if (config.Name || config.Distance) {
				Gui.Text(wdistance, Vec2(centerX, FootScreen.y + 5), boxColor, 15.0f, true);
			}
		}
	}

	if (Local.SpectatedCount > 0 && Local.SpectatedCount < 100) {
		char text[64];
		sprintf(text, "Spectators: %d", Local.SpectatedCount);
		Gui.Text(text, Vec2(Gui.Window.Size.x / 2.0f - 30, Gui.Window.Size.y - 100), IM_COL32(255, 255, 255, 255));
	}
	if (minDis < 9999.0f) {
		char text[64];
		sprintf(text, "Closest Enemy: %dm", (int)minDis);
		Gui.Text(text, Vec2(Gui.Window.Size.x / 2.0f - 30, 100), IM_COL32(255, 255, 255, 255));
	}

	// --- Grenade / Molotov ESP ---
	DrawGrenadeEsp(data);
}

void DrawGrenadeEsp(const SharedData& data)
{
	auto drawList = ImGui::GetBackgroundDrawList();
	Vector3 camPos(data.CameraCache.POV.Location.X,
	               data.CameraCache.POV.Location.Y,
	               data.CameraCache.POV.Location.Z);

	for (auto& entity : data.GrenadeActors) {
		if (!entity || entity->PlayerType != 3) continue;

		bool isMolotov = (entity->objName == "ProjMolotov_C");
		bool isGrenade = (entity->objName == "ProjGrenade_C");

		// --- Timer logic: grenades use game offset, molotovs use client-side countdown ---
		float timer = 0.0f;
		if (isGrenade) {
			if (entity->ExplodeState != 0) continue;        // Already detonated
			if (entity->TimeTillExplosion <= 0.0f) continue; // Expired
			timer = entity->TimeTillExplosion;
		} else if (isMolotov) {
			// Molotovs don't share the grenade's TimeTillExplosion offset.
			// Use client-side spawn time with 5 second burn duration.
			if (!entity->SpawnTimeSet) {
				entity->SpawnTime = std::chrono::steady_clock::now();
				entity->SpawnTimeSet = true;
			}
			float elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - entity->SpawnTime).count() / 1000.0f;
			timer = 5.0f - elapsed;
			if (timer <= 0.0f) continue; // Burn finished, stop rendering
		} else {
			continue; // Unknown projectile type
		}

		Vector3 grenadePos = entity->GetPosition();
		if (grenadePos.IsZero()) continue;

		float distance = Vector3::Distance(camPos, grenadePos) / 100.0f;
		if (distance > 150.0f) continue;  // Max render distance for grenades

		Vector2 screenPos = Camera::WorldToScreen(data.CameraCache.POV, grenadePos);
		if (screenPos.IsZero()) continue;

		float radius = entity->ExplosionRadius;  // In Unreal Units

		// --- Color by urgency ---
		ImU32 textColor;
		ImU32 circleColor;
		if (timer > 3.0f) {
			textColor = IM_COL32(0, 255, 0, 255);       // Green = safe
			circleColor = IM_COL32(0, 255, 0, 120);
		} else if (timer > 1.5f) {
			textColor = IM_COL32(255, 255, 0, 255);      // Yellow = warning
			circleColor = IM_COL32(255, 255, 0, 150);
		} else {
			textColor = IM_COL32(255, 0, 0, 255);        // Red = danger
			circleColor = IM_COL32(255, 0, 0, 200);
		}

		// --- Draw world-space explosion radius circle ---
		// Projects 40 points on a horizontal circle around the grenade through WorldToScreen.
		// This gives a proper perspective-distorted ellipse, not a flat screen circle.
		if (radius > 0.0f && distance < 80.0f) {
			const int segments = 40;
			const float step = 6.2831853f / segments;  // 2*PI / segments
			std::vector<ImVec2> points;
			points.reserve(segments);

			for (int i = 0; i < segments; i++) {
				float theta = step * i;
				Vector3 worldPt(
					grenadePos.x + radius * cosf(theta),
					grenadePos.y + radius * sinf(theta),
					grenadePos.z  // Flat circle at grenade height
				);
				Vector2 screenPt = Camera::WorldToScreen(data.CameraCache.POV, worldPt);
				if (!screenPt.IsZero())
					points.push_back(ImVec2(screenPt.x, screenPt.y));
			}

			if (points.size() >= 3) {
				drawList->AddPolyline(points.data(), (int)points.size(),
					circleColor, ImDrawFlags_Closed, 2.0f);
			}
		}

		// --- Draw label: "FRAG (3.2s) [45m]" ---
		const char* label = isGrenade ? "FRAG" : "MOLOTOV";
		char text[64];
		sprintf(text, "%s (%.1fs) [%dm]", label, timer, (int)distance);

		ImVec2 textSize = ImGui::CalcTextSize(text);
		ImVec2 textPos(screenPos.x - textSize.x / 2.0f, screenPos.y - 20.0f);

		// Shadow outline for readability
		drawList->AddText(ImVec2(textPos.x + 1, textPos.y + 1), IM_COL32(0, 0, 0, 200), text);
		// Main text
		drawList->AddText(textPos, textColor, text);

		// --- Warning marker dot at grenade position ---
		drawList->AddCircleFilled(ImVec2(screenPos.x, screenPos.y), 4.0f, textColor);
		drawList->AddCircle(ImVec2(screenPos.x, screenPos.y), 6.0f, IM_COL32(0, 0, 0, 200), 0, 1.5f);
	}
}