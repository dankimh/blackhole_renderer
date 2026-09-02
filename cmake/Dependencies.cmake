# Dependency resolution: prefer installed packages, otherwise FetchContent.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- GLFW ---------------------------------------------------------------
find_package(glfw3 3.3 QUIET)
if(NOT TARGET glfw)
  if(NOT BH_FETCH_DEPS)
    message(FATAL_ERROR "glfw3 not found and BH_FETCH_DEPS=OFF")
  endif()
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glfw)
endif()

# --- glm ----------------------------------------------------------------
find_package(glm QUIET)
if(NOT TARGET glm::glm)
  if(TARGET glm)
    add_library(glm::glm ALIAS glm)
  else()
    FetchContent_Declare(glm
      GIT_REPOSITORY https://github.com/g-truc/glm.git
      GIT_TAG 1.0.1 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glm)
  endif()
endif()

# --- nlohmann_json ------------------------------------------------------
find_package(nlohmann_json QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# --- Vulkan headers + volk (runtime loader, no link-time libvulkan) -----
if(BH_ENABLE_VULKAN)
  find_path(BH_VULKAN_INCLUDE vulkan/vulkan.h
    HINTS $ENV{VULKAN_SDK}/include $ENV{VULKAN_SDK}/Include ${CMAKE_PREFIX_PATH})
  if(NOT BH_VULKAN_INCLUDE)
    FetchContent_Declare(vulkan_headers
      GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
      GIT_TAG v1.3.290 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(vulkan_headers)
    set(BH_VULKAN_INCLUDE ${vulkan_headers_SOURCE_DIR}/include)
  endif()
  set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG vulkan-sdk-1.3.296.0 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(volk)
  target_include_directories(volk_headers INTERFACE ${BH_VULKAN_INCLUDE})
  if(TARGET volk)
    target_include_directories(volk PRIVATE ${BH_VULKAN_INCLUDE})
    set_target_properties(volk PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()
  message(STATUS "Vulkan headers: ${BH_VULKAN_INCLUDE}")
endif()

# --- Dear ImGui (datUI equivalent) --------------------------------------
if(BH_ENABLE_IMGUI)
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.0 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(imgui)
  set(IMGUI_SRC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  if(BH_ENABLE_VULKAN)
    list(APPEND IMGUI_SRC ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
  endif()
  add_library(bh_imgui STATIC ${IMGUI_SRC})
  target_include_directories(bh_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_compile_definitions(bh_imgui PUBLIC
    GLFW_INCLUDE_NONE IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    $<$<BOOL:${BH_ENABLE_VULKAN}>:IMGUI_IMPL_VULKAN_NO_PROTOTYPES IMGUI_IMPL_VULKAN_USE_VOLK>)
  target_link_libraries(bh_imgui PUBLIC glfw)
  if(BH_ENABLE_VULKAN)
    target_link_libraries(bh_imgui PUBLIC volk::volk_headers)
  endif()
endif()

# --- X11 (Linux wallpaper mode + global cursor polling) -----------------
if(UNIX AND NOT APPLE)
  find_library(BH_X11_LIB X11 HINTS ${CMAKE_PREFIX_PATH}/lib)
  find_path(BH_X11_INCLUDE X11/Xlib.h HINTS ${CMAKE_PREFIX_PATH}/include)
  if(BH_X11_INCLUDE)
    include_directories(${BH_X11_INCLUDE})
  endif()
endif()
