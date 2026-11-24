# The Atlas of Impossible Worlds — Prototype 0.1

A **buildable first-stage 3D OpenGL prototype** for the planned five-environment interactive art experience. This is **not yet** the finished five-world game.

## Included
- Modern OpenGL 3.3 Core with **VAOs and VBOs** (one per procedural mesh)
- **Three primitive types:** GL_TRIANGLES (platform, spheres, creature), GL_LINES (orrery rings), GL_POINTS (starfield)
- **Timed translation:** luminous manta-like creature follows a looping flight path
- **Click rotation:** clicking the window rotates the Atlas's nested rings (prototype uses global click, not object picking yet)
- **Mouse scroll scaling:** scales the green miniature world independently
- Independent model matrices for each object
- Orbit camera and full-screen toggle
- CMake FetchContent for GLFW 3.4, GLM 1.0.1, GLAD 2

## Windows prerequisites
- VS Code and MSVC (Visual Studio 2026 C++ Build Tools)
- CMake and Git
- Python 3 with `jinja2` installed (`python -m pip install Jinja2`)
- Network access during first CMake configure

## Build in VS Code PowerShell
Open VS Code **from the x64 Native Tools Command Prompt**, then open a terminal in this folder.

If Python is not on PATH, replace the example path with your actual python.exe path:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DPython_EXECUTABLE="C:/Users/Neil/AppData/Local/Programs/Python/Python314/python.exe"
cmake --build build --config Debug --target AtlasOfImpossibleWorlds
.\build\Debug\AtlasOfImpossibleWorlds.exe
```

## Controls
- **Left click:** rotate Atlas rings by 45 degrees
- **Scroll:** scale green world
- **Right mouse drag:** orbit camera
- **F11:** toggle full-screen
- **R:** reset transformations and camera
- **Esc:** quit

## Scope and limitations
This is a **technical and visual foundation**, not the finished art-directed environment. The observatory architecture, four enterable worlds, scene manager, cinematic portals, advanced shaders, world-specific interactions, and textures are **future milestones**. The current shaders are embedded in `main.cpp` to make the prototype self-contained; the `assets/shaders` directory is reserved for later refactoring.

For the rubric's phrase **Vertex Arrays**, this prototype uses **Vertex Array Objects (VAOs)**, as required by modern OpenGL Core. If your professor specifically requires legacy client-side vertex arrays, clarify that requirement before final submission.

## Collaboration
Do not commit the `build/` folder to Git. Each group member should run CMake locally to fetch and compile dependencies.
