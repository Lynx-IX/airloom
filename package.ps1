# Builds Live Rig and makes a copy ready to move to another computer:
#   Release\Live Rig\Live Rig.exe + How to install.txt
#   Release\Live Rig for Windows.zip
#
# Run from the project folder:  powershell -ExecutionPolicy Bypass -File package.ps1

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot
$build = Join-Path $env:TEMP "LiveRig-build"   # short path: MSBuild breaks past 260 characters

if (Get-Process "Live Rig" -ErrorAction SilentlyContinue) {
    throw "Close Live Rig first - Windows won't let the build replace a running program."
}

cmake -S $project -B $build -G "Visual Studio 18 2026" -A x64 | Out-Null
cmake --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "The build failed." }

$exe = Join-Path $build "LiveRig_artefacts\Release\Live Rig.exe"
$out = Join-Path $project "Release"
$folder = Join-Path $out "Live Rig"
$zip = Join-Path $out "Live Rig for Windows.zip"

Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $folder | Out-Null
Copy-Item $exe $folder
Copy-Item (Join-Path $project "Packaging\How to install.txt") $folder
Compress-Archive -Path $folder -DestinationPath $zip

$size = "{0:N1} MB" -f ((Get-Item $zip).Length / 1MB)
Write-Host "Ready: $zip ($size)"
Write-Host "Or copy the folder: $folder"
