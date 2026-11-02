# Build Swell Chord Motion on Windows (Standalone + VST3)
$ErrorActionPreference = 'Stop'

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter cmake.exe -ErrorAction SilentlyContinue |
             Select-Object -First 1 -ExpandProperty FullName
}
if (-not $cmake) { throw "CMake introuvable. Installe-le avec: winget install Kitware.CMake" }

& $cmake -S $PSScriptRoot -B "$PSScriptRoot\build" -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { throw "La configuration CMake a echoue" }

& $cmake --build "$PSScriptRoot\build" --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "La compilation a echoue" }

$out = "$PSScriptRoot\build\SwellChordMotion_artefacts\Release"
Write-Host ""
Write-Host "OK !"
Write-Host "Standalone : $out\Standalone\Swell Chord Motion.exe"
Write-Host "VST3       : $out\VST3\Swell Chord Motion.vst3"
