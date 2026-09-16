#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "ConfigUtilities.h"

namespace
{
    class ConfigTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            char* previous = nullptr;
            std::size_t previousLength = 0;
            if (_dupenv_s(&previous, &previousLength, "USERPROFILE") == 0 && previous != nullptr)
            {
                previousUserProfile = previous;
                hadUserProfile = true;
                std::free(previous);
            }

            profile = std::filesystem::temp_directory_path() / "pubgext-config-gtest";
            std::error_code error;
            std::filesystem::remove_all(profile, error);
            ASSERT_EQ(_putenv_s("USERPROFILE", profile.string().c_str()), 0);
        }

        void TearDown() override
        {
            if (hadUserProfile)
                _putenv_s("USERPROFILE", previousUserProfile.c_str());
            else
                _putenv_s("USERPROFILE", "");

            std::error_code error;
            std::filesystem::remove_all(profile, error);
        }

        std::filesystem::path profile;
        std::string previousUserProfile;
        bool hadUserProfile = false;
    };
}

TEST_F(ConfigTest, VendoredJsonParserAcceptsValidAndRejectsInvalidInput)
{
    EXPECT_TRUE(json::accept(R"({"Aimbot":{"Enabled":true}})"));
    EXPECT_FALSE(json::accept(R"({"Aimbot":})"));
}

TEST_F(ConfigTest, SaveAndLoadRoundTripRestoresValues)
{
    Configs.Survivor.Name = false;
    Configs.Survivor.RadarRange = 275.5f;
    Configs.Survivor.TextColour = Colour(12, 34, 56, 78);
    Configs.Overlay.OverrideResolution = true;
    Configs.Overlay.Width = 2560;
    Configs.Overlay.Height = 1440;
    Configs.Aimbot.Enabled = true;
    Configs.Aimbot.FOV = 27.5f;

    SaveConfig(L"roundtrip");

    const auto filePath = profile / "Documents" / "Hunt" / "roundtrip.json";
    ASSERT_TRUE(std::filesystem::exists(filePath));
    std::ifstream file(filePath);
    const std::string contents((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    ASSERT_TRUE(json::accept(contents));

    Configs.Survivor.Name = true;
    Configs.Survivor.RadarRange = 1.0f;
    Configs.Survivor.TextColour = Colour();
    Configs.Overlay.OverrideResolution = false;
    Configs.Overlay.Width = 800;
    Configs.Overlay.Height = 600;
    Configs.Aimbot.Enabled = false;
    Configs.Aimbot.FOV = 50.0f;

    LoadConfig(L"roundtrip");

    EXPECT_FALSE(Configs.Survivor.Name);
    EXPECT_FLOAT_EQ(Configs.Survivor.RadarRange, 275.5f);
    EXPECT_EQ(Configs.Survivor.TextColour.r, 12);
    EXPECT_EQ(Configs.Survivor.TextColour.g, 34);
    EXPECT_EQ(Configs.Survivor.TextColour.b, 56);
    EXPECT_EQ(Configs.Survivor.TextColour.a, 78);
    EXPECT_TRUE(Configs.Overlay.OverrideResolution);
    EXPECT_EQ(Configs.Overlay.Width, 2560);
    EXPECT_EQ(Configs.Overlay.Height, 1440);
    EXPECT_TRUE(Configs.Aimbot.Enabled);
    EXPECT_FLOAT_EQ(Configs.Aimbot.FOV, 27.5f);
}
