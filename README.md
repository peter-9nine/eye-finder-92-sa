# EyeFinder92SA — GTA San Andreas

ASI plugin for GTA San Andreas (1.0 US), built on [plugin-sdk](https://github.com/DK22Pac/plugin-sdk).

Output: `EyeFinder92SA_SA.asi`

## Repository layout

```
EyeFinder92SA.sln                 Visual Studio 2022 solution
EyeFinder92SA.vcxproj(.filters)   Project (configurations: Debug GTASA / Release GTASA, Win32)
Resource.rc, resource.h      Version info and embedded resources
source/                      C++ sources
include/                     Project headers (pch.h)
shaders/                     HLSL pixel shaders (FxCompile -> $(IntDir)generated\<name>_ps_bytecode.h)
lib/                         Extra .lib files (optional)
```

## Requirements

- Visual Studio 2022 (toolset v143), Windows 10 SDK, C++ "latest" standard
- plugin-sdk built, with `PLUGIN_SDK_DIR` pointing to it (`plugin.lib`, `plugin_d.lib`, `CLEO.lib` in `$(PLUGIN_SDK_DIR)\output\lib`)
- Shared helpers (ImGui, nlohmann, magic_enum) at `$(PLUGIN_SDK_DIR)\tools\myplugin-gen\generated\shared`
- DirectX SDK headers/libs at `$(PLUGIN_SDK_DIR)\shared\dxsdk`
- Optional: `GTA_SA_DIR` — post-build copies the `.asi` to `%GTA_SA_DIR%\modloader\EyeFinder92SA_SA`
- Optional (Release): `SIGN_PATH`, `SIGN_CERT`, `SIGN_PASSWORD` — signs the copied `.asi`

## Author

Peter_99
