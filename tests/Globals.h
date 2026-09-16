#pragma once

#include <cstdint>

// Camera.cpp only needs the camera offsets when compiling its driver-backed
// accessor.  The W2S tests never call that accessor, so keep this test target
// independent from the runtime's global/driver state.
struct CameraTestOffsets
{
    std::uint32_t CameraFov = 0xA2C;
    std::uint32_t CameraRot = 0xA10;
    std::uint32_t CameraPos = 0xA30;
};

inline CameraTestOffsets SDK;
