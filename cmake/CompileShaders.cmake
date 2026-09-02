# bh_compile_spirv(<target> SOURCE_DIR <dir> OUTPUT_DIR <dir> SHADERS <files...>)
# Compiles GLSL to SPIR-V with glslc when available. The generated .spv files are
# committed under shaders/spv so a Windows build without the Vulkan SDK still works.
function(bh_compile_spirv target)
  cmake_parse_arguments(ARG "" "SOURCE_DIR;OUTPUT_DIR" "SHADERS" ${ARGN})
  if(NOT BH_COMPILE_SPIRV)
    return()
  endif()
  find_program(BH_GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin $ENV{VULKAN_SDK}/Bin ${CMAKE_PREFIX_PATH}/bin)
  if(NOT BH_GLSLC)
    message(STATUS "glslc not found - using pre-compiled SPIR-V in ${ARG_OUTPUT_DIR}")
    return()
  endif()
  set(outputs)
  foreach(shader ${ARG_SHADERS})
    set(src ${ARG_SOURCE_DIR}/${shader})
    set(out ${ARG_OUTPUT_DIR}/${shader}.spv)
    file(GLOB common ${ARG_SOURCE_DIR}/common/*.glsl)
    add_custom_command(OUTPUT ${out}
      COMMAND ${BH_GLSLC} --target-env=vulkan1.1 -O
              -I ${ARG_SOURCE_DIR} -o ${out} ${src}
      DEPENDS ${src} ${common}
      COMMENT "glslc ${shader}")
    list(APPEND outputs ${out})
  endforeach()
  add_custom_target(${target}_spirv DEPENDS ${outputs})
  add_dependencies(${target} ${target}_spirv)
endfunction()
