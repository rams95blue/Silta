# Building the mod DLL

This is a Visual Studio (MSVC, Win32/x86) project. It must be built on Windows.

## Requirements
- **Visual Studio 2022** (toolset **v143**) with the *Desktop development with C++* workload.
- **DirectX SDK (June 2010)** — required. The functional-camera feature uses
  `d3dx9.lib` / `D3DXSaveTextureToFile`, which only ships in this legacy SDK, and the project
  references `$(DXSDK_DIR)`. The modern Windows SDK does **not** satisfy this.

## Steps
1. **Install the DirectX SDK (June 2010)** from Microsoft.
   - If the installer fails with error **S1023**, a newer *Visual C++ 2010 Redistributable* is
     already installed. Uninstall both the x86 and x64 "Microsoft Visual C++ 2010 Redistributable"
     entries, install the SDK, then reinstall the redistributables.
   - The installer sets the `DXSDK_DIR` environment variable automatically.
2. Open **`FloorbInfraMod.sln`** in Visual Studio. If you installed the SDK while VS was open,
   restart VS so it picks up `DXSDK_DIR`.
3. Set the configuration to **Release** and the platform to **Win32 (x86)**.
   - This matters: INFRA is a 32-bit process, and only the **Release|Win32** config is wired to emit
     the correctly-named DLL. The Debug and x64 configs produce the wrong filename and/or
     architecture.
4. Build (**Build → Build Solution**, or Ctrl+Shift+B).
5. Output: **`xinput1_3_wrapper\Release\silta.dll`**.

## Install the result
1. Copy the built `silta.dll` into the `infra/` folder (the one containing
   `gameinfo.txt`). The included `.vdf` uses the bare `"file"` value `silta`, so
   Source resolves the DLL at the `infra/` root.
2. Put `silta.vdf` (included in this repo under `addons/`) into `infra/addons/`,
   so you have `infra/silta.dll` and `infra/addons/silta.vdf`.
   The VDF is what tells Source to load the plugin. (To keep the DLL inside `addons/` instead, set the
   VDF `"file"` value back to `addons/silta`.)
3. Remove any other conflicting `xinput1_3.dll` (e.g. fehc's original Success Counters DLL).
4. *(Optional)* copy `mapdata.txt` next to `infra.exe`.
5. Launch INFRA and press **Insert** to toggle the overlays.

## Troubleshooting
| Symptom | Cause / fix |
| --- | --- |
| Linker can't find `d3dx9.lib` | `DXSDK_DIR` not set — reinstall the SDK, restart VS. |
| Compiler can't find `d3dx9.h` | Same root cause as above. |
| DirectX SDK installer error **S1023** | Uninstall newer VC++ 2010 redistributables first (see step 1). |
| Output DLL has the wrong name | You built Debug or x64 — switch to **Release \| Win32**. |
