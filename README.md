<div align="center">
# 🌌 The Atlas of Impossible Worlds

### Four impossible worlds. One interactive observatory.

![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![OpenGL](https://img.shields.io/badge/OpenGL-3.3_Core-5586A4?style=for-the-badge&logo=opengl&logoColor=white)
![GLFW](https://img.shields.io/badge/GLFW-3.4-7B61FF?style=for-the-badge)
![GLM](https://img.shields.io/badge/GLM-1.0.1-4CAF50?style=for-the-badge)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-064F8C?style=for-the-badge&logo=cmake&logoColor=white)

**[Worlds](#worlds) · [Features](#features) · [Controls](#controls) · [Build & Run](#build-and-run-on-windows)**

</div>

---

An interactive 3D fantasy observatory built with **C++17 and OpenGL 3.3 Core**. Explore four imaginary worlds from a central observatory, watch a luminous manta circle the scene, and interact with independently transformed objects.

## Preview

Explore a central fantasy observatory and four distinct environments: the Garden of Unborn Stars, Factory of Forgotten Sunsets, Library of Unwritten Universes, and Kingdom Between Seconds.

*Scene screenshots will be added here after capture from the running application.*

---

## Worlds

| World | Interaction inside the world |
| --- | --- |
| Garden of Unborn Stars | Press E to toggle blooming flowers. |
| Factory of Forgotten Sunsets | Press E to toggle the Sunset Engine. |
| Library of Unwritten Universes | Press E to toggle the unwritten book. |
| Kingdom Between Seconds | Press E to toggle the direction of time flow. |

## Features

- Procedurally generated geometry and embedded GLSL shaders.
- VAOs and VBOs for mesh storage and vertex attribute configuration.
- GL_TRIANGLES for surfaces, GL_LINES for markings and rays, and GL_POINTS for stars and particles.
- Time-driven manta translation, click-controlled Atlas ring rotation, and scroll-controlled planet scaling.
- Separate model matrices for the main objects; moons and decorative rings inherit their planet's transform.
- Object picking, orbit camera, world transitions, and WASD exploration.
- Lighting, twinkling stars, particles, bloom, and tone mapping.
- Full-screen toggle and TGA screenshot capture.

## Controls

| Input | Action |
| --- | --- |
| Left-click Atlas | Add 45 degrees to the ring rotation target. |
| Left-click a planet | Select the planet. |
| Double-click a planet / Enter with a selection | Enter its world. |
| Scroll in the hub | Scale the selected planet; defaults to Garden when none is selected. |
| Tab / Shift+Tab | Cycle planet selection. |
| F | Toggle camera focus on the selected planet. |
| Right-drag | Orbit in the hub; look around while exploring. |
| Arrow keys in the hub | Adjust the orbit camera. |
| WASD / Left Shift | Move / move faster while exploring. |
| E | Toggle the current world's effect. |
| Esc / Backspace | Return from a world; Esc exits from the hub. |
| F11 | Toggle full-screen mode. |
| R in the hub | Reset ring rotation target, planet scales, and camera. |
| B | Toggle bloom. |
| P | Save a TGA screenshot. |

The first click during the introduction skips it. To rotate the Atlas, click its hit area after the introduction. Planet scale is limited to 0.45–2.8. The application currently starts in a 1280×720 window; press F11 for full-screen display.

## Requirements

- A C++17 compiler and CMake 3.20 or newer.
- Git, Python 3, and the Python Jinja2 package for GLAD generation.
- A graphics driver supporting OpenGL 3.3 Core.
- Internet access for the first CMake configuration to fetch dependencies.

CMake fetches GLFW 3.4, GLM 1.0.1, and GLAD from its glad2 branch. Install compiler/build tools separately; VS Code is the editor.

## Build and run on Windows

Install Visual Studio Build Tools with the **Desktop development with C++** workload, CMake, Git, and Python. Open a developer terminal, then run:

```powershell
git clone https://github.com/arendelleh/Atlas_of_Impossible_Worlds.git
cd Atlas_of_Impossible_Worlds
python -m pip install Jinja2
cmake -S . -B build -A x64
cmake --build build --config Release --target AtlasOfImpossibleWorlds
.\build\Release\AtlasOfImpossibleWorlds.exe
```

These commands assume a Visual Studio generator. For a single-configuration generator such as Ninja, configure with `-DCMAKE_BUILD_TYPE=Release` and run the executable directly from `build`.

The optional `build-local.ps1` is an existing machine-specific MSYS2 helper. It expects `C:\msys64\ucrt64\bin` and dependencies/generated GLAD files already under `build`; it is not the fresh-clone setup path.

## Repository layout

```text
.
├── README.md
├── .gitignore
├── CMakeLists.txt
├── build-local.ps1
├── src/
│   └── main.cpp
├── assets/
│   └── shaders/README.md
└── archive/
    └── main.before-atlas-restoration.cpp
```

`src/main.cpp` contains the active application and embedded shaders. The shader asset directory is reserved for future extraction. The archived source is a historical backup and is not compiled. Build output and downloaded dependencies are excluded from Git.

## Graphics implementation

`Mesh::upload` allocates vertex storage in a VBO and records the attribute layout in a VAO. `Mesh::draw` binds that VAO and calls `glDrawArrays`. Model matrices position each object separately. `glfwGetTime` drives the manta's circular flight and bobbing; GLFW callbacks collect mouse, wheel, and keyboard input for dispatch in the main loop.

## Manual verification checklist

These are expected checks, not recorded test results:

- Toggle F11 twice; verify full-screen display, restored window size, and accurate picking.
- Watch the manta without input; expect a circular lap in approximately 11.4 seconds with vertical bobbing.
- Click the Atlas after the intro; expect a smooth ring turn and the rotation console message.
- Scroll both ways; expect the selected planet's scale to stay within 0.45–2.8.
- Scale a planet and rotate the Atlas; expect unrelated objects to retain their own transforms.
- Enter each world, move, toggle E, and return to the hub.

## Project context and AI transparency

Created for a group computer graphics project demonstrating primitives, vertex arrays, VBOs, transformations, animation, and mouse input. Significant AI assistance must be disclosed in the group's AI Usage Log, including code explanations, review, debugging suggestions, and documentation/repository assistance. Record only changes and tests the group actually performed. This README does not certify assignment compliance or replace the usage log and recorded walkthrough.

## Dependencies and licensing

GLFW, GLM, and GLAD retain their respective upstream licenses. No project license has been added here; public visibility alone does not grant a software reuse license.
