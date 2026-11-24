$ErrorActionPreference = 'Stop'
$compiler = 'C:\msys64\ucrt64\bin'
$env:PATH = "$compiler;$env:PATH"
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force local-build | Out-Null
    $glfw = 'build/_deps/glfw-src'
    $glad = 'build/gladsources/glad_gl_core_33'
    $sources = @('context','init','input','monitor','platform','vulkan','window','egl_context','osmesa_context','null_init','null_monitor','null_window','null_joystick','win32_module','win32_time','win32_thread','win32_init','win32_joystick','win32_monitor','win32_window','wgl_context')
    foreach ($name in $sources) {
        & "$compiler\gcc.exe" -O2 -D_GLFW_WIN32 "-I$glfw/include" -c "$glfw/src/$name.c" -o "local-build/$name.o"
        if ($LASTEXITCODE) { throw "GLFW build failed: $name" }
    }
    & "$compiler\gcc.exe" -O2 "-I$glad/include" -c "$glad/src/gl.c" -o local-build/glad.o
    if ($LASTEXITCODE) { throw 'GLAD build failed' }
    $objects = Get-ChildItem local-build/*.o | ForEach-Object FullName
    & "$compiler\g++.exe" -std=c++17 -O2 -Wall -Wextra '-Wno-missing-field-initializers' "-I$glfw/include" "-I$glad/include" '-Ibuild/_deps/glm-src' src/main.cpp @objects -o local-build/AtlasOfImpossibleWorlds.exe -static -lopengl32 -lgdi32 -luser32 -lshell32
    if ($LASTEXITCODE) { throw 'Atlas build failed' }
} finally { Pop-Location }
