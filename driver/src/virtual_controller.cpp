#include "virtual_controller.h"
#include <cstring>

VirtualController::VirtualController(const char* serial, vr::ETrackedControllerRole role)
    : m_role(role)
{
    strncpy_s(m_serial, sizeof(m_serial), serial, _TRUNCATE);
}

vr::EVRInitError VirtualController::Activate(uint32_t unObjectId) {
    m_deviceIndex = unObjectId;

    auto props = vr::VRProperties();
    auto c     = props->TrackedDeviceToPropertyContainer(unObjectId);

    props->SetStringProperty(c, vr::Prop_SerialNumber_String,       m_serial);
    props->SetStringProperty(c, vr::Prop_ModelNumber_String,        "OVRDancers Virtual Controller");
    props->SetStringProperty(c, vr::Prop_ManufacturerName_String,   "OVRDancers");
    props->SetStringProperty(c, vr::Prop_TrackingSystemName_String, "ovrdancers");
    props->SetInt32Property( c, vr::Prop_DeviceClass_Int32,          vr::TrackedDeviceClass_Controller);
    // Start with OptOut so we don't steal the hand role from real controllers.
    // SetRole() is called by DeviceProvider when a mapping is enabled/disabled.
    props->SetInt32Property( c, vr::Prop_ControllerRoleHint_Int32,   (int32_t)vr::TrackedControllerRole_OptOut);
    // Vive controller profile — widely supported and exposes position-only tracking without
    // requiring a specific manufacturer's button binding
    props->SetStringProperty(c, vr::Prop_InputProfilePath_String,
        "{htc}/input/vive_controller_profile.json");
    props->SetStringProperty(c, vr::Prop_ControllerType_String, "vive_controller");
    props->SetStringProperty(c, vr::Prop_RenderModelName_String, "vr_controller_vive_1_5");

    // Register input components — without these SteamVR falls back to "Steam Controller"
    auto* inp = vr::VRDriverInput();
    if (inp) {
        inp->CreateBooleanComponent(c, "/input/grip/click",             &m_grip);
        inp->CreateScalarComponent (c, "/input/trigger/value",          &m_trigger,
            vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided);
        inp->CreateBooleanComponent(c, "/input/application_menu/click", &m_appMenu);
        inp->CreateBooleanComponent(c, "/input/system/click",           &m_system);
        inp->CreateBooleanComponent(c, "/input/trackpad/click",         &m_padClick);
        inp->CreateBooleanComponent(c, "/input/trackpad/touch",         &m_padTouch);
        inp->CreateScalarComponent (c, "/input/trackpad/x",             &m_padX,
            vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
        inp->CreateScalarComponent (c, "/input/trackpad/y",             &m_padY,
            vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
        inp->CreateHapticComponent (c, "/output/haptic",                &m_haptic);
    }

    return vr::VRInitError_None;
}

void VirtualController::Deactivate() {
    m_deviceIndex = vr::k_unTrackedDeviceIndexInvalid;
}

void VirtualController::SetRole(vr::ETrackedControllerRole role) {
    m_role = role;
    uint32_t idx = m_deviceIndex.load();
    if (idx == vr::k_unTrackedDeviceIndexInvalid) return;
    auto props = vr::VRProperties();
    if (!props) return;
    auto c = props->TrackedDeviceToPropertyContainer(idx);
    if (c == vr::k_ulInvalidPropertyContainer) return;
    props->SetInt32Property(c, vr::Prop_ControllerRoleHint_Int32, (int32_t)role);
}

vr::DriverPose_t VirtualController::GetPose() {
    vr::DriverPose_t pose{};
    bool conn = m_connected.load();
    pose.poseIsValid              = conn;
    pose.deviceIsConnected        = conn;
    pose.result                   = conn
        ? vr::TrackingResult_Running_OK
        : vr::TrackingResult_Running_OutOfRange;
    pose.qWorldFromDriverRotation = {1, 0, 0, 0};
    pose.qDriverFromHeadRotation  = {1, 0, 0, 0};
    pose.qRotation                = {1, 0, 0, 0};
    // Position stays at origin — the pose hook overwrites it with tracker+offset
    return pose;
}

void VirtualController::SetConnected(bool connected) {
    m_connected = connected;
    uint32_t idx = m_deviceIndex.load();
    if (idx == vr::k_unTrackedDeviceIndexInvalid) return;
    auto* host = vr::VRServerDriverHost();
    if (host) host->TrackedDevicePoseUpdated(idx, GetPose(), sizeof(vr::DriverPose_t));
}
