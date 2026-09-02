# Windows build (TITAN X desktop)

## Prerequisites

| Component | Notes |
|-----------|-------|
| Visual Studio 2026 | "Desktop development with C++" workload (MSVC v145 toolset, Windows 10/11 SDK). CMake 3.22 does not know the "Visual Studio 18 2026" generator - use the CMake bundled with VS 2026 or CMake 4.x from cmake.org. |
| CMake 4.1+ | bundled with VS 2026 (or from cmake.org); required for the VS 2026 generator |
| CUDA Toolkit **12.x** (12.8 or 12.9) | TITAN X (Maxwell sm_52) / TITAN X Pascal & Xp (sm_61) are **dropped in CUDA 13**, so stay on 12.x. Install after Visual Studio so the VS integration is registered. Older 12.x releases do not recognise the VS 2026 (v145) toolset. |
| NVIDIA driver | Any recent Game Ready / Studio driver (OpenGL 4.6 + Vulkan 1.1 support) |
| Vulkan SDK (optional) | Only needed to recompile SPIR-V (`glslc`). Pre-compiled `shaders/spv/*.spv` are committed, so it is optional. |
| Git | FetchContent downloads GLFW, glm, nlohmann/json, volk, Vulkan-Headers (if no SDK) and Dear ImGui on first configure. |

## Build

### CUDA 12.x and the VS 2026 toolset (read first)

CUDA 12.9's `cudafe++` crashes (`died with status 0xC0000005`) when the host compiler is
MSVC 14.5x (VS 2026, `v145`). `-allow-unsupported-compiler` does not help. Install the
**v143 (VS 2022) toolset** inside VS 2026 and build with it:

1. Visual Studio Installer -> Modify -> *Individual components* -> check
   **"MSVC v143 - VS 2022 C++ x64/x86 build tools (Latest)"** -> Modify.
2. `cmake --preset windows-msvc-v143 && cmake --build --preset windows-msvc-v143`
   (-> `build-win-v143\Release\blackhole_render.exe`).

The preset passes `toolset v143` so every translation unit (C++ and CUDA) uses the 14.4x
compiler that CUDA 12.x supports. The `windows-msvc` / `windows-ninja` presets use the
default v145 toolset and only work once NVIDIA ships a CUDA release that supports it.

### Option A - Ninja (VS 2026 default toolset; needs a CUDA that supports v145)

Open **"x64 Native Tools Command Prompt for VS 2026"** (or *Developer PowerShell for VS
2026*) so that `cl.exe` is on the PATH, then:

```powershell
cd blackhole_render
cmake --preset windows-ninja          # configure (downloads dependencies)
cmake --build --preset windows-ninja  # -> build-win-ninja\blackhole_render.exe
```

Ninja ships with Visual Studio (C++ CMake tools component); if `ninja` is not found,
install it with `winget install Ninja-build.Ninja`. This path calls `nvcc.exe` directly and
does not need the CUDA Visual Studio (MSBuild) integration. `-allow-unsupported-compiler`
is passed to nvcc because CUDA 12.x releases predate the VS 2026 (v145) toolset.

### Option B - Visual Studio solution

```powershell
cmake --preset windows-msvc          # generator "Visual Studio 18 2026"
cmake --build --preset windows-msvc  # -> build-win\Release\blackhole_render.exe
```

If configure stops with **`No CUDA toolset found`**, the CUDA MSBuild integration is not
registered for VS 2026. Fix it by copying the four `CUDA 12.x.props/targets/xml` files from

```
C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.x\extras\visual_studio_integration\MSBuildExtensions\
```

into

```
C:\Program Files\Microsoft Visual Studio\18\<Edition>\MSBuild\Microsoft\VC\v180\BuildCustomizations\
```

(create the folder if missing), delete `build-win\CMakeCache.txt`, and re-run the preset.
Alternatively use Option A, or the `windows-msvc-nocuda` preset (particles then run on the
GLSL compute shader path - visually identical, slightly slower).

The post-build step copies `shaders/`, `LivelyInfo.json` and `LivelyProperties.json`
next to the executable, so `build-win\Release\` is a self-contained wallpaper folder.

## Run

```powershell
build-win\Release\blackhole_render.exe --debug              # window + tuning panel
build-win\Release\blackhole_render.exe --wallpaper          # behind the desktop icons (no Lively needed)
build-win\Release\blackhole_render.exe --backend vk --debug # Vulkan backend
```

Stop a `--wallpaper` instance from Task Manager or with `taskkill /im blackhole_render.exe`.

## Lively Wallpaper

1. Copy `build-win\Release\` (exe + shaders + json files + assets) into a folder.
2. In Lively: **Add wallpaper -> select `LivelyInfo.json`** (or drag the folder). `Type` is `0`
   (application); Lively launches `blackhole_render.exe --lively`, waits for the main
   window and re-parents it behind the desktop icons.
3. Enable **Settings -> Wallpaper -> Input forwarding** if you want cursor parallax /
   particles while another window has focus. The app also polls the OS cursor itself
   (`globalMouse` property), so this is optional.
4. Tuning: edit `LivelyProperties.json` in the wallpaper folder - the app hot-reloads the
   file within 0.5 s. Set `"debug": true` for the live panel and press *Save* to write
   the current values back.

## Troubleshooting

* **Black window / "Shader compile failed"** - the driver must expose OpenGL 4.6 with
  `GL_ARB_compute_shader`; update the NVIDIA driver.
* **`vkCreateInstance: INCOMPATIBLE_DRIVER`** - use `--backend gl` or reinstall the driver.
* **CUDA unavailable** - the log prints `CUDA requested but unavailable`; the GLSL
  compute path is used automatically.
* **Low FPS** - lower `displayScaling`, `samples` or `maxSteps`; the ray marcher cost is
  `pixels x samples x steps`.
