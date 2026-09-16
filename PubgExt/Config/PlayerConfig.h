#pragma once
#include "pch.h"
class PlayerConfig
{
	std::string ConfigName;

public:
	PlayerConfig(const std::string& name)
	{
		ConfigName = name;
	}
    bool Name = true;
    bool Distance = true;
	bool Box = true;
	bool Skeleton = false;
	bool Health = true;
	bool Radar = false;
	bool Prediction = false;
	float RadarSize = 200.0f;
	float RadarRange = 150.0f;
	int MaxDistance = 1000;
	int MemoryUpdateRate = 144;
	Colour TextColour = Colour(255, 255, 255);
	int FontSize = 11;
    void ToJsonColour(json* j, const std::string& name, Colour* colour)
    {
        (*j)[ConfigName][name][LIT("r")] = colour->r;
        (*j)[ConfigName][name][LIT("g")] = colour->g;
        (*j)[ConfigName][name][LIT("b")] = colour->b;
        (*j)[ConfigName][name][LIT("a")] = colour->a;

    }
    void FromJsonColour(json j, const std::string& name, Colour* colour)
    {
        if (j[ConfigName].contains(name))
        {
            colour->r = j[ConfigName][name][LIT("r")];
            colour->g = j[ConfigName][name][LIT("g")];
            colour->b = j[ConfigName][name][LIT("b")];
            colour->a = j[ConfigName][name][LIT("a")];
        }
    }

    json ToJson()
    {
        json j;
        j[ConfigName][LIT("Name")] = Name;
        j[ConfigName][LIT("Distance")] = Distance;
        j[ConfigName][LIT("Box")] = Box;
        j[ConfigName][LIT("Skeleton")] = Skeleton;
        j[ConfigName][LIT("Health")] = Health;
        j[ConfigName][LIT("Radar")] = Radar;
        j[ConfigName][LIT("Prediction")] = Prediction;
        j[ConfigName][LIT("RadarSize")] = RadarSize;
        j[ConfigName][LIT("RadarRange")] = RadarRange;
        j[ConfigName][LIT("FontSize")] = FontSize;
        j[ConfigName][LIT("MaxDistance")] = MaxDistance;
        j[ConfigName][LIT("MemoryUpdateRate")] = MemoryUpdateRate;
        ToJsonColour(&j, LIT("TextColour"), &TextColour);

        return j;
    }
    void FromJson(const json& j)
    {
        if (!j.contains(ConfigName))
            return;
        if (j[ConfigName].contains(LIT("Name")))
            Name = j[ConfigName][LIT("Name")];
        if (j[ConfigName].contains(LIT("Distance")))
            Distance = j[ConfigName][LIT("Distance")];
        if (j[ConfigName].contains(LIT("Box")))
            Box = j[ConfigName][LIT("Box")];
        if (j[ConfigName].contains(LIT("Skeleton")))
            Skeleton = j[ConfigName][LIT("Skeleton")];
        if (j[ConfigName].contains(LIT("Health")))
            Health = j[ConfigName][LIT("Health")];
        if (j[ConfigName].contains(LIT("Radar")))
            Radar = j[ConfigName][LIT("Radar")];
        if (j[ConfigName].contains(LIT("Prediction")))
            Prediction = j[ConfigName][LIT("Prediction")];
        if (j[ConfigName].contains(LIT("RadarSize")))
            RadarSize = j[ConfigName][LIT("RadarSize")];
        if (j[ConfigName].contains(LIT("RadarRange")))
            RadarRange = j[ConfigName][LIT("RadarRange")];
        if (j[ConfigName].contains(LIT("FontSize")))
            FontSize = j[ConfigName][LIT("FontSize")];
        if (j[ConfigName].contains(LIT("MaxDistance")))
            MaxDistance = j[ConfigName][LIT("MaxDistance")];
        if (j[ConfigName].contains(LIT("MemoryUpdateRate")))
            MemoryUpdateRate = j[ConfigName][LIT("MemoryUpdateRate")];
        FromJsonColour(j, LIT("TextColour"), &TextColour);
    }
};

