#include <gtest/gtest.h>

#include "pch.h"
#include "Camera.h"
#include "OS-ImGui.h"

// Camera.cpp only needs these operations from the runtime for the pure W2S
// paths exercised here.  Keeping them local to the test target prevents unit
// tests from linking to or initializing the driver transport.
Memory::Memory() {}
Memory::~Memory() {}
bool Memory::BatchRead(DriverInterfaceV3::BatchReadEntry*, size_t) const
{
    return false;
}

namespace RenderCore
{
    void OSImGui_External::NewWindow(std::string, Vec2, std::function<void()>) {}
    void OSImGui_External::MainLoop() {}
}

namespace
{
    MinimalViewInfo IdentityView()
    {
        MinimalViewInfo view{};
        view.FOV = 90.0f;
        return view;
    }
}

TEST(CameraTest, ProjectsForwardPointToScreenCentre)
{
    Gui.Window.Size = Vec2(1920.0f, 1080.0f);

    const Vector2 screen = Camera::WorldToScreen(IdentityView(), Vector3(10.0f, 0.0f, 0.0f));

    EXPECT_FLOAT_EQ(screen.x, 960.0f);
    EXPECT_FLOAT_EQ(screen.y, 540.0f);
}

TEST(CameraTest, ProjectsOffsetPointUsingIdentityRotation)
{
    Gui.Window.Size = Vec2(1920.0f, 1080.0f);

    const Vector2 screen = Camera::WorldToScreen(
        IdentityView(), Vector3(10.0f, 10.0f, 5.0f));

    EXPECT_FLOAT_EQ(screen.x, 1920.0f);
    EXPECT_FLOAT_EQ(screen.y, 60.0f);
}

TEST(CameraTest, RejectsPointBehindCamera)
{
    Gui.Window.Size = Vec2(1920.0f, 1080.0f);

    EXPECT_EQ(Camera::WorldToScreen(IdentityView(), Vector3(-1.0f, 0.0f, 0.0f)),
              Vector2::Zero());
}
