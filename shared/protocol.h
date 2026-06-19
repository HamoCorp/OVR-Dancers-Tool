#pragma once
#include <cstdint>
#include <cstring>

// IPC transport name — named pipe on Windows, Unix domain socket on Linux
#ifdef _WIN32
#define OVRDANCERS_PIPE_NAME R"(\\.\pipe\OVRDancers)"
#else
#define OVRDANCERS_PIPE_NAME "/tmp/ovrdancers.sock"
#endif

// Max devices we track
#define MAX_DEVICES 16

enum class MsgType : uint32_t {
    // Overlay -> Driver
    SetMapping      = 1,   // Bind a tracker to a controller
    ClearMapping    = 2,
    SetOffset       = 3,   // Update 6DOF offset for a mapping
    EnableMapping   = 4,
    DisableMapping  = 5,
    RequestState    = 6,   // Ask driver to send full state back
    InputUpdate     = 7,   // Forward real-controller button/axis state to virtual device
    HideSettings    = 8,   // Toggle render model visibility for controllers/trackers
    CreateVirtualControllers = 9, // Register VirtCtrl_L and VirtCtrl_R in SteamVR (one-shot)
    SetVirtCtrlActive        = 10,// Connect or disconnect a virtual controller (side: 1=L,2=R)
    // Driver -> Overlay
    StateUpdate     = 100, // Full state snapshot
    DeviceListUpdate= 101, // Tracked device list changed
};

struct Vec3 {
    float x, y, z;
};

struct Quat {
    float w, x, y, z;
};

// 6DOF offset: position + rotation applied to tracker pose before using as controller pose
struct Offset6DOF {
    Vec3 pos   = {0, 0, 0};
    Quat rot   = {1, 0, 0, 0}; // identity quaternion — tracker-local rotation
    Quat calibTrackerRot = {1, 0, 0, 0}; // tracker world rotation at calibration time
    // World-space transform applied after the tracker→controller position is computed.
    // Use for global position/orientation correction independent of calibration.
    Vec3 originOffset = {0, 0, 0};
    Quat originRot    = {1, 0, 0, 0};
};

struct DeviceMapping {
    uint32_t  controllerIndex = 0xFFFFFFFF; // vr device index
    uint32_t  trackerIndex    = 0xFFFFFFFF;
    bool      enabled         = false;
    char      controllerSerial[32] = {};
    char      trackerSerial[32]    = {};
    Offset6DOF offset;
    uint32_t  side            = 0; // 1=left, 2=right (TrackedControllerRole values)
    uint32_t  virtDevIdx      = 0xFFFFFFFF; // ghost virtual controller device index
    bool      redirectMode    = false; // true = use redirect-source (OVRIE) method
};

// Describes one tracked device as seen by the overlay
struct TrackedDeviceInfo {
    uint32_t  index            = 0xFFFFFFFF;
    char      serial[32]       = {};
    char      modelNumber[64]  = {};
    uint32_t  deviceClass      = 0; // vr::ETrackedDeviceClass
    bool      connected        = false;
};

// --- Message frames ---
// Every message on the pipe starts with MsgHeader, followed by payload.

struct MsgHeader {
    MsgType  type;
    uint32_t payloadSize;
};

struct Msg_SetMapping {
    DeviceMapping mapping;
};

struct Msg_ClearMapping {
    uint32_t controllerIndex;
};

struct Msg_SetOffset {
    uint32_t controllerIndex;
    Offset6DOF offset;
};

struct Msg_EnableDisable {
    uint32_t controllerIndex;
};

struct Msg_StateUpdate {
    uint32_t      mappingCount;
    DeviceMapping mappings[MAX_DEVICES];
    uint32_t      deviceCount;
    TrackedDeviceInfo devices[MAX_DEVICES * 2]; // controllers + trackers
};

struct Msg_DeviceListUpdate {
    uint32_t      deviceCount;
    TrackedDeviceInfo devices[MAX_DEVICES * 2];
};

// Raw legacy controller state forwarded from the overlay to the driver.
// The driver pushes these values into the virtual controller's input components
// so games receive button/trigger/axis events.
struct Msg_HideSettings {
    uint8_t hideControllers; // 1 = hide physical controllers, 0 = show
    uint8_t hideTrackers;    // 1 = hide tracker pucks, 0 = show
    uint8_t _pad[6];
};

struct Msg_SetVirtCtrlActive {
    uint8_t side;   // 1=left, 2=right
    uint8_t active; // 1=connect, 0=disconnect
    uint8_t _pad[6];
};

struct Msg_InputUpdate {
    uint32_t controllerIndex; // real device index (same as DeviceMapping::controllerIndex)
    uint32_t _pad;            // keep uint64_t aligned
    uint64_t ulButtonPressed;
    uint64_t ulButtonTouched;
    float    axisX[5];        // rAxis[0..4].x
    float    axisY[5];        // rAxis[0..4].y
}; // sizeof = 64
