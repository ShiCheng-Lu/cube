# Cross-platform build (Linux / Raspberry Pi, Windows, macOS).
# Build commands for each platform are in README.md.
#
#   cmake -S . -B outputs/build
#   cmake --build outputs/build --config Release
#
# The executable lands in outputs/<config>/ (e.g. outputs/release/cube),
# with compiled shaders in outputs/<config>/shaders/.

cmake_minimum_required(VERSION 3.22)
project(cube LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # for clangd / VS Code IntelliSense

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---- Dependencies -----------------------------------------------------------
# Vulkan: from the system on Linux (libvulkan-dev), from the Vulkan SDK on
# Windows/macOS (sets VULKAN_SDK, which FindVulkan uses).
find_package(Vulkan REQUIRED)

# GLFW and GLM: use the system's copy if there is one (Linux packages),
# otherwise download and build them (Windows/macOS need nothing installed).
include(FetchContent)

find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
    message(STATUS "GLFW not found on the system; fetching it")
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(glfw)
endif()

find_package(glm QUIET)
if(NOT glm_FOUND)
    message(STATUS "GLM not found on the system; fetching it")
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(glm)
endif()
# Older system GLM packages export glm::glm only as "glm"
if(NOT TARGET glm::glm)
    add_library(glm::glm ALIAS glm)
endif()

# ufbx: single-file FBX loader (ufbx.c + ufbx.h), always downloaded
FetchContent_Declare(ufbx
    GIT_REPOSITORY https://github.com/ufbx/ufbx.git
    GIT_TAG        v0.23.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(ufbx)
add_library(ufbx STATIC ${ufbx_SOURCE_DIR}/ufbx.c)
target_include_directories(ufbx PUBLIC ${ufbx_SOURCE_DIR})

# ---- App --------------------------------------------------------------------
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(cube ${SOURCES})

target_include_directories(cube PRIVATE src)
target_link_libraries(cube PRIVATE Vulkan::Vulkan glfw glm::glm ufbx)
target_compile_definitions(cube PRIVATE
    # Radians everywhere, and Vulkan's 0..1 depth range instead of OpenGL's -1..1
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    $<$<CONFIG:Debug>:DEBUG>
    $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>)

if(MSVC)
    target_compile_options(cube PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(cube PRIVATE -Wall -Wextra)
endif()

# outputs/debug, outputs/release, ... for every generator (Visual Studio
# included, which would otherwise add its own Debug/Release subdirectory)
set_target_properties(cube PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/outputs/$<LOWER_CASE:$<CONFIG>>")

# ---- Shaders ----------------------------------------------------------------
# GLSL under shaders/ is compiled to SPIR-V and copied to <exe dir>/shaders/
file(GLOB_RECURSE SHADER_SOURCES CONFIGURE_DEPENDS
    shaders/*.vert shaders/*.frag shaders/*.comp shaders/*.geom)

if(SHADER_SOURCES)
    if(Vulkan_GLSLC_EXECUTABLE)
        set(SHADER_COMPILER ${Vulkan_GLSLC_EXECUTABLE})
        set(SHADER_ARGS)
    elseif(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
        set(SHADER_COMPILER ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE})
        set(SHADER_ARGS -V)
    else()
        message(FATAL_ERROR "No GLSL compiler found (glslc or glslangValidator); "
                            "install glslang-tools or the Vulkan SDK")
    endif()

    set(SPV_DIR "${CMAKE_BINARY_DIR}/shaders")
    set(SPV_FILES)
    foreach(src IN LISTS SHADER_SOURCES)
        file(RELATIVE_PATH rel "${PROJECT_SOURCE_DIR}/shaders" "${src}")
        set(spv "${SPV_DIR}/${rel}.spv")
        get_filename_component(spv_dir "${spv}" DIRECTORY)
        add_custom_command(
            OUTPUT  "${spv}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${spv_dir}"
            COMMAND ${SHADER_COMPILER} ${SHADER_ARGS} "${src}" -o "${spv}"
            DEPENDS "${src}"
            COMMENT "Compiling shader ${rel}"
            VERBATIM)
        list(APPEND SPV_FILES "${spv}")
    endforeach()

    add_custom_target(shaders DEPENDS ${SPV_FILES})
    add_dependencies(cube shaders)
    add_custom_command(TARGET cube POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${SPV_DIR}" "$<TARGET_FILE_DIR:cube>/shaders"
        VERBATIM)
endif()
