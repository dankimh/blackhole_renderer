# Windows build (TITAN X desktop)

## Prerequisites

| Component | Notes |
|-----------|-------|
| Visual Studio 2022 | "Desktop development with C++" workload (MSVC v143, Windows 10/11 SDK) |
| CMake 3.22+ | bundled with VS or from cmake.org |
| CUDA Toolkit **12.x** (12.4 - 12.9) | TITAN X (Maxwell sm_52) / TITAN X Pascal & Xp (sm_61) are **dropped in CUDA 13**, so stay on 12.x. Install after Visual Studio so the VS integration is registered. |
| NVIDIA driver | Any recent Game Ready / Studio driver (OpenGL 4.6 + Vulkan 1.1 support) |
| Vulkan SDK (optional) | Only needed to recompile SPIR-V (`glslc`). Pre-compiled `shaders/spv/*.spv` are committed, so it is optional. |
| Git | FetchContent downloads GLFW, glm, nlohmann/json, volk, Vulkan-Headers (if no SDK) and Dear ImGui on first configure. |

## Build

```powershell
git clone <this repo> blackhole_render
cd blackhole_render
cmake --preset windows-msvc          # configure (downloads dependencies)
cmake --build --preset windows-msvc  # Release build -> build-win\Release\blackhole_render.exe
```

Without a CUDA toolkit use the `windows-msvc-nocuda` preset; particles then run on the
GLSL compute shader path (visually identical, slightly slower).

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
