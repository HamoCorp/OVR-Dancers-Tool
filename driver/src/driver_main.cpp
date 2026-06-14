#include <openvr_driver.h>
#include "device_provider.h"

static DeviceProvider g_provider;

extern "C" __declspec(dllexport)
void* HmdDriverFactory(const char* interfaceName, int* returnCode) {
    if (strcmp(interfaceName, vr::IServerTrackedDeviceProvider_Version) == 0) {
        return &g_provider;
    }
    if (returnCode) *returnCode = vr::VRInitError_Init_InterfaceNotFound;
    return nullptr;
}
