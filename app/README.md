# Cube app

Renderer in C++20 using Vulkan, GLFW and GLM. Builds with CMake on Linux, the
Raspberry Pi, Windows and macOS.

- Sources: every `.cpp` under `src/` is built (new files are picked up automatically).
- Shaders: GLSL under `shaders/` (`.vert`, `.frag`, `.comp`, `.geom`) is compiled to
  SPIR-V and copied to `shaders/` next to the executable.
- Output: `outputs/release/` or `outputs/debug/` (git-ignored).

## Linux / Raspberry Pi / WSL

Install the tools and libraries (once):

```
sudo apt install -y g++ make cmake git pkg-config libvulkan-dev vulkan-tools \
    glslang-tools vulkan-validationlayers mesa-vulkan-drivers libglfw3-dev libglm-dev
```

Configure (once per build type), then build with `make`:

```
cmake -S . -B outputs/build-release -DCMAKE_BUILD_TYPE=Release
make -C outputs/build-release -j      # outputs/release/cube

cmake -S . -B outputs/build-debug -DCMAKE_BUILD_TYPE=Debug
make -C outputs/build-debug -j        # outputs/debug/cube
```

Run from the output directory, so the app finds `shaders/`:

```
cd outputs/release && ./cube
```

Clean with `rm -rf outputs`.

## Windows

Needs Visual Studio 2022 with C++ and the Vulkan SDK. GLFW and GLM are
downloaded by CMake.

```
winget install Kitware.CMake KhronosGroup.VulkanSDK
```

Open a new terminal after installing the Vulkan SDK so `VULKAN_SDK` is set. Then:

```
cmake -S . -B outputs/build
cmake --build outputs/build --config Release   # outputs/release/cube.exe
cmake --build outputs/build --config Debug     # outputs/debug/cube.exe
```

The `app` folder can also be opened directly in Visual Studio, or in VS Code with
the CMake Tools extension.

## macOS

Install the Vulkan SDK (it includes MoltenVK), then use the same `cmake` commands
as Windows. The app has to enable the Vulkan portability enumeration extension
to find the GPU on macOS.
