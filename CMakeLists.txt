cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# Black Hole Renderer - live wallpaper (OpenGL compute / Vulkan / CUDA interop)
# Structure mirrors rocksdanister/rain:  LivelyInfo.json, LivelyProperties.json,
# shaders/, src/ (== js/), assets/, media/.
# ---------------------------------------------------------------------------
project(blackhole_render VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(BH_ENABLE_VULKAN   "Build the Vulkan render backend"                 ON)
option(BH_ENABLE_CUDA     "Build the CUDA particle backend (GL/VK interop)" ON)
option(BH_ENABLE_IMGUI    "Build the Dear ImGui debug panel (datUI)"        ON)
option(BH_ENABLE_HEADLESS "Build headless (EGL / no-swapchain) render mode" ON)
option(BH_FETCH_DEPS      "Fetch missing dependencies with FetchContent"    ON)
option(BH_COMPILE_SPIRV   "Compile GLSL -> SPIR-V at build time (needs glslc)" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(BH_SOURCES
  src/main.cpp
  src/app/Application.cpp
  src/app/Settings.cpp
  src/app/LivelyIpc.cpp
  src/app/DebugUI.cpp
  src/platform/Window.cpp
  src/platform/Input.cpp
  src/platform/Wallpaper.cpp
  src/util/Log.cpp
  src/util/File.cpp
  src/util/Image.cpp
  src/render/Camera.cpp
  src/render/gl/GLLoader.cpp
  src/render/gl/GLShader.cpp
  src/render/gl/GLRenderer.cpp
  src/render/gl/GLParticles.cpp
)
if(BH_ENABLE_HEADLESS AND NOT WIN32)
  list(APPEND BH_SOURCES src/render/gl/EglContext.cpp)
endif()
if(BH_ENABLE_VULKAN)
  list(APPEND BH_SOURCES
    src/render/vk/VkRenderer.cpp
    src/render/vk/VkContext.cpp
    src/render/vk/VkParticles.cpp)
endif()
if(BH_ENABLE_CUDA)
  list(APPEND BH_SOURCES
    src/render/cuda/CudaParticles.cu
    src/render/cuda/CudaInterop.cpp)
endif()

add_executable(blackhole_render ${BH_SOURCES})
target_include_directories(blackhole_render PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

target_compile_definitions(blackhole_render PRIVATE
  BH_VERSION="${PROJECT_VERSION}"
  BH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  $<$<BOOL:${BH_ENABLE_VULKAN}>:BH_ENABLE_VULKAN=1>
  $<$<BOOL:${BH_ENABLE_CUDA}>:BH_ENABLE_CUDA=1>
  $<$<BOOL:${BH_ENABLE_IMGUI}>:BH_ENABLE_IMGUI=1>
  $<$<BOOL:${BH_ENABLE_HEADLESS}>:BH_ENABLE_HEADLESS=1>
  GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL GLFW_INCLUDE_NONE
  $<$<BOOL:${WIN32}>:NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS GLFW_EXPOSE_NATIVE_WIN32>
  $<$<BOOL:${BH_ENABLE_VULKAN}>:VK_NO_PROTOTYPES>
  $<$<AND:$<BOOL:${BH_ENABLE_VULKAN}>,$<BOOL:${WIN32}>>:VK_USE_PLATFORM_WIN32_KHR>
)

target_link_libraries(blackhole_render PRIVATE
  glfw glm::glm nlohmann_json::nlohmann_json ${CMAKE_DL_LIBS})
if(BH_ENABLE_VULKAN)
  target_link_libraries(blackhole_render PRIVATE volk::volk_headers)
endif()
if(BH_ENABLE_IMGUI)
  target_link_libraries(blackhole_render PRIVATE bh_imgui)
endif()
if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(blackhole_render PRIVATE Threads::Threads)
  if(BH_X11_LIB)
    target_compile_definitions(blackhole_render PRIVATE BH_HAS_X11=1)
    target_link_libraries(blackhole_render PRIVATE ${BH_X11_LIB})
  endif()
endif()
if(MSVC)
  target_compile_options(blackhole_render PRIVATE /W3 /utf-8 /MP)
  # Wallpaper process should not pop a console window in Release.
  set_target_properties(blackhole_render PROPERTIES
    LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
else()
  target_compile_options(blackhole_render PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# CUDA
# ---------------------------------------------------------------------------
if(BH_ENABLE_CUDA)
  enable_language(CUDA)
  find_package(CUDAToolkit REQUIRED)
  if(NOT CMAKE_CUDA_ARCHITECTURES)
    # Maxwell TITAN X (5.2), Pascal TITAN X/Xp (6.1), Turing/Ampere/Ada + PTX.
    set(CMAKE_CUDA_ARCHITECTURES 52 61 75 86 89)
  endif()
  set_target_properties(blackhole_render PROPERTIES
    CUDA_STANDARD 17 CUDA_SEPARABLE_COMPILATION OFF CUDA_RESOLVE_DEVICE_SYMBOLS ON)
  target_link_libraries(blackhole_render PRIVATE CUDA::cudart_static)
  target_compile_options(blackhole_render PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr -Xcudafe --diag_suppress=20012>)
  if(NOT WIN32)
    # cuda_gl_interop.h wants <GL/gl.h>; use the glcorearb-based stub if Mesa headers are absent.
    find_path(BH_SYSTEM_GL_H GL/gl.h)
    if(NOT BH_SYSTEM_GL_H)
      set_source_files_properties(src/render/cuda/CudaInterop.cpp PROPERTIES
        INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/gl_stub)
    endif()
  endif()
endif()

# ---------------------------------------------------------------------------
# Shaders: GLSL is consumed at runtime by the GL backend; SPIR-V for Vulkan.
# ---------------------------------------------------------------------------
include(CompileShaders)
bh_compile_spirv(blackhole_render
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders
  OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spv
  SHADERS blackhole.comp particles_sim.comp bloom.comp particles.vert particles.frag
          composite.vert composite.frag)

# Runtime layout: <bin>/shaders, <bin>/LivelyProperties.json ... (like rain's flat folder).
# Runs on every build so shader-only edits reach the executable directory.
add_custom_target(blackhole_render_assets ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:blackhole_render>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/LivelyProperties.json
          ${CMAKE_CURRENT_SOURCE_DIR}/LivelyInfo.json
          $<TARGET_FILE_DIR:blackhole_render>
  COMMENT "Copying shaders and Lively metadata next to the executable")
add_dependencies(blackhole_render_assets blackhole_render)
if(TARGET blackhole_render_spirv)
  add_dependencies(blackhole_render_assets blackhole_render_spirv)
endif()

install(TARGETS blackhole_render RUNTIME DESTINATION .)
install(DIRECTORY shaders assets media DESTINATION .)
install(FILES LivelyInfo.json LivelyProperties.json README.md License.txt DESTINATION .)
