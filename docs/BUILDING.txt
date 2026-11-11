# Cross-platform build

The game uses OpenGL 3.3 and GLFW 3.4. GLFW and GLEW are compiled from the source
included in `third_party/`; they are linked statically, so the game does not need
`glfw3.dll`, `glew32.dll`, or `freeglut.dll` at runtime. OpenGL itself is supplied by
the operating system and its graphics driver.

## Windows

Install Visual Studio 2022 with the C++ desktop workload and its CMake component,
or install CMake 3.20 or newer and add it to `PATH`. The existing `SimpleGame.sln`
uses Visual Studio's bundled CMake when available, then falls back to `PATH`, and
builds the bundled GLFW static library as part of the project. CMake presets are
also available for a standalone build:

```text
cmake --preset windows-x64
cmake --build --preset windows-debug
```

The executable and copied `Data` directory are produced under `build/windows-x64`.

## Linux

Install CMake 3.20 or newer, a C++17 compiler, OpenGL development files, and the
development packages for GLFW's X11 and Wayland backends (both are enabled by
default). X11 normally needs Xlib, XRandR, Xinerama, Xcursor, and Xi headers;
Wayland needs Wayland, wayland-protocols, wayland-scanner, and xkbcommon. If one
backend is unavailable, disable it with `-DHB_GLFW_BUILD_X11=OFF` or
`-DHB_GLFW_BUILD_WAYLAND=OFF` when configuring CMake.

```text
cmake --preset linux-debug
cmake --build --preset linux-debug --parallel
```

## macOS

Install Xcode command-line tools and CMake 3.20 or newer. The build uses the system
OpenGL framework and GLFW's bundled Cocoa backend.

```text
cmake --preset macos-debug
cmake --build --preset macos-debug --parallel
```

The application requests an OpenGL 3.3 core context. The renderer uses shader,
vertex-array, and buffer APIs; no fixed-function OpenGL calls are used. GLFW handles
window creation, input, framebuffer resizing, event processing, and buffer swaps.

## Runtime data and Korean text

Build output includes `Data/` beside the executable. This includes the game content,
resource meshes and textures, and the bundled Nanum Gothic font. The game resolves
these paths relative to the executable, so it does not depend on a particular IDE
working directory or a Korean font installed by the operating system.

PNG materials are decoded by the bundled stb_image source. Korean UI labels are
rasterized from the bundled TrueType font by stb_truetype and then rendered through
the existing OpenGL text cache. The font's SIL Open Font License is included beside
the font in `SimpleGame/Data/Fonts/OFL.txt`.

GLFW source and license are under `third_party/glfw/`; GLEW source and license are
under `third_party/glew/`; stb headers and their license are under `third_party/stb/`.
Versions are pinned in the source tree so builds do not download dependencies.
