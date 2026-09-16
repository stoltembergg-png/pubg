#include "pch.h"
#include "Memory.h"
#include "Globals.h"
#include "Engine.h"
#include "ESP/PlayerEsp.h"
#include "SDK/Aimbot.h"
#include "OS-ImGui/OS-ImGui.h"
#include "OS-ImGui/DiscordOverlay.h"

#include "Config/ConfigUtilities.h"
#include "SDK/Camera.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <chrono>
#include <exception>
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#include "SharedState.h"
#include "ESP/Radar.h"

SharedData GSharedData;
std::mutex GDataMutex;
bool GIsRunning = true;

std::shared_ptr<Engine> EngineInstance;
Radar GRadar;
std::string ProcessName = "TslGame.exe";

bool ShowMenu = false; 
int ActiveTab = 0;
bool IsFrozen = false;
std::vector<std::shared_ptr<ActorEntity>> FrozenActors;
bool RunDrawingTest = false;

bool TestMouseEnabled = false;
std::atomic_bool TestMouseRunning = false;
std::thread TestMouseThread;

std::string GLiveLogs;
std::mutex GLiveLogsMutex;

void BaseLog(const char* fmt, ...) {
	static char buf[8192];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	printf("%s", buf);

	std::ofstream logFile("logs.txt", std::ios::app);
	if (logFile.is_open()) {
		logFile << buf;
	}

	std::lock_guard<std::mutex> lock(GLiveLogsMutex);
	GLiveLogs += buf;
	if (GLiveLogs.size() > 100000) {
		GLiveLogs.erase(0, GLiveLogs.size() - 50000);
	}
}



bool InitializeFeatures() {
	LOG("[*] Initializing Engine/Features...\n");
	
	if (!TargetProcess.Init("TslGame.exe", true, false)) {
		LOG("[-] TslGame.exe not found!\n");
		return false;
	}

	while (!TargetProcess.FixCr3()) {
		std::this_thread::sleep_for(std::chrono::seconds(3));
	}
	
	EngineInstance = std::make_shared<Engine>();
	EngineInstance->Cache();
	
	LOG("[+] Features initialized successfully.\n");
	return true;
}

void MemoryLoop() {
	while (GIsRunning) {
		int rate = Configs.Survivor.MemoryUpdateRate > 0 ? Configs.Survivor.MemoryUpdateRate : 144;
		auto target = std::chrono::microseconds(1000000 / rate);
		auto start = std::chrono::high_resolution_clock::now();

		if (EngineInstance) {
			try {
				EngineInstance->Cache();
				
				if (!EngineInstance->FrameTranslateError && EngineInstance->PlayerCameraManager != 0) {
					EngineInstance->UpdatePlayers();
					EngineInstance->UpdateGrenades();
					EngineInstance->RefreshViewMatrix();
				}

				bool dataChanged = false;
				{
					std::lock_guard<std::mutex> lock(GDataMutex);
					// Read the source collection while holding the state lock so the comparison
					// and publication of the actor snapshot are one synchronized operation.
					const size_t actorCount = EngineInstance->Actors.size();
					if (GSharedData.Actors.size() != actorCount || !EngineInstance->FrameTranslateError) {
						GSharedData.Actors = EngineInstance->Actors;
						GSharedData.GrenadeActors = EngineInstance->GrenadeActors;
						GSharedData.CameraCache = EngineInstance->GetCameraCache();
						GSharedData.UWorld = EngineInstance->UWorld;
						GSharedData.GNames = EngineInstance->GNames;
						GSharedData.GameInstance = EngineInstance->GameInstance;
						GSharedData.AcknowledgedPawn = EngineInstance->AcknowledgedPawn;
						GSharedData.LocalCharacterPawn = EngineInstance->LocalCharacterPawn;
						GSharedData.CameraManagerAddr = EngineInstance->PlayerCameraManager;
						GSharedData.Recoil = Local.Recoil;
						GSharedData.MemoryThreadId = GetCurrentThreadId();
						GSharedData.LastUpdateTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
						dataChanged = true;
					}
				}

				if (dataChanged) {
					Aimbot::Tick();
				}

				static int consecutiveFaults = 0;
				if (EngineInstance->FrameTranslateError) {
					consecutiveFaults++;
					if (consecutiveFaults > 10) { 
						TargetProcess.FixCr3();
						consecutiveFaults = 0; 
					}
				} else {
					consecutiveFaults = 0; 
				}

			} catch (const std::exception& e) {
				// Keep the memory loop alive, but do not silently discard failures.
				OutputDebugStringA("[MemoryLoop] std::exception: ");
				OutputDebugStringA(e.what());
				OutputDebugStringA("\n");
				LOG("[MemoryLoop] std::exception: %s\n", e.what());
			} catch (...) {
				// Unknown exceptions are also reported before continuing the loop.
				OutputDebugStringA("[MemoryLoop] unknown exception.\n");
				LOG("[MemoryLoop] unknown exception.\n");
			}
		}
		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
		
		if (elapsed < target) {
			std::this_thread::sleep_for(target - elapsed);
		}
	}
}

void RenderFrame() {
	static bool homeKeyWasPressed = false;
	bool homeKeyIsPressed = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
	if (homeKeyIsPressed && !homeKeyWasPressed) {
		ShowMenu = !ShowMenu;
	}
	homeKeyWasPressed = homeKeyIsPressed;

	if (TestMouseEnabled) {
		bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
		if (altDown && !TestMouseRunning.load(std::memory_order_relaxed)) {
			TestMouseRunning.store(true, std::memory_order_relaxed);
			if (!TestMouseThread.joinable()) {
				TestMouseThread = std::thread([] { 
					TargetProcess.driver.TestRandomMouseMoveLoop(TestMouseRunning); 
				});
			}
		} else if (!altDown && TestMouseRunning.load(std::memory_order_relaxed)) {
			TestMouseRunning.store(false, std::memory_order_relaxed);
			if (TestMouseThread.joinable()) {
				TestMouseThread.join();
			}
		}
	} else if (!TestMouseEnabled && TestMouseRunning.load(std::memory_order_relaxed)) {
		TestMouseRunning.store(false, std::memory_order_relaxed);
		if (TestMouseThread.joinable()) {
			TestMouseThread.join();
		}
	}

	SharedData frameData;
	{
		std::lock_guard<std::mutex> lock(GDataMutex);
		frameData = GSharedData;
	}

	if (frameData.CameraManagerAddr != 0) {
		frameData.CameraCache.POV = Camera::GetCurrentView(frameData.CameraManagerAddr);
	}

	if (ShowMenu) {
		ImGui::SetNextWindowSize(ImVec2(580, 620));
		if (ImGui::Begin("##menu", nullptr, 
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | 
			ImGuiWindowFlags_NoTitleBar)) {
			
			if (ImGui::Button("VISUALS", ImVec2(100, 30))) ActiveTab = 0;
			ImGui::SameLine();
			if (ImGui::Button("DEBUG", ImVec2(100, 30))) ActiveTab = 1;
			ImGui::SameLine();
			if (ImGui::Button("PLAYER LIST", ImVec2(100, 30))) ActiveTab = 2;
			ImGui::SameLine();
			if (ImGui::Button("LOGS", ImVec2(100, 30))) ActiveTab = 3;
			ImGui::SameLine();
			if (ImGui::Button("AIMBOT", ImVec2(100, 30))) ActiveTab = 4;
			
			ImGui::Separator();

			if (ActiveTab == 0) {
				ImGui::Text("ESP Settings");
				ImGui::SliderInt("Memory Thread FPS (Hz)", &Configs.Survivor.MemoryUpdateRate, 30, 240);
				ImGui::Checkbox("Names ESP", &Configs.Survivor.Name);
				ImGui::Checkbox("Distance ESP", &Configs.Survivor.Distance);
				ImGui::SliderInt("Max Distance", &Configs.Survivor.MaxDistance, 100, 3000);
				ImGui::Checkbox("Draw ESP Box", &Configs.Survivor.Box);
				ImGui::Checkbox("Draw Skeleton", &Configs.Survivor.Skeleton);
				ImGui::Checkbox("Show Prediction", &Configs.Survivor.Prediction);
				ImGui::Checkbox("Run Drawing Test (White Box)", &RunDrawingTest);
				
				ImGui::Separator();
				if (ImGui::Button("Save Configuration", ImVec2(150, 30))) {
					SaveConfig(L"Default");
				}
				
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Text("Hardware Spoofer Diagnostics");
				ImGui::Text("Hold ALT to randomly move cursor");
				ImGui::Checkbox("Enable ALT test", &TestMouseEnabled);
				ImGui::Separator();

				ImGui::Checkbox("Radar", &Configs.Survivor.Radar);
				if (Configs.Survivor.Radar) {
					ImGui::SliderFloat("Radar Size", &Configs.Survivor.RadarSize, 100.0f, 400.0f, "%.0f");
					ImGui::SliderFloat("Radar Range", &Configs.Survivor.RadarRange, 50.0f, 500.0f, "%.0fm");
				}
			}
			else if (ActiveTab == 1) {
				ImGui::TextColored(ImVec4(0, 1, 1, 1), "Core Engine Trace");
				ImGui::BulletText("UWorld: 0x%llX", frameData.UWorld);
				ImGui::BulletText("GNames: 0x%llX", frameData.GNames);
				
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(1, 0, 1, 1), "Local Player State");
				ImGui::BulletText("Pawn: 0x%llX", frameData.AcknowledgedPawn);
				ImGui::BulletText("Team ID: %d", Local.Teamid);
				ImGui::BulletText("Spectators: %d", Local.SpectatedCount);
				ImGui::BulletText("Bullet Speed: %.1f m/s", EngineInstance->GetCurrentBulletSpeed());
				ImGui::BulletText("Gravity: %.2f", EngineInstance->GetCurrentGravity());

				ImGui::Spacing();
				ImGui::TextColored(ImVec4(1, 1, 0, 1), "Camera & Projection");
				auto& pov = frameData.CameraCache.POV;
				ImGui::BulletText("Pos: [%.0f, %.0f, %.0f]", pov.Location.X, pov.Location.Y, pov.Location.Z);
				ImGui::BulletText("FOV: %.1f", pov.FOV);

				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0, 1, 0, 1), "Actor Diagnostics");
				ImGui::BulletText("Total Actors: %zu", frameData.Actors.size());
				
				if (!frameData.Actors.empty()) {
					auto& first = frameData.Actors[0];
					Vector3 pos = first->GetPosition();
					Vector2 screen = Camera::WorldToScreen(pov, pos);
					ImGui::BulletText("First Actor: %s", first->objName.c_str());
					ImGui::BulletText("  -> World: [%.1f, %.1f, %.1f]", pos.x, pos.y, pos.z);
					ImGui::BulletText("  -> Screen: [%.1f, %.1f]", screen.x, screen.y);
				}

				ImGui::Separator();
				if (ImGui::Button("COPY FULL DEBUG REPORT", ImVec2(200, 40))) {
					std::ostringstream oss;
					oss << "=== GLOBAL STATE ===\n";
					oss << "UWorld: 0x" << std::hex << frameData.UWorld << "\n";
					oss << "GNames: 0x" << frameData.GNames << "\n";
					oss << "AcknowledgedPawn: 0x" << frameData.AcknowledgedPawn << "\n";
					oss << "LocalTeamID: " << std::dec << Local.Teamid << "\n";
					oss << "LocalSpectators: " << Local.SpectatedCount << "\n";
					oss << "Actors Found: " << frameData.Actors.size() << "\n";
					oss << "Camera FOV: " << pov.FOV << "\n";
					oss << "Camera Pos: [" << pov.Location.X << ", " << pov.Location.Y << ", " << pov.Location.Z << "]\n";
					oss << "Camera Rot: [" << pov.Rotation.Pitch << ", " << pov.Rotation.Yaw << ", " << pov.Rotation.Roll << "]\n\n";
					
					oss << "=== ACTOR SNAPSHOT ===\n";
					for (size_t i = 0; i < frameData.Actors.size(); ++i) {
						auto& a = frameData.Actors[i];
						Vector3 pos = a->GetPosition();
						Vector2 headScreen = Camera::WorldToScreen(pov, Vector3(a->Head3D.X, a->Head3D.Y, a->Head3D.Z + 10.0f));
						Vector2 footScreen = Camera::WorldToScreen(pov, pos);
						float dist = Vector3::Distance(Vector3(pov.Location.X, pov.Location.Y, pov.Location.Z), pos) / 100.0f;
						
						oss << "--- ACTOR[" << std::dec << i << "] ---\n";
						oss << "Name: " << a->objName << "\n";
						oss << "PlayerType: " << a->PlayerType << " | PlayerRole: " << a->PlayerRole << " | id: " << a->id << "\n";
						oss << "Team: " << a->LastTeamNum << " | Health: " << a->Health << "\n";
						oss << "isDie: " << (a->isDie ? "true" : "false") << " | isCheck: " << (a->isCheck ? "true" : "false") << "\n";
						oss << "Address (Class): 0x" << std::hex << a->Class << "\n";
						oss << "Mesh: 0x" << a->Mesh << " | BoneArray: 0x" << a->BoneArray << "\n";
						oss << "WorldPos (Vec3): [" << std::dec << pos.x << ", " << pos.y << ", " << pos.z << "]\n";
						oss << "Head3D: [" << a->Head3D.X << ", " << a->Head3D.Y << ", " << a->Head3D.Z << "]\n";
						oss << "Neck3D: [" << a->neck3D.X << ", " << a->neck3D.Y << ", " << a->neck3D.Z << "]\n";
						oss << "Pelvis3D: [" << a->pelvis3D.X << ", " << a->pelvis3D.Y << ", " << a->pelvis3D.Z << "]\n";
						oss << "Distance: " << dist << "m\n";
						oss << "Head Screen (W2S): [" << headScreen.x << ", " << headScreen.y << "]\n";
						oss << "Foot Screen (W2S): [" << footScreen.x << ", " << footScreen.y << "]\n\n";
					}
					
					std::string str = oss.str();
					ImGui::SetClipboardText(str.c_str());
				}
			}
			else if (ActiveTab == 2) {
				ImGui::Text("Captured Actors (First 30)");
				ImGui::Separator();
				if (ImGui::BeginTable("PlayerTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 300))) {
					ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60.0f);
					ImGui::TableSetupColumn("Team", ImGuiTableColumnFlags_WidthFixed, 40.0f);
					ImGui::TableSetupColumn("Health", ImGuiTableColumnFlags_WidthFixed, 50.0f);
					ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 60.0f);
					ImGui::TableSetupColumn("Name");
					ImGui::TableHeadersRow();

					int idx = 0;
					for (auto& actor : frameData.Actors) {
						if (idx++ >= 30) break;
						ImGui::TableNextRow();
						
						ImGui::TableSetColumnIndex(0);
						ImGui::Text("%s", actor->PlayerType == 1 ? "Bot" : (actor->PlayerType == 2 ? "Player" : "Other"));
						
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("%d", actor->LastTeamNum);
						
						ImGui::TableSetColumnIndex(3);
						float dist = Vector3::Distance(Vector3(frameData.CameraCache.POV.Location.X, frameData.CameraCache.POV.Location.Y, frameData.CameraCache.POV.Location.Z), actor->GetPosition()) / 100.0f;
						ImGui::Text("%.0fm", dist);
						
						ImGui::TableSetColumnIndex(4);
						ImGui::Text("%s", actor->objName.c_str());
					}
					ImGui::EndTable();
				}
				ImGui::Spacing();
				ImGui::Text("Check 'Run Drawing Test' in Visuals to see these in-game.");
			}
			else if (ActiveTab == 3) {
				ImGui::Text("Application Logs");
				ImGui::Separator();
				
				std::string logCopy;
				{
					std::lock_guard<std::mutex> lock(GLiveLogsMutex);
					logCopy = GLiveLogs;
				}

				if (logCopy.empty()) {
					logCopy = "Waiting for logs...\n";
				}

				logCopy.reserve(logCopy.size() + 1);
				ImGui::InputTextMultiline("##logs", logCopy.data(), logCopy.capacity(), ImVec2(-FLT_MIN, -FLT_MIN), ImGuiInputTextFlags_ReadOnly);
			}
			else if (ActiveTab == 4) {
				ImGui::Text("Aimbot Settings");
				ImGui::Separator();
				
				ImGui::Checkbox("Enable Aimbot", &Configs.Aimbot.Enabled);
				ImGui::Checkbox("Visibility Check", &Configs.Aimbot.VisibilityCheck);
				ImGui::Checkbox("Auto-Bone Selection", &Configs.Aimbot.AutoBone);

				const char* keys[] = { "Left Alt", "Right Click", "Shift", "Mouse 5", "Mouse 4", "Left Mouse" };
				ImGui::Combo("Toggle Key", &Configs.Aimbot.Hotkey, keys, IM_ARRAYSIZE(keys));

				if (!Configs.Aimbot.AutoBone) {
				    const char* bones[] = { "Head", "Neck", "Chest", "Pelvis" };
				    ImGui::Combo("Target Bone", &Configs.Aimbot.TargetBone, bones, IM_ARRAYSIZE(bones));
				}

				ImGui::SliderFloat("FOV Degrees", &Configs.Aimbot.FOV, 1.0f, 90.0f, "%.1f deg");
				
				ImGui::Spacing();
				ImGui::Text("Recoil Control System (RCS)");
				ImGui::SliderFloat("RCS Vertical (Pitch)", &Configs.Aimbot.AR_RecoilValue, 0.0f, 100.0f, "%.0f%%");
				ImGui::SliderFloat("RCS Horizontal (Yaw)", &Configs.Aimbot.AR_RecoilYaw, 0.0f, 100.0f, "%.0f%%");

				ImGui::Spacing();
				ImGui::Text("Smoothing Algorithms");
				ImGui::SliderFloat("AR Smooth Value", &Configs.Aimbot.AR_SmoothValue, 0.1f, 10.0f, "%.1f");
				ImGui::SliderFloat("Sniper Base Smooth", &Configs.Aimbot.SR_baseSmooth, 0.1f, 10.0f, "%.1f");
				ImGui::SliderInt("Max Delta (Anti-Snap limit)", &Configs.Aimbot.MaxDelta, 1, 50);

				ImGui::Spacing();
				ImGui::Text("Tracking Aggression");
				ImGui::SliderFloat("Aggression", &Configs.Aimbot.Aggression, 0.5f, 3.0f, "%.2f");
				ImGui::SliderFloat("Driver Scale", &Configs.Aimbot.DriverScale, 0.10f, 0.50f, "%.2f");
				ImGui::Checkbox("Lock-On Target", &Configs.Aimbot.LockOnTarget);
			}
		}
		ImGui::End();
	}

	if (RunDrawingTest) {
		float cx = Gui.Window.Size.x / 2.0f;
		float cy = Gui.Window.Size.y / 2.0f;
		Gui.Rectangle(Vec2(cx - 50, cy - 50), Vec2(100, 100), IM_COL32(255, 255, 255, 255), 2.0f);
	}
	if (Configs.Aimbot.Enabled) {
		float cx = Gui.Window.Size.x / 2.0f;
		float cy = Gui.Window.Size.y / 2.0f;
		float fovRad = Configs.Aimbot.FOV * 3.14159265f / 180.0f;
		float radius = std::tan(fovRad * 0.5f) * Gui.Window.Size.y;
		if (radius < 10.0f) radius = 10.0f;
		if (radius > Gui.Window.Size.y * 0.5f) radius = Gui.Window.Size.y * 0.5f;
		Gui.Circle(Vec2(cx, cy), radius, IM_COL32(255, 255, 255, 80), 1.0f, 64);
	}

	extern void DrawPlayerEsp(const SharedData& data);
	DrawPlayerEsp(frameData);
	
	GRadar.Render(frameData);
}

// ============================================================================
// ENTRY POINT
// ============================================================================
int Run() {
	// Console removed for stealth
	// If you need debug output, use OutputDebugStringA or logs
	// AllocConsole();
	// freopen_s(&fDummy, "CONIN$", "r", stdin);
	// freopen_s(&fDummy, "CONOUT$", "w", stderr);
	// freopen_s(&fDummy, "CONOUT$", "w", stdout);

	timeBeginPeriod(1);

	LOG("[*] Setup configuration loader...\n");
	SetUpConfig();

	LOG("[*] Ready | Menu Key: HOME\n");

	if (!InitializeFeatures()) {
		LOG("[-] Initialization FAILED.\n");
		Sleep(5000);
		timeEndPeriod(1);
		return 1;
	}

	std::thread memThread(MemoryLoop);
	memThread.detach();

	// Console already hidden since we don't allocate one
	// ShowWindow(GetConsoleWindow(), SW_HIDE);

	// Initialize Discord overlay — hijack existing overlay window, no modifications
	LOG("[*] Initializing Discord overlay hijack...\n");
	DiscordOverlay::Run([&]() {
		Gui.Window.Size = ImVec2((float)DiscordOverlay::ScreenWidth, (float)DiscordOverlay::ScreenHeight);
		Gui.Window.hWnd = DiscordOverlay::Overlay;
		RenderFrame();
	});

	GIsRunning = false;
	timeEndPeriod(1);
	return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	return Run();
}
