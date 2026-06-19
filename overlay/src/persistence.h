#pragma once
#include <vector>
#include <string>
#include "../../../shared/protocol.h"

struct SavedMapping {
    char       controllerSerial[32] = {};
    char       trackerSerial[32]    = {};
    bool       enabled              = false;
    bool       redirectMode         = false;
    uint32_t   side                 = 0; // 1=left, 2=right
    Offset6DOF offset               = {};
};

struct AppSettings {
    bool     virtualControllers = false;
    bool     oscEnabled         = false;
    char     oscIP[64]          = "127.0.0.1";
    uint16_t oscPort            = 9000;
    char     oscParam[64]       = "OVRDancers_Active";
    bool     wsEnabled          = false;
    uint16_t wsPort             = 8080;
    char     wsParam[64]        = "OVRDancers_Active";
    bool     htLocked           = false; // lock hand tracking device (no auto-switching)
    uint8_t  language           = 0;     // 0=EN, 1=JA, 2=KO
};

bool SaveMappings(const std::string& path, const std::vector<SavedMapping>& mappings,
                  const AppSettings& settings = {});
bool LoadMappings(const std::string& path, std::vector<SavedMapping>& mappings,
                  AppSettings* settings = nullptr);
