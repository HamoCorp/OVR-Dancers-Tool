<img width="100" height="100" alt="logofull size" src="https://github.com/user-attachments/assets/e432d39d-3f82-4f99-b15b-8adb36a840a4" />

# OVR-Dancers-Tool
A tool for VR dancers, VR bboys and bgirls to quickly switch between hand trackers as controller for things like **handstands** without cooking your controller. This is a quicker alternative to [Openvr-Input-Emulator](https://github.com/matzman666/OpenVR-InputEmulator), specifically for dancers to **override** hand tracker position and add **bindings** to quickly switch between them.



## installation

Then, in SteamVR settings → Startup/Shutdown → Manage Add-ons, enable **Dancers Tool**.


## usage
With your controllers and wrist trackers in your hands, select OVR Dancers tool from the Steam menu and select add mapping.

Select the tracker and controller you want to override, or use virtual controller. If you want to use only the tracker without a controller, then select add tracker

Press the on button to enable the tracker, then press the quick adjust button while holding your controller to align the tracker to the controller, or manually edit the offsets if you need more offset

Then press the save button, so it loads this every time you start VR

## adding bindings

## settings to change

## osc websockets?


## Possible issues and fixes

- Driver not connecting might be caused after a steam crash, addons blocked, in SteamVR settings → Startup/Shutdown → Manage Add-ons, enable **Dancers Tool**.
- check it's installed correctly or want to ensure it's completely removed, delete this folder C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\00ovrdancers\
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
