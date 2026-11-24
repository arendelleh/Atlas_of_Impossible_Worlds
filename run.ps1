param([switch]$Build)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $program = Join-Path $PSScriptRoot 'local-build/AtlasOfImpossibleWorlds.exe'
    $mainSource = Join-Path $PSScriptRoot 'src/main.cpp'
    if ($Build -or !(Test-Path -LiteralPath $program) -or ((Get-Item $mainSource).LastWriteTime -gt (Get-Item $program).LastWriteTime)) {
        Write-Host 'Building Atlas. Please wait...'
        & (Join-Path $PSScriptRoot 'build-local.ps1')
        if (!(Test-Path -LiteralPath $program)) { throw 'Build did not produce AtlasOfImpossibleWorlds.exe.' }
    }
    Write-Host 'Opening The Atlas of Impossible Worlds...'
    & $program
    if ($LASTEXITCODE -ne 0) { throw "Atlas exited with code $LASTEXITCODE." }
} finally { Pop-Location }
