#pragma once
#include <vector>
#include <string>
#include "../../../shared/protocol.h"

struct SavedMapping {
    char       controllerSerial[32] = {};
    char       trackerSerial[32]    = {};
    bool       enabled              = false;
    Offset6DOF offset               = {};
};

bool SaveMappings(const std::string& path, const std::vector<SavedMapping>& mappings);
bool LoadMappings(const std::string& path, std::vector<SavedMapping>& mappings);
