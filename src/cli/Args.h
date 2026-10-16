#pragma once

#include "sim/SimParams.h"

#include <string>
#include <utility>
#include <vector>

namespace phys::cli {

struct Args {
    uint32_t width = 1024;       // simulation size
    uint32_t height = 1024;
    uint32_t outWidth = 0;       // output size (0 = simulation size)
    uint32_t outHeight = 0;
    uint32_t frames = 60;
    uint32_t every = 0;          // write every Nth frame (0 = last frame only)
    uint32_t bits = 8;
    std::string outDir = "physarum-out";
    bool images = true;
    bool selfcheck = false;
    uint32_t snapshotCheck = 0;  // snapshot interval K (0 = off)
    uint32_t cacheMb = 512;
    bool warp = false;
    bool debugLayer = false;
    bool help = false;
    std::vector<std::pair<std::string, std::string>> settings; // key=value, applied in order
};

// Parses argv. Returns false and fills error on bad input.
bool ParseArgs(int argc, char** argv, Args& args, std::string& error);

// Reads a flat JSON object ({"key": value, ...}) and appends its pairs to settings.
bool LoadJsonSettings(const std::string& path, std::vector<std::pair<std::string, std::string>>& settings,
                      std::string& error);

// Applies one setting to params. Angles are in degrees, spawn centre in 0..1 of the frame.
bool ApplySetting(SimParams& p, const std::string& key, const std::string& value, std::string& error);

void PrintUsage();
void PrintParams(const SimParams& p);

} // namespace phys::cli
