<img width="100" height="100" alt="logofull size" src="https://github.com/user-attachments/assets/e432d39d-3f82-4f99-b15b-8adb36a840a4" />

# OVR-Dancers-Tool
A tool for VR dancers, VR bboys and bgirls to quickly switch between hand trackers as controller for things like **handstands** without cooking your controller. This is a quicker alternative to [Openvr-Input-Emulator](https://github.com/matzman666/OpenVR-InputEmulator), specificly for dancer to **override** hand tracker position and add **bindings** to quickly switch between them.



## installation

Then, in SteamVR settings → Startup/Shutdown → Manage Add-ons, enable **Dancers Tool**.
## feature list

## usage



## Possible issues and fixes

- Driver not connecting might be caused after a steam crash, addons blocked, in SteamVR settings → Startup/Shutdown → Manage Add-ons, enable **Dancers Tool**.
- check it's installed correctly or want to ensure it's completely removed, delete this folder C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\dancerstool\
- might be some issues using the Steam store version of Space Call, try using the [GitHub version](https://github.com/hyblocker/OpenVR-SpaceCalibrator) if your controller appears extremely far away



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
C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\breakerstool\
```

Structure should be:
```
drivers/breakerstool/
    driver.vrdrivermanifest
    bin/win64/
        driver_breakerstool.dll
```

Then, in SteamVR settings → Startup/Shutdown → Manage Add-ons, enable **Dancers Tool**.
