# not finished work in progress, Not Working yet almost finished

<img width="100" height="100" alt="logofull size" src="https://github.com/user-attachments/assets/e432d39d-3f82-4f99-b15b-8adb36a840a4" />

# OVR-Dancers-Tool
A tool for dancers, VR bboys and bgirls can quickly switch between use trackers to override controllers or finger tracking for things like **handstands** without cooking your controller. This is a faster working alternative to [OpenVR-Input-Emulator](https://github.com/matzman666/OpenVR-InputEmulator), specifically for dancers to **override** hand-tracker positions and add **bindings** to quickly switch between them.

**Should work for index  and quest controllers and more**

<img width="769" height="571" alt="Capture1" src="https://github.com/user-attachments/assets/5339865f-aabc-4d6e-9daf-348c624d33b6" />

## installation

Install the driver into SteamVR

Copy the `build/driver/` folder into SteamVR's driver directory:

```
C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\00ovrdancers\
```

Structure should be:
```
drivers/ovrdancers/
    driver.vrdrivermanifest
    bin/win64/
        driver_ovrdancers.dll
```

Then, in SteamVR settings → Startup/Shutdown → Manage Add-ons, enable **Dancers Tool**.

Then open OVRDancersTool.exe




## usage
With your controllers and wrist trackers in your hands, select OVRDancersTool from the Steam menu and select add mapping.

<img width="145" height="50" alt="Capture4" src="https://github.com/user-attachments/assets/67d02866-f1fb-41b2-ba09-eea4575ba961" />

Select the tracker and controller you want to override, or use virtual controller. If you want to use only the tracker without a controller, then select add tracker

<img width="453" height="318" alt="Capture7" src="https://github.com/user-attachments/assets/9a94a90c-aed1-46e5-8a5b-569b2b3c7025" />


Press the on button to enable the tracker, then press the quick adjust button while holding your controller to align the tracker to the controller, or manually edit the offsets if you need more offset

<img width="769" height="301" alt="Capture2" src="https://github.com/user-attachments/assets/7cbc7f36-b363-440b-bafa-8c4d9e393eae" />

Then press the save button, so it loads this every time you start VR


<img width="200" height="49" alt="Capture5" src="https://github.com/user-attachments/assets/c38a4c3c-c634-4360-a7eb-bb7b244c16c1" />


## adding bindings

## settings to change

## osc websockets?

## file locations
want to ensure it's completely removed or check it's installed correctly

-   steam vr driver folder `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\00ovrdancers\`
-   Saving and logging data might not show up until running it in SteamVR
-   executable location

## Possible issues and fixes

- Driver not connecting might be caused after a steam crash, addons blocked, in SteamVR settings → Startup/Shutdown → Manage Add-ons, enable **Dancers Tool**.
- vibration not working: Unfortunately, the SteamVR driver SDK doesn't expose a way to trigger haptics on another driver's physical device (TriggerHapticPulse only exists in the client API, not IVRServerDriverHost), so vibration forwarding isn't implementable at this level without deeper OS-level hooking. best to set a binding to switch between trackers when you're holding them.
- might be some issues using the Steam store version of Space Call, try using the [GitHub version](https://github.com/hyblocker/OpenVR-SpaceCalibrator) if your controller appears extremely far away
- Related issue: driver naming can cause an issue if a different drive loads before this one, which takes over the controllers. This is due to the naming order 00 at the start, making it load before Oculus alphabetically

 <img width="367" height="272" alt="Capture" src="https://github.com/user-attachments/assets/747e81cc-1509-4d4e-92b5-730f95fb0646" />




## rebuilding (don't read this, only for nerds or Claude)

- Visual Studio (Desktop C++ workload)
- CMake 3.20+
- Git

Edit this command with your vs version:
`cmake -B build -S . -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release`

Output:
- `build/driver/`  → SteamVR driver DLL
- `build/overlay/` → Overlay app exe

Install the driver into SteamVR

Copy the `build/driver/` folder into SteamVR's driver directory:

```
C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\00ovrdancers\
```

Structure should be:
```
drivers/ovrdancers/
    driver.vrdrivermanifest
    bin/win64/
        driver_ovrdancers.dll
```

Then, in SteamVR settings → Startup/Shutdown → Manage Add-ons, enable **Dancers Tool**.
