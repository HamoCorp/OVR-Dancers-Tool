#include "virtual_controller.h"
#include "pose_hook.h"
#include <cstring>

extern void DLog(const char* fmt, ...);

VirtualController::VirtualController(bool isRight) : m_isRight(isRight) {}
VirtualController::~VirtualController() { Deactivate(); }

std::string VirtualController::GetSerial() const {
    return m_isRight ? "OVRDancers-Right" : "OVRDancers-Left";
}

vr::EVRInitError VirtualController::Activate(uint32_t deviceId) {
    m_deviceId = deviceId;
    DLog("OVRDancers: ghost controller (%s) activated id=%u\n",
         m_isRight ? "R" : "L", deviceId);

    vr::PropertyContainerHandle_t props =
        vr::VRProperties()->TrackedDeviceToPropertyContainer(deviceId);

    vr::VRProperties()->SetStringProperty(props, vr::Prop_SerialNumber_String, GetSerial().c_str());
    vr::VRProperties()->SetStringProperty(props, vr::Prop_ManufacturerName_String, "OVRDancers");
    vr::VRProperties()->SetStringProperty(props, vr::Prop_ModelNumber_String, "OVRDancers Ghost");
    vr::VRProperties()->SetInt32Property(props, vr::Prop_DeviceClass_Int32,
                                         vr::TrackedDeviceClass_Controller);
    vr::VRProperties()->SetInt32Property(props, vr::Prop_ControllerRoleHint_Int32,
        m_isRight ? vr::TrackedControllerRole_RightHand : vr::TrackedControllerRole_LeftHand);

    // Priority -1: always loses hand role to any real controller.
    // Ghost devices are never the "active" controller for a hand — they only provide
    // a visual marker at the real physical controller's location.
    vr::VRProperties()->SetInt32Property(props,
        vr::Prop_ControllerHandSelectionPriority_Int32, -1);

    // Default render model — overwritten by ApplyRealControllerProperties when a mapping is set.
    vr::VRProperties()->SetStringProperty(props, vr::Prop_RenderModelName_String,
        m_isRight ? "{indexcontroller}valve_controller_knu_1_0_right"
                  : "{indexcontroller}valve_controller_knu_1_0_left");

    return vr::VRInitError_None;
}

void VirtualController::Deactivate() {
    m_deviceId = vr::k_unTrackedDeviceIndexInvalid;
}

vr::DriverPose_t VirtualController::GetPose() {
    uint32_t id = m_deviceId.load();
    vr::DriverPose_t pose{};
    if (id != vr::k_unTrackedDeviceIndexInvalid && PoseHook::GetGhostPose(id, pose))
        return pose;
    // No pose pushed yet — report disconnected so SteamVR doesn't place us at origin.
    pose.poseIsValid       = false;
    pose.result            = vr::TrackingResult_Uninitialized;
    pose.deviceIsConnected = false;
    pose.qWorldFromDriverRotation = {1,0,0,0};
    pose.qDriverFromHeadRotation  = {1,0,0,0};
    return pose;
}

void VirtualController::ApplyRealControllerProperties(const std::string& renderModel) {
    uint32_t deviceId = m_deviceId.load();
    if (deviceId == vr::k_unTrackedDeviceIndexInvalid) return;
    vr::PropertyContainerHandle_t props =
        vr::VRProperties()->TrackedDeviceToPropertyContainer(deviceId);
    if (!renderModel.empty())
        vr::VRProperties()->SetStringProperty(props, vr::Prop_RenderModelName_String,
                                              renderModel.c_str());
    DLog("OVRDancers: ghost (%s) render model → '%s'\n",
         m_isRight ? "R" : "L", renderModel.c_str());
}

void VirtualController::UpdateBattery(float pct) {
    uint32_t deviceId = m_deviceId.load();
    if (deviceId == vr::k_unTrackedDeviceIndexInvalid) return;
    vr::PropertyContainerHandle_t props =
        vr::VRProperties()->TrackedDeviceToPropertyContainer(deviceId);
    vr::VRProperties()->SetBoolProperty(props, vr::Prop_DeviceProvidesBatteryStatus_Bool, true);
    vr::VRProperties()->SetFloatProperty(props, vr::Prop_DeviceBatteryPercentage_Float, pct);
}
